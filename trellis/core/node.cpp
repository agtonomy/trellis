/*
 * Copyright (C) 2021 Agtonomy
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

#include "trellis/core/node.hpp"

#include <queue>
#include <thread>

#include "trellis/core/ipc/utils.hpp"
#include "trellis/core/timer_registry.hpp"

using namespace trellis::core;

Node::Node(std::string_view name, trellis::core::Config config)
    : name_{name},
      config_{std::move(config)},
      crash_counter_{config_, name_, trellis::core::ipc::utils::GetUidGidFromConfig(config_).first,
                     trellis::core::ipc::utils::GetUidGidFromConfig(config_).second},
      ev_loop_{std::make_shared<TimerRegistry>()},
      discovery_{std::make_shared<trellis::core::discovery::Discovery>(name_, ev_loop_, config_)},
      signal_set_(*ev_loop_, SIGTERM, SIGINT),
      health_{std::string(name), config_,
              [this](const std::string& topic) { return CreatePublisher<trellis::core::HealthHistory>(topic); },
              [this](unsigned interval_ms, trellis::core::TimerImpl::Callback cb) {
                return CreateTimer(interval_ms, cb);
              }} {
  Log::SetLogLevel(config_.AsIfExists<std::string>("trellis.logging.log_level", "fatal"));
  const int unclean_exits = crash_counter_.UncleanExitCount();
  if (unclean_exits > 0) {
    Log::Error("{} starting after {} consecutive unclean exit(s)", name_, unclean_exits);
  }
  // Handle signals explicitly, allowing the user to supply their own handler
  signal_set_.async_wait([this](const trellis::core::error_code& error, int signal_number) {
    if (!error) {
      ipc::NamedResourceRegistry::Get().UnlinkAll();
      if (user_handler_) user_handler_(signal_number);
      Log::Info("{} node stopping...", name_);
      Stop();
    }
  });

  if (config_.AsIfExists<bool>("trellis.health.auto_report", false)) {
    // Kick off health reporting for this node
    UpdateHealth(trellis::core::HealthState::HEALTH_STATE_NORMAL);
  }

  // Initialize metrics publisher if enabled
  if (config_.AsIfExists<bool>("trellis.metrics.enabled", false)) {
    const auto metrics_topic = config.AsIfExists<std::string>("trellis.metrics.topic", "/trellis/app/metrics");
    const auto metrics_interval_ms = config.AsIfExists<unsigned>("trellis.metrics.interval_ms", 60000);

    // Deliberately an application timer rather than a management one. Publication being late is itself evidence that
    // this node's loop is starving, which is what the timer metrics exist to surface, so this timer belongs in the
    // figures rather than excluded from them -- and it was counted before the application/management split existed.
    metrics_.emplace(
        trellis::utils::metrics::MetricsPublisher(
            name_, CreatePublisher<trellis::utils::metrics::MetricsGroup>(metrics_topic)),
        CreateTimer(metrics_interval_ms, [this](const time::TimePoint& now) {
          metrics_->first.AddCounter(now, "timer_overrun_count", static_cast<int64_t>(GetTimerOverrunCount()));
          metrics_->first.AddMeasurement(now, "unclean_exit_count", static_cast<double>(GetUncleanExitCount()));
          const auto sched_stats = GetAndResetTimerSchedLatencyStats();
          if (sched_stats.count > 0) {
            metrics_->first.AddMeasurement(now, "timer_sched_latency_max_us", static_cast<double>(sched_stats.max_us));
            metrics_->first.AddMeasurement(now, "timer_sched_latency_mean_us", sched_stats.mean_us);
          }

          // Collect and publish subscriber latency stats
          for (const auto& weak_sub : subscribers_) {
            if (auto sub = weak_sub.lock()) {
              const auto stats = sub->GetLatestLatencyStats();
              if (stats.count > 0) {
                const auto& topic = sub->GetTopic();
                metrics_->first.AddMeasurement(now, topic + "__latency_min_us", static_cast<double>(stats.min_us));
                metrics_->first.AddMeasurement(now, topic + "__latency_mean_us", static_cast<double>(stats.mean_us));
                metrics_->first.AddMeasurement(now, topic + "__latency_max_us", static_cast<double>(stats.max_us));
              }
            }
          }

          metrics_->first.Publish(now);
        }));
  }
}

Node::~Node() { Stop(); }

int Node::Run() {
  Log::Debug("{} node running...", name_);
  try {
    while (ShouldRun()) {
      ev_loop_.RunFor(std::chrono::milliseconds(500));
      if (ev_loop_.Stopped()) {
        break;  // the event loop was explicitly stopped
      }
    }
  } catch (const std::exception& e) {
    Log::Error("Unhandled std::exception: {}", e.what());
    crash_counter_.MarkUncleanExit();
    ipc::NamedResourceRegistry::Get().UnlinkAll();
    return 1;
  } catch (...) {
    Log::Error("Unhandled unknown exception occurred.");
    crash_counter_.MarkUncleanExit();
    ipc::NamedResourceRegistry::Get().UnlinkAll();
    return 1;
  }

  return 0;
}

bool Node::RunN(const unsigned n) {
  try {
    unsigned count{0};
    // poll_one will return immediately (never block). If it returned 0 there's
    // nothing to do right now, so we'll just drop out of the loop, otherwise we keep
    // polling so long as work is being done
    while (ShouldRun() && ev_loop_.PollOne() && count++ < n);
    return ShouldRun();
  } catch (const std::exception& e) {
    Log::Error("Unhandled std::exception: {}", e.what());
    crash_counter_.MarkUncleanExit();
    ipc::NamedResourceRegistry::Get().UnlinkAll();
    return false;
  } catch (...) {
    Log::Error("Unhandled unknown exception occurred.");
    crash_counter_.MarkUncleanExit();
    ipc::NamedResourceRegistry::Get().UnlinkAll();
    return false;
  }
}

bool Node::ShouldRun() {
  const bool should_run = (!ev_loop_.Stopped() || first_run_);
  first_run_ = false;
  return should_run;
}

PeriodicTimer Node::CreatePeriodicTimer(unsigned interval_ms, TimerImpl::Callback callback, unsigned initial_delay_ms) {
  auto timer = Node::CreateTimer<PeriodicTimerImpl>(interval_ms, std::move(callback), initial_delay_ms);
  return timer;
}

OneShotTimer Node::CreateOneShotTimer(unsigned initial_delay_ms, TimerImpl::Callback callback) {
  auto timer = Node::CreateTimer<OneShotTimerImpl>(std::move(callback), initial_delay_ms);
  return timer;
}

void Node::Stop() { ev_loop_.Stop(); }

void Node::UpdateHealth(const trellis::core::HealthStatus& status, const bool compare_description) {
  UpdateHealth(status.health_state(), status.status_code(), status.status_description(), compare_description);
}

void Node::UpdateHealth(trellis::core::HealthState state, Health::Code code, const std::string& description,
                        const bool compare_description) {
  health_.Update(state, code, description, compare_description);
}

trellis::core::HealthState Node::GetHealthState() const { return health_.GetHealthState(); }

const trellis::core::HealthStatus& Node::GetLastHealthStatus() const { return health_.GetLastHealthStatus(); }

const Health::HealthHistory& Node::GetHealthHistory() const { return health_.GetHealthHistory(); }

void Node::AddSignalHandler(const SignalHandler& handler) { user_handler_ = handler; }

uint64_t Node::GetTimerOverrunCount() const {
  uint64_t total = 0;
  // ForEach holds the registry lock, so an entry cannot be erased partway through the walk and entry.timer stays a
  // valid pointer. That is all the lock buys: it serializes entry lifetime, not a timer's internals, so the counters
  // read here must be safe to read concurrently in their own right. Neither this nor the collection below runs a user
  // callback, so the no-reentrancy contract holds.
  ev_loop_.GetTimerRegistry()->ForEach([&total](const TimerRegistry::Entry& entry) {
    if (entry.kind == TimerKind::kApplication) {
      total += entry.timer->GetOverrunCount();
    }
  });
  return total;
}

TimerImpl::SchedLatencyStats Node::GetAndResetTimerSchedLatencyStats() {
  TimerImpl::SchedLatencyStats combined{};
  ev_loop_.GetTimerRegistry()->ForEach([&combined](const TimerRegistry::Entry& entry) {
    if (entry.kind != TimerKind::kApplication) {
      return;  // resetting a timer this node does not own would steal the samples from whoever does
    }
    const auto stats = entry.timer->GetAndResetSchedLatencyStats();
    if (stats.max_us > combined.max_us) {
      combined.max_us = stats.max_us;
    }
    combined.total_us += stats.total_us;
    combined.count += stats.count;
  });
  combined.mean_us =
      combined.count > 0 ? static_cast<double>(combined.total_us) / static_cast<double>(combined.count) : 0.0;
  return combined;
}

void Node::UpdateSimulatedClock(const time::TimePoint& new_time) {
  if (time::IsSimulatedClockEnabled()) {
    asio::post(*ev_loop_, [this, new_time]() {
      auto existing_time = time::Now();
      bool reset_timers{false};
      if (new_time >= existing_time) {
        const auto registry = ev_loop_.GetTimerRegistry();
        // Only timers the simulated clock drives belong here. Anything else is driven by asio and its expiry is a
        // steady clock reading, so comparing it against simulated time would be comparing two unrelated epochs -- and
        // since a non-simulated Reload() advances expiry by a single interval, catching such a timer up would spin once
        // per interval across the gap between those epochs.
        std::vector<TimerRegistry::Entry> entries;
        for (const auto& entry : registry->GetEntries()) {
          if (entry.timer->IsSimulationDriven()) {
            entries.push_back(entry);
          }
        }
        if (!entries.empty()) {
          if (time::TimePointToMilliseconds(existing_time) != 0) {
            // A queued timer holds its expiry by value: pop() and push() re-heapify, which runs the comparator against
            // other queued timers, and a callback fired below may have destroyed one of those. The comparator must
            // therefore never dereference, and every dereference of a popped timer is guarded by its registration
            // handle, which -- unlike an address -- the allocator cannot recycle onto a different timer.
            struct QueuedTimer {
              TimerImpl* timer{nullptr};
              TimerRegistry::RegistrationHandle handle{TimerRegistry::kInvalidRegistrationHandle};
              time::TimePoint expiry;
            };
            // Timers sharing an expiry fire in creation order. Handles are monotonic, so ordering on them gives that,
            // and it keeps the sequence reproducible across runs -- entries arrive here in the registry map's iteration
            // order, which is unspecified and shifts with its rehash history.
            auto timer_comp = [](const QueuedTimer& a, const QueuedTimer& b) {
              return a.expiry != b.expiry ? a.expiry > b.expiry : a.handle > b.handle;
            };
            std::priority_queue<QueuedTimer, std::vector<QueuedTimer>, decltype(timer_comp)> expired_timers(timer_comp);

            // First find all the non-cancelled timers that are expiring before our new_time
            for (const auto& entry : entries) {
              const auto expiry = entry.timer->GetExpiry();
              if (!entry.timer->IsCancelled() && new_time >= expiry) {
                expired_timers.push(QueuedTimer{.timer = entry.timer, .handle = entry.handle, .expiry = expiry});
              }
            }

            // Step forward in time while firing the timers that are expiring until there are no more timers to fire
            while (!expired_timers.empty()) {
              const auto top = expired_timers.top();
              expired_timers.pop();
              // An earlier callback may have destroyed this timer, which deregisters it
              if (!registry->Contains(top.handle)) {
                continue;
              }
              // Move our simulated time up to the expiration time of this timer
              time::SetSimulatedTime(top.expiry);
              top.timer->Fire();  // Fire the timer (which updates the expiry time also)

              // The callback we just ran may have destroyed this timer too
              if (!registry->Contains(top.handle) || top.timer->GetType() == TimerImpl::Type::kOneShot) {
                continue;
              }
              // If our expiry time is still earlier than our new_time, put it back in the queue for another go
              const auto next_expiry = top.timer->GetExpiry();
              if (new_time >= next_expiry) {
                expired_timers.push(QueuedTimer{.timer = top.timer, .handle = top.handle, .expiry = next_expiry});
              }
            }
          } else {
            // This is our first jump forward in time, reset all the timers so their expiry times are sane
            reset_timers = true;
          }
        }
        time::SetSimulatedTime(new_time);
        // If we need to reset timers, it needs to happen after the new time is updated. Reusing the entries read above
        // is safe here because this branch is mutually exclusive with the one that fires callbacks, so nothing has run
        // that could have destroyed a timer since.
        if (reset_timers) {
          for (const auto& entry : entries) {
            entry.timer->Reset();
          }
        }
      } else {
        Log::Debug("Ignored attempt to rewind simulated clock. Current time {} Set time {}",
                   time::TimePointToSeconds(existing_time), time::TimePointToSeconds(new_time));
      }
    });
  }
}
