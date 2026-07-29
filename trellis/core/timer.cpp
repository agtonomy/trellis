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

#include "trellis/core/timer.hpp"

#include <stdexcept>

namespace trellis {
namespace core {

// =============================================================================
// TimerImpl (base class)
// =============================================================================

TimerImpl::TimerImpl(EventLoop loop, Callback callback, unsigned interval_ms, unsigned delay_ms, TimerKind kind)
    : loop_{loop},
      callback_{std::move(callback)},
      interval_ms_{interval_ms},
      delay_ms_(delay_ms),
      kind_{kind},
      timer_{CreateSteadyTimer(loop, delay_ms, kind)} {}

TimerImpl::~TimerImpl() { DeregisterFromLoop(); }

void TimerImpl::RegisterWithLoop() {
  if (const auto registry = loop_.GetTimerRegistry(); registry != nullptr) {
    registration_ = registry->Add(this, &(*loop_), kind_);
  }
}

void TimerImpl::DeregisterFromLoop() {
  if (const auto registry = loop_.GetTimerRegistry(); registry != nullptr) {
    registry->Remove(registration_);
  }
  registration_ = TimerRegistry::kInvalidRegistrationHandle;
}

void TimerImpl::Reset() {
  Stop();
  Reload();
  KickOff();
}

void TimerImpl::Stop() {
  cancelled_ = true;
  if (!IsSimulationDriven()) {
    timer_->cancel();
  }
}

bool TimerImpl::Expired() const { return did_fire_.load() || cancelled_.load(); }

void TimerImpl::KickOff() {
  if (!IsSimulationDriven()) {
    timer_->async_wait([this](const trellis::core::error_code& e) {
      if (e) {
        return;
      }
      Fire();
    });
  }
}

void TimerImpl::Fire() {
  if (!ShouldFire()) {
    return;
  }
  const auto fire_time = time::Now();
  last_fire_time_ = fire_time;  // used for sim time
  did_fire_ = true;
  callback_(fire_time);
  if (!cancelled_) {
    OnFired(fire_time);
  }
}

std::unique_ptr<asio::steady_timer> TimerImpl::CreateSteadyTimer(EventLoop loop, unsigned delay_ms, TimerKind kind) {
  // A management timer services the process rather than the data being replayed, so it keeps running on wall time when
  // a simulated clock is active. An application timer under a simulated clock needs no underlying timer at all:
  // advancing the clock is what fires it.
  if (time::IsSimulatedClockEnabled() && kind != TimerKind::kManagement) {
    return nullptr;
  }
  return std::make_unique<asio::steady_timer>(*loop, asio::chrono::milliseconds(delay_ms));
}

std::chrono::milliseconds TimerImpl::GetTimeInterval() const { return std::chrono::milliseconds(interval_ms_); }

// =============================================================================
// OneShotTimerImpl
// =============================================================================

OneShotTimerImpl::OneShotTimerImpl(EventLoop loop, Callback callback, unsigned delay_ms, TimerKind kind)
    : TimerImpl(loop, std::move(callback), 0, delay_ms, kind) {
  RegisterWithLoop();
  KickOff();
}

OneShotTimerImpl::~OneShotTimerImpl() { DeregisterFromLoop(); }

void OneShotTimerImpl::Reload() {
  if (!IsSimulationDriven()) {
    // If we're reloading a one shot timer we simply reload to now + our delay time
    timer_->expires_after(asio::chrono::milliseconds(delay_ms_));
  } else {
    last_fire_time_ = time::Now();  // this essentially pushes out the expiry time
  }
  did_fire_ = false;
  cancelled_ = false;
}

bool OneShotTimerImpl::ShouldFire() const { return !did_fire_; }

time::TimePoint OneShotTimerImpl::GetExpiry() const {
  if (IsSimulationDriven()) {
    return last_fire_time_ + std::chrono::milliseconds(delay_ms_);
  } else {
    return timer_->expiry();
  }
}

// =============================================================================
// PeriodicTimerImpl
// =============================================================================

PeriodicTimerImpl::PeriodicTimerImpl(EventLoop loop, Callback callback, unsigned interval_ms, unsigned delay_ms,
                                     TimerKind kind)
    : TimerImpl(loop, std::move(callback), interval_ms, delay_ms, kind) {
  RegisterWithLoop();
  KickOff();
}

PeriodicTimerImpl::PeriodicTimerImpl(EventLoop loop, unsigned interval_ms, Callback callback, unsigned delay_ms,
                                     TimerKind kind)
    : PeriodicTimerImpl(loop, std::move(callback), interval_ms, delay_ms, kind) {};

PeriodicTimerImpl::~PeriodicTimerImpl() { DeregisterFromLoop(); }

void PeriodicTimerImpl::Reload() {
  if (!IsSimulationDriven()) {
    // We calculate the new expiration time based on the last expiration
    // rather than "now" in order to avoid drift due to jitter error
    timer_->expires_at(timer_->expiry() + asio::chrono::milliseconds(interval_ms_));
  } else {
    last_fire_time_ = time::Now();  // this essentially pushes out the expiry time
  }
  did_fire_ = false;
  cancelled_ = false;
}

void PeriodicTimerImpl::OnFired(const time::TimePoint& fire_time) {
  // A wall-clock timer under a simulated clock takes its expiry from the steady clock while fire_time comes from the
  // simulated one. Subtracting across those domains yields a meaningless number, so skip the accounting rather than
  // accumulate garbage into stats a caller may read.
  const bool comparable_clocks = IsSimulationDriven() || !time::IsSimulatedClockEnabled();
  if (comparable_clocks) {
    const auto expected = GetExpiry();
    const auto latency_us = std::chrono::duration_cast<std::chrono::microseconds>(fire_time - expected).count();
    if (latency_us > max_sched_latency_us_) {
      max_sched_latency_us_ = latency_us;
    }
    total_sched_latency_us_ += latency_us;
    ++sched_latency_count_;

    const auto next_expected_expiry = expected + std::chrono::milliseconds(interval_ms_);
    if (fire_time > next_expected_expiry) {
      ++overrun_count_;
    }
  }
  Reload();
  KickOff();
}

time::TimePoint PeriodicTimerImpl::GetExpiry() const {
  if (IsSimulationDriven()) {
    return last_fire_time_ + std::chrono::milliseconds(interval_ms_);
  } else {
    return timer_->expiry();
  }
}

PeriodicTimerImpl::SchedLatencyStats PeriodicTimerImpl::GetAndResetSchedLatencyStats() {
  // Either this thread is the one firing the timer, or nothing is firing it. Anything else races the accumulators below
  // against OnFired, and the numbers it would return are unusable anyway.
  if (!loop_.Stopped() && !(*loop_).get_executor().running_in_this_thread()) {
    throw std::runtime_error("Attempt to collect timer scheduling latency from a thread not running the timer's loop");
  }
  SchedLatencyStats stats{
      .max_us = max_sched_latency_us_,
      .total_us = total_sched_latency_us_,
      .mean_us = sched_latency_count_ > 0
                     ? static_cast<double>(total_sched_latency_us_) / static_cast<double>(sched_latency_count_)
                     : 0.0,
      .count = sched_latency_count_,
  };
  max_sched_latency_us_ = 0;
  total_sched_latency_us_ = 0;
  sched_latency_count_ = 0;
  return stats;
}

}  // namespace core
}  // namespace trellis
