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

#include "health.hpp"

#include "logging.hpp"
#include "trellis/core/health_status.pb.h"

namespace trellis {
namespace core {

namespace {
bool operator==(const trellis::core::HealthStatus& lhs, const trellis::core::HealthStatus& rhs) {
  if (lhs.health_state() != rhs.health_state()) {
    return false;
  }
  if (lhs.status_code() != rhs.status_code()) {
    return false;
  }
  if (lhs.status_description() != rhs.status_description()) {
    return false;
  }
  return true;
}
}  // namespace

Health::Health(const std::string& name, const trellis::core::Config& config,
               const PublisherCreateFunction& publisher_create_fn, const TimerCreateFunction& timer_create_fn)
    : name_{name},
      reporting_topic_{GetTopicFromConfig(config)},
      reporting_interval_ms_{GetReportingIntervalFromConfig(config)},
      history_size_{GetHistorySizeFromConfig(config)},
      publisher_create_fn_{publisher_create_fn},
      timer_create_fn_{timer_create_fn} {}

void Health::Update(const trellis::core::HealthState& state, const Code& code, const std::string& description) {
  const auto update = CreateHealthUpdateMessage(state, code, description);
  if (!health_history_.empty()) {
    const auto& last = health_history_.back();
    if (update == last) {
      return;  // don't push a new update if it's identical to the last
    }
  }

  LogUpdate(update);

  health_history_.push_back(update);

  if (health_history_.size() > history_size_) {
    health_history_.pop_front();
  }

  // We lazily create the publisher and timer here so that we guarantee the middleware layer is fully initialized, and
  // we don't waste resources for applications that don't use these health alerting features.
  if (publisher_ == nullptr) {
    publisher_ = publisher_create_fn_(reporting_topic_);
  }

  if (update_timer_ == nullptr) {
    update_timer_ = timer_create_fn_(reporting_interval_ms_, [this](const time::TimePoint&) { UpdateTimer(); });
  }

  publisher_->Send(CreateHealthHistoryMessage(name_, health_history_));
}

trellis::core::HealthState Health::GetHealthState() const {
  if (health_history_.empty()) {
    throw std::runtime_error("Attempt to get health state before any health updates were made.");
  }
  return health_history_.back().health_state();
}

const Health::HealthHistory& Health::GetHealthHistory() const { return health_history_; }

const trellis::core::HealthStatus& Health::GetLastHealthStatus() const {
  if (health_history_.empty()) {
    throw std::runtime_error("Attempt to get last health update before any health updates were made.");
  }
  return health_history_.back();
}

trellis::core::HealthHistory Health::CreateHealthHistoryMessage(const std::string& name,
                                                                const HealthHistory& health_history) {
  trellis::core::HealthHistory msg;
  msg.set_name(name);
  *msg.mutable_history() =
      google::protobuf::RepeatedPtrField<trellis::core::HealthStatus>(health_history.begin(), health_history.end());
  return msg;
}

trellis::core::HealthStatus Health::CreateHealthUpdateMessage(const trellis::core::HealthState& state, const Code& code,
                                                              const std::string& description) {
  trellis::core::HealthStatus msg;
  *msg.mutable_timestamp() = time::TimePointToTimestamp(time::Now());
  msg.set_health_state(state);
  msg.set_status_code(code);
  msg.set_status_description(description);
  return msg;
}

void Health::UpdateTimer() { publisher_->Send(CreateHealthHistoryMessage(name_, health_history_)); }

void Health::LogUpdate(const trellis::core::HealthStatus& update) const {
  static constexpr std::string_view first_update_no_code = "{} health state is {}";
  static constexpr std::string_view first_update_with_code = "{} health state is {} (code = {} description = {})";
  static constexpr std::string_view update_no_code = "{} health state changed {} -> {}";
  static constexpr std::string_view update_with_code = "{} health state changed {} -> {} (code = {} description = {})";

  if (!health_history_.empty()) {
    const auto& last = health_history_.back();
    if (update.status_code() == 0 && update.status_description().empty()) {
      if (update.health_state() == trellis::core::HealthState::HEALTH_STATE_NORMAL) {
        Log::Info(update_no_code, name_, trellis::core::HealthState_Name(last.health_state()),
                  trellis::core::HealthState_Name(update.health_state()));
      } else {
        Log::Warn(update_no_code, name_, trellis::core::HealthState_Name(last.health_state()),
                  trellis::core::HealthState_Name(update.health_state()));
      }

    } else {
      if (update.health_state() == trellis::core::HealthState::HEALTH_STATE_NORMAL) {
        Log::Info(update_with_code, name_, trellis::core::HealthState_Name(last.health_state()),
                  trellis::core::HealthState_Name(update.health_state()), update.status_code(),
                  update.status_description());
      } else {
        Log::Warn(update_with_code, name_, trellis::core::HealthState_Name(last.health_state()),
                  trellis::core::HealthState_Name(update.health_state()), update.status_code(),
                  update.status_description());
      }
    }
  } else {
    if (update.status_code() == 0 && update.status_description().empty()) {
      if (update.health_state() == trellis::core::HealthState::HEALTH_STATE_NORMAL) {
        Log::Info(first_update_no_code, name_, trellis::core::HealthState_Name(update.health_state()));
      } else {
        Log::Warn(first_update_no_code, name_, trellis::core::HealthState_Name(update.health_state()));
      }

    } else {
      if (update.health_state() == trellis::core::HealthState::HEALTH_STATE_NORMAL) {
        Log::Info(first_update_with_code, name_, trellis::core::HealthState_Name(update.health_state()),
                  update.status_code(), update.status_description());
      } else {
        Log::Warn(first_update_with_code, name_, trellis::core::HealthState_Name(update.health_state()),
                  update.status_code(), update.status_description());
      }
    }
  }
}

std::string Health::GetTopicFromConfig(const trellis::core::Config& config) {
  if (config["trellis"] && config["trellis"]["health"] && config["trellis"]["health"]["reporting_topic"]) {
    return config["trellis"]["health"]["reporting_topic"].as<std::string>();
  }
  return "/trellis/app/health";  // default fallback
}

unsigned Health::GetReportingIntervalFromConfig(const trellis::core::Config& config) {
  if (config["trellis"] && config["trellis"]["health"] && config["trellis"]["health"]["interval_ms"]) {
    return config["trellis"]["health"]["interval_ms"].as<unsigned>();
  }
  return 200U;  // default fallback
}

unsigned Health::GetHistorySizeFromConfig(const trellis::core::Config& config) {
  if (config["trellis"] && config["trellis"]["health"] && config["trellis"]["health"]["history_size"]) {
    return config["trellis"]["health"]["history_size"].as<unsigned>();
  }
  return 10U;  // default fallback
}

}  // namespace core
}  // namespace trellis
