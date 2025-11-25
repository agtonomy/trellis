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

using namespace trellis::core;

Node::Node(std::string name, trellis::core::Config config)
    : name_{name},
      config_{config},
      ev_loop_{},
      discovery_{std::make_shared<trellis::core::discovery::Discovery>(name_, ev_loop_, config_)},
      signal_set_(*ev_loop_, SIGTERM, SIGINT),
      health_{name, config,
              [this](const std::string& topic) { return CreatePublisher<trellis::core::HealthHistory>(topic); },
              [this](unsigned interval_ms, trellis::core::TimerImpl::Callback cb) {
                return CreateTimer(interval_ms, cb);
              }} {
  Log::SetLogLevel(config.AsIfExists<std::string>("trellis.logging.log_level", "fatal"));
  // Handle signals explicitly, allowing the user to supply their own handler
  signal_set_.async_wait([this](const trellis::core::error_code& error, int signal_number) {
    if (!error) {
      ipc::NamedResourceRegistry::Get().UnlinkAll();
      if (user_handler_) user_handler_(signal_number);
      Log::Info("{} node stopping...", name_);
      Stop();
    }
  });

  if (config.AsIfExists<bool>("trellis.health.auto_report", false)) {
    // Kick off health reporting for this node
    UpdateHealth(trellis::core::HealthState::HEALTH_STATE_NORMAL);
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
    ipc::NamedResourceRegistry::Get().UnlinkAll();
    return 1;
  } catch (...) {
    Log::Error("Unhandled unknown exception occurred.");
    ipc::NamedResourceRegistry::Get().UnlinkAll();
    return 1;
  }

  return 0;
}

bool Node::RunN(unsigned n) {
  unsigned count = 0;
  try {
    // poll_one will return immediately (never block). If it returned 0 there's
    // nothing to do right now and so we'll just drop out of the loop, otherwise we keep
    // polling so long as work is being done
    while (ShouldRun() && ev_loop_.PollOne() && count++ < n);
    return ShouldRun();
  } catch (const std::exception& e) {
    Log::Error("Unhandled std::exception: {}", e.what());
    ipc::NamedResourceRegistry::Get().UnlinkAll();
    return 1;
  } catch (...) {
    Log::Error("Unhandled unknown exception occurred.");
    ipc::NamedResourceRegistry::Get().UnlinkAll();
    return 1;
  }
}

bool Node::ShouldRun() {
  const bool should_run = (!ev_loop_.Stopped() || first_run_);
  first_run_ = false;
  return should_run;
}

Timer Node::CreateTimer(unsigned interval_ms, TimerImpl::Callback callback, unsigned initial_delay_ms) {
  auto timer =
      std::make_shared<TimerImpl>(GetEventLoop(), TimerImpl::Type::kPeriodic, callback, interval_ms, initial_delay_ms);

  timers_.emplace_back(std::weak_ptr<TimerImpl>(timer));
  return timer;
}

Timer Node::CreateOneShotTimer(unsigned initial_delay_ms, TimerImpl::Callback callback) {
  auto timer =
      std::make_shared<TimerImpl>(GetEventLoop(), TimerImpl::Type::kOneShot, std::move(callback), 0, initial_delay_ms);

  timers_.emplace_back(std::weak_ptr<TimerImpl>(timer));
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

void Node::AddSignalHandler(SignalHandler handler) { user_handler_ = handler; }

void Node::UpdateSimulatedClock(const time::TimePoint& new_time) {
  if (time::IsSimulatedClockEnabled()) {
    asio::post(*ev_loop_, [this, new_time]() {
      auto existing_time = time::Now();
      bool reset_timers{false};
      if (new_time >= existing_time) {
        if (!timers_.empty()) {
          if (time::TimePointToMilliseconds(existing_time) != 0) {
            // Use a priority queue to store the timers we need to fire in order of nearest expiration
            auto timer_comp = [](const Timer& a, const Timer& b) { return a->GetExpiry() > b->GetExpiry(); };
            std::priority_queue<Timer, std::vector<Timer>, decltype(timer_comp)> expired_timers(timer_comp);

            // First find all the non-cancelled timers that are expiring before our new_time
            for (auto& timer : timers_) {
              if (auto shared_timer = timer.lock()) {
                if (!shared_timer->IsCancelled() && new_time >= shared_timer->GetExpiry()) {
                  expired_timers.push(shared_timer);
                }
              }
            }

            // Step forward in time while firing the timers that are expiring until there are no more timers to fire
            while (!expired_timers.empty()) {
              auto top = expired_timers.top();
              expired_timers.pop();
              // Move our simulated time up to the expiration time of this timer
              time::SetSimulatedTime(top->GetExpiry());
              top->Fire();  // Fire the timer (which updates the expiry time also)

              // If our expiry time is still earlier than our new_time, put it back in the queue for another go
              if (new_time >= top->GetExpiry() && top->GetType() != TimerImpl::Type::kOneShot) {
                expired_timers.push(top);
              }
            }
          } else {
            // This is our first jump forward in time, reset all the timers so their expiry times are sane
            reset_timers = true;
          }
        }
        time::SetSimulatedTime(new_time);
        // If we need to reset timers, it needs to happen after the new time is updated
        if (reset_timers) {
          for (auto& timer : timers_) {
            if (auto shared_timer = timer.lock()) {
              shared_timer->Reset();
            }
          }
        }
      } else {
        Log::Debug("Ignored attempt to rewind simulated clock. Current time {} Set time {}",
                   time::TimePointToSeconds(existing_time), time::TimePointToSeconds(new_time));
      }
    });
  }
}
