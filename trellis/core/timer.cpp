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

#include "timer.hpp"

namespace trellis {
namespace core {

TimerImpl::TimerImpl(EventLoop loop, Type type, Callback callback, unsigned interval_ms, unsigned delay_ms)
    : loop_{loop},
      type_{type},
      callback_{callback},
      interval_ms_{interval_ms},
      delay_ms_(delay_ms),
      timer_{CreateSteadyTimer(loop, delay_ms)} {
  KickOff();
}

void TimerImpl::Reset() {
  Stop();
  Reload();
  KickOff();
}

void TimerImpl::Stop() {
  if (!SimulationActive()) {
    cancelled_ = true;
    timer_->cancel();
  }
}

bool TimerImpl::Expired() const { return (type_ == kOneShot) ? (did_fire_.load() || cancelled_.load()) : false; }

void TimerImpl::KickOff() {
  if (!SimulationActive()) {
    timer_->async_wait([this](const trellis::core::error_code& e) {
      if (e) {
        return;
      }
      Fire();
    });
  }
}

void TimerImpl::Reload() {
  if (!SimulationActive()) {
    if (type_ == kPeriodic) {
      // We calculate the new expiration time based on the last expiration
      // rather than "now" in order to avoid drift due to jitter error
      timer_->expires_at(timer_->expiry() + asio::chrono::milliseconds(interval_ms_));
    } else if (type_ == kOneShot) {
      // If we're reloading a one shot timer we simply reload to now + our delay time
      timer_->expires_after(asio::chrono::milliseconds(delay_ms_));
    }
  } else {
    last_fire_time_ = time::Now();  // this essentially pushes out the expiry time
  }
  did_fire_ = false;
  cancelled_ = false;
}

void TimerImpl::Fire() {
  if (did_fire_ && type_ == kOneShot) {
    return;
  }
  last_fire_time_ = time::Now();  // used for sim time
  did_fire_ = true;
  callback_();
  if (type_ != kOneShot) {
    Reload();
    KickOff();
  }
}

std::unique_ptr<asio::steady_timer> TimerImpl::CreateSteadyTimer(EventLoop loop, unsigned delay_ms) {
  if (time::IsSimulatedClockEnabled()) {
    return nullptr;  // we don't need an underlying timer during simulated time
  } else {
    return std::make_unique<asio::steady_timer>(*loop, asio::chrono::milliseconds(delay_ms));
  }
}

bool TimerImpl::SimulationActive() const {
  return (timer_ == nullptr);  // just use existence of timer_ as a proxy
}

time::TimePoint TimerImpl::GetExpiry() const {
  if (SimulationActive()) {
    auto interval = (type_ == kPeriodic) ? interval_ms_ : delay_ms_;
    return last_fire_time_ + std::chrono::milliseconds(interval);
  } else {
    return timer_->expiry();
  }
}

std::chrono::milliseconds TimerImpl::GetTimeInterval() const { return std::chrono::milliseconds(interval_ms_); }

}  // namespace core
}  // namespace trellis
