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
      timer_{CreateSteadyTimer(loop, delay_ms, kind)} {
  // An asio-driven timer already carries the deadline CreateSteadyTimer gave it, so take it from there. A
  // simulation-driven timer has no such deadline and is armed separately by ArmInitialExpiry().
  if (timer_ != nullptr) {
    next_expiry_ = timer_->expiry();
  }
}

void TimerImpl::ArmInitialExpiry() {
  if (!IsSimulationDriven()) {
    return;
  }
  // Deliberately asymmetric with the asio case, which starts at its initial delay and so is usually due
  // immediately. A simulation-driven timer only moves when the clock is advanced explicitly, and starting it
  // due would make anything constructed while the clock was already running fire on the very next advance no
  // matter what wait it asked for. RestartDelayMs() is that wait -- the delay for a one shot, the interval for
  // a periodic timer -- which is also why this cannot be done from the constructor above.
  next_expiry_ = time::Now() + std::chrono::milliseconds(RestartDelayMs());
}

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

time::TimePoint TimerImpl::NowInOwnDomain() const {
  return ClockDomainsAgree() ? time::Now() : asio::steady_timer::clock_type::now();
}

void TimerImpl::Reset() {
  Stop();
  SetNextExpiry(NowInOwnDomain() + std::chrono::milliseconds(RestartDelayMs()));
  ClearFireState();
  KickOff();
}

void TimerImpl::SetNextExpiry(const time::TimePoint& expiry) {
  next_expiry_ = expiry;
  // A null check rather than !IsSimulationDriven(): here it guards the dereference, where the places asking
  // IsSimulationDriven() are asking which clock they are on
  if (timer_ != nullptr) {
    timer_->expires_at(next_expiry_);  // mirror the deadline onto whatever will wake us
  }
}

void TimerImpl::Stop() {
  cancelled_ = true;
  if (timer_ != nullptr) {
    timer_->cancel();
  }
}

bool TimerImpl::Expired() const { return did_fire_.load() || cancelled_.load(); }

void TimerImpl::KickOff() {
  if (timer_ != nullptr) {
    timer_->async_wait([this](const trellis::core::error_code& e) {
      if (e) {
        return;
      }
      Fire();
    });
  }
}

void TimerImpl::Fire(const time::TimePoint& horizon) {
  if (!ShouldFire()) {
    return;
  }
  // fire_time is the moment this callback belongs to; horizon is how far the caller is advancing overall. They
  // are the same reading whenever a real clock drives the timer, and differ only while
  // Node::UpdateSimulatedClock is stepping: it moves the clock to this timer's own expiry before firing, so the
  // callback sees the slot it was scheduled for while the rearm still learns how many later slots the step
  // passed over. Only RearmPolicy::kSkipAligned reads the horizon; everything else ignores it.
  const auto fire_time = time::Now();
  did_fire_ = true;
  callback_(fire_time);
  if (!cancelled_) {
    OnFired(fire_time, horizon);
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
  ArmInitialExpiry();
  RegisterWithLoop();
  KickOff();
}

OneShotTimerImpl::~OneShotTimerImpl() { DeregisterFromLoop(); }

bool OneShotTimerImpl::ShouldFire() const { return !did_fire_; }

// =============================================================================
// PeriodicTimerImpl
// =============================================================================

PeriodicTimerImpl::PeriodicTimerImpl(EventLoop loop, Callback callback, unsigned interval_ms, unsigned delay_ms,
                                     TimerKind kind, std::optional<RearmPolicy> rearm_policy)
    : TimerImpl(loop, std::move(callback), interval_ms, delay_ms, kind),
      rearm_policy_{rearm_policy.value_or(loop.GetTimerOptions().rearm_policy)} {
  // Rejected rather than clamped: every rearm advances by a whole interval, so a zero interval leaves the
  // expiry where it is and the timer either spins on a passed deadline or wedges the simulated clock's walk
  if (interval_ms == 0) {
    throw std::invalid_argument("Periodic timer interval must be greater than zero");
  }
  ArmInitialExpiry();
  RegisterWithLoop();
  KickOff();
}

PeriodicTimerImpl::PeriodicTimerImpl(EventLoop loop, unsigned interval_ms, Callback callback, unsigned delay_ms,
                                     TimerKind kind, std::optional<RearmPolicy> rearm_policy)
    : PeriodicTimerImpl(loop, std::move(callback), interval_ms, delay_ms, kind, rearm_policy) {};

PeriodicTimerImpl::~PeriodicTimerImpl() { DeregisterFromLoop(); }

void PeriodicTimerImpl::Reload(const time::TimePoint& horizon) {
  const auto interval = std::chrono::milliseconds(interval_ms_);
  const auto previous_expiry = GetExpiry();
  // The caller's horizon is a time::Now() reading, which is this timer's own clock only while the two agree.
  // When they do not, how far there is still to travel can only be measured on the clock this timer runs on.
  const auto effective_horizon = ClockDomainsAgree() ? horizon : NowInOwnDomain();
  const auto slots_behind = effective_horizon > previous_expiry ? (effective_horizon - previous_expiry) / interval : 0;

  // Advance from the previous expiry rather than from now, so a timer dispatched with jitter keeps its
  // phase instead of drifting
  auto next = previous_expiry + interval;
  if (rearm_policy_ == RearmPolicy::kSkipAligned && next <= effective_horizon) {
    // Land on the first slot still ahead of us, staying on multiples of the interval so the timer keeps
    // its phase. Computed rather than stepped: a multi-second stall on a fast timer would otherwise take
    // thousands of iterations to walk forward.
    next = previous_expiry + (slots_behind + 1) * interval;
  }

  // kSkipAligned drops every slot it passed over, so it accounts for all of them here. kCatchUp replays them
  // instead, so this dispatch accounts only for itself and the rest of the burst accounts for the others,
  // which adds up to the same total. The two have to agree: the count leaves the process as a fleet metric,
  // where a figure that meant something different per policy could not be compared between two apps.
  slots_accounted_ =
      rearm_policy_ == RearmPolicy::kSkipAligned ? static_cast<uint64_t>(slots_behind) : (slots_behind >= 1 ? 1u : 0u);
  SetNextExpiry(next);
  ClearFireState();
}

void PeriodicTimerImpl::OnFired(const time::TimePoint& fire_time, const time::TimePoint& horizon) {
  // fire_time comes from time::Now(), so it is comparable with this timer's expiry only while the two clocks
  // agree. Subtracting across domains yields a meaningless number, so skip the accounting rather than
  // accumulate garbage into stats a caller may read.
  if (ClockDomainsAgree()) {
    const auto latency_us = std::chrono::duration_cast<std::chrono::microseconds>(fire_time - GetExpiry()).count();
    if (latency_us > max_sched_latency_us_) {
      max_sched_latency_us_ = latency_us;
    }
    total_sched_latency_us_ += latency_us;
    ++sched_latency_count_;
  }
  Reload(horizon);
  // Reload works out how many slots this dispatch is answering for, and does so on a clock it can trust, so
  // unlike the latency figures above this needs no guard
  overrun_count_ += slots_accounted_;
  KickOff();
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
