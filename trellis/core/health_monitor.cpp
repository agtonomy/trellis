/*
 * Copyright (C) 2022 Agtonomy
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "trellis/core/health_monitor.hpp"

#include "trellis/core/health.hpp"
#include "trellis/core/logging.hpp"

namespace trellis {
namespace core {

namespace {
bool operator==(const google::protobuf::Timestamp& lhs, const google::protobuf::Timestamp& rhs) {
  return lhs.nanos() == rhs.nanos() && lhs.seconds() == rhs.seconds();
}

bool operator!=(const google::protobuf::Timestamp& lhs, const google::protobuf::Timestamp& rhs) {
  return !(lhs == rhs);
}
}  // namespace

HealthMonitor::HealthMonitor(const trellis::core::EventLoop& loop, const trellis::core::Config& config,
                             const TimerCreateFunction& timer_create_fn,
                             const SubscriberCreateFunction& subscriber_create_fn,
                             const HealthEventCallback& health_event_callback)
    : ev_loop_{loop},
      reporting_watchdog_ms_{GetReportingIntervalTimeoutFromConfig(config)},
      timer_create_fn_{timer_create_fn},
      subscriber_{
          subscriber_create_fn(trellis::core::Health::GetTopicFromConfig(config),
                               [this](const time::TimePoint&, const time::TimePoint&,
                                      std::unique_ptr<trellis::core::HealthHistory> status) { NewUpdate(*status); })},
      health_event_cb_{health_event_callback} {}

void HealthMonitor::NewUpdate(const trellis::core::HealthHistory& status) {
  const std::string& name = status.name();
  if (status.history().empty()) {
    trellis::core::Log::Warn("Received zero-length health update from {}. This shouldn't ever happen.", name);
    return;
  }

  const trellis::core::HealthStatus& most_recent_update_from_message = GetMostRecentNodeHealthFromMessage(status);

  // First we will determine if we should call back to the user based on this new message. And then only after we will
  // update the cache since our logic depends on the current cache

  const bool first_encounter = !HasHealthInfo(name);
  // The first time we encounter an update from the node, we will always call back
  bool dispatch_event = first_encounter;
  if (!first_encounter) {
    // This isn't our first time encountering this node, so we already have cached info. Let's use the cache to see if
    // our new message contains new information
    const trellis::core::HealthStatus most_recent_update_from_cache = GetMostRecentNodeHealthFromCache(name);
    const bool message_contains_new_info =
        (most_recent_update_from_message.timestamp() != most_recent_update_from_cache.timestamp());

    dispatch_event = message_contains_new_info;
  }

  // Insert into the cache before dispatching the event to the user
  InsertIntoCache(status);

  if (dispatch_event) {
    const Event event =
        (most_recent_update_from_message.health_state() == trellis::core::HealthState::HEALTH_STATE_NORMAL)
            ? Event::kNodeTransitionToHealthy
            : Event::kNodeTransitionToUnhealthy;
    health_event_cb_(name, event, time::Now());
  }
}

void HealthMonitor::InsertIntoCache(const trellis::core::HealthHistory& status) {
  // Insert into our cache and reset the watchdog
  std::lock_guard<std::mutex> guard{mutex_};
  const std::string& name = status.name();

  if (health_data_.find(name) != health_data_.end()) {
    Data& data = health_data_.at(name);
    data.history_ = status.history();
    if (data.watchdog_ != nullptr) {
      data.watchdog_->Reset();
    }
  } else {
    health_data_.insert({name,
                         {{timer_create_fn_(reporting_watchdog_ms_,
                                            [this, name](const time::TimePoint& now) { WatchdogExpired(name, now); })},
                          {status.history()}}});
  }
}

void HealthMonitor::WatchdogExpired(const std::string& name, const time::TimePoint& now) {
  // Add a new entry to signify that the health state was lost
  std::lock_guard<std::mutex> guard{mutex_};
  if (health_data_.find(name) != health_data_.end()) {
    auto& history = health_data_.at(name).history_;
    trellis::core::HealthStatus* new_status = history.Add();
    new_status->set_health_state(trellis::core::HealthState::HEALTH_STATE_LOST);
    *new_status->mutable_timestamp() = trellis::core::time::TimePointToTimestamp(now);
    health_event_cb_(name, Event::kNodeHealthUpdateLost, now);
  }
}

bool HealthMonitor::IsAllHealthy() {
  std::lock_guard<std::mutex> guard{mutex_};
  for (const auto& [name, data] : health_data_) {
    const trellis::core::HealthStatus& last_update =
        data.history_.at(data.history_.size() - 1);  // History list is guaranteed to be non-zero
    if (last_update.health_state() != trellis::core::HealthState::HEALTH_STATE_NORMAL) {
      return false;
    }
  }
  return true;
}

std::set<std::string> HealthMonitor::GetNodeNames() {
  std::lock_guard<std::mutex> guard{mutex_};
  std::set<std::string> result;
  std::transform(health_data_.begin(), health_data_.end(), std::inserter(result, result.end()),
                 [](const auto& pair) { return pair.first; });
  return result;
}

bool HealthMonitor::HasHealthInfo(const std::string& name) {
  std::lock_guard<std::mutex> guard{mutex_};
  return health_data_.find(name) != health_data_.end();
}

const trellis::core::HealthStatus HealthMonitor::GetMostRecentNodeHealthFromCache(const std::string& name) {
  std::lock_guard<std::mutex> guard{mutex_};
  const auto& history = health_data_.at(name).history_;
  const auto size = history.size();
  if (size == 0) {
    throw std::logic_error("Encountered zero-length health hitory from cache for " + name);
  }
  return history.at(size - 1);  // History list is guaranteed to be non-zero
}

const trellis::core::HealthStatus& HealthMonitor::GetMostRecentNodeHealthFromMessage(
    const trellis::core::HealthHistory& status) {
  const auto& history = status.history();
  const auto size = history.size();
  if (size == 0) {
    throw std::logic_error("Encountered zero-length health history from message for " + status.name());
  }
  return history.at(size - 1);  // History list is guaranteed to be non-zero
}

void HealthMonitor::CheckNameExists(const std::string& name) const {
  // Expected to be called in mutex lock region
  if (health_data_.find(name) == health_data_.end()) {
    throw std::runtime_error("Attempt to retrieve health information for " + name + " , which does not exist");
  }
}

const trellis::core::HealthStatus HealthMonitor::GetLastHealthUpdate(const std::string& name) {
  std::lock_guard<std::mutex> guard{mutex_};
  CheckNameExists(name);
  const auto& history = health_data_.at(name).history_;
  return history.at(history.size() - 1);  // History list is guaranteed to be non-zero
}

const HealthMonitor::HealthHistoryList HealthMonitor::GetHealthHistory(const std::string& name) {
  std::lock_guard<std::mutex> guard{mutex_};
  CheckNameExists(name);
  return health_data_.at(name).history_;
}

unsigned HealthMonitor::GetReportingIntervalTimeoutFromConfig(const trellis::core::Config& config) {
  if (config["trellis"] && config["trellis"]["health"] && config["trellis"]["health"]["reporting_watchdog_ms"]) {
    return config["trellis"]["health"]["reporting_watchdog_ms"].as<unsigned>();
  }
  return 800U;  // default fallback
}

}  // namespace core
}  // namespace trellis
