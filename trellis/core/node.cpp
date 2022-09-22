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

#include "node.hpp"

#include <queue>
#include <thread>

using namespace trellis::core;

Node::Node(std::string name, trellis::core::Config config)
    : name_{name},
      config_{config},
      ev_loop_{CreateEventLoop()},
      work_guard_{asio::make_work_guard(*ev_loop_)},
      signal_set_(*ev_loop_, SIGTERM, SIGINT),
      health_{name, config,
              [this](const std::string& topic) { return CreatePublisher<trellis::core::HealthHistory>(topic); },
              [this](unsigned interval_ms, trellis::core::TimerImpl::Callback cb) {
                return CreateTimer(interval_ms, cb);
              }} {
  // XXX(bsirang) eCAL can take argv/argc to parse options for overriding the config filepath and/or specific config
  // options. We won't make use of that for now. We'll just call Initialize with default arguments.
  // eCAL::Initialize();
  eCAL::Initialize(0, nullptr, nullptr, eCAL::Init::All);

  // Instead of passing the unit name as part of Initialize above, we'll set it here explicitly. This ensures the unit
  // name gets set in cases where we may have called Initialize already, such as from logging APIs.
  eCAL::SetUnitName(name_.c_str());

  // Handle signals explicitly, allowing the user to supply their own handler
  signal_set_.async_wait([this](const trellis::core::error_code& error, int signal_number) {
    if (!error) {
      if (user_handler_) user_handler_(signal_number);
      Log::Debug("{} node stopping...", name_);
      Stop();
    }
  });
}

Node::~Node() { Stop(); }

int Node::Run() {
  Log::Debug("{} node running...", name_);
  while (ShouldRun()) {
    ev_loop_->run_for(std::chrono::milliseconds(500));
    // If the event loop was stopped, run_for will return immediately, so
    // we should sleep in that case.
    if (ev_loop_->stopped()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
  }
  Stop();
  return 0;
}

bool Node::RunN(unsigned n) {
  unsigned count = 0;
  bool ok;
  // poll_one will return immediately (never block). If it returned 0 there's
  // nothing to do right now and so we'll just drop out of the loop, otherwise we keep
  // polling so long as work is being done
  while ((ok = ShouldRun()) && ev_loop_->poll_one() && count++ < n)
    ;
  return ok;
}

Timer Node::CreateTimer(unsigned interval_ms, TimerImpl::Callback callback, unsigned initial_delay_ms) {
  auto timer =
      std::make_shared<TimerImpl>(GetEventLoop(), TimerImpl::Type::kPeriodic, callback, interval_ms, initial_delay_ms);

  timers_.push_back(timer);
  return timer;
}

Timer Node::CreateOneShotTimer(unsigned initial_delay_ms, TimerImpl::Callback callback) {
  auto timer = std::make_shared<TimerImpl>(GetEventLoop(), TimerImpl::Type::kOneShot, callback, 0, initial_delay_ms);

  timers_.push_back(timer);
  return timer;
}

void Node::Stop() {
  should_run_ = false;
  ev_loop_->stop();
  eCAL::Finalize();
}

void Node::UpdateHealth(trellis::core::HealthState state, Health::Code code, const std::string& description) {
  health_.Update(state, code, description);
}

trellis::core::HealthState Node::GetHealthState() const { return health_.GetHealthState(); }

const trellis::core::HealthStatus& Node::GetLastHealthStatus() const { return health_.GetLastHealthStatus(); }

bool Node::ShouldRun() const { return should_run_ && eCAL::Ok(); }

void Node::AddSignalHandler(SignalHandler handler) { user_handler_ = handler; }

void Node::UpdateSimulatedClock(const time::TimePoint& new_time) {
  if (time::IsSimulatedClockEnabled()) {
    asio::post(*ev_loop_, [this, new_time]() {
      auto existing_time = time::Now();
      bool reset_timers{false};
      if (new_time > existing_time) {
        if (timers_.size() > 0) {
          if (time::TimePointToMilliseconds(existing_time) != 0) {
            // Use a priority queue to store the timers we need to fire in order of nearest expiration
            auto timer_comp = [](const Timer& a, const Timer& b) { return a->GetExpiry() > b->GetExpiry(); };
            std::priority_queue<Timer, std::vector<Timer>, decltype(timer_comp)> expired_timers(timer_comp);

            // First find all the timers that are expiring before our new_time
            for (auto& timer : timers_) {
              if (new_time >= timer->GetExpiry()) {
                expired_timers.push(timer);
              }
            }

            // Step forward in time while firing the timers that are expiring until there are no more timers to fire
            while (expired_timers.size()) {
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
            timer->Reset();
          }
        }
      } else {
        Log::Warn("Ignored attempt to rewind simulated clock. Current time {} Set time {}",
                  time::TimePointToSeconds(existing_time), time::TimePointToSeconds(new_time));
      }
    });
  }
}
