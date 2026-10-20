/*
 * Copyright (C) 2026 Agtonomy
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

#include "trellis/core/simulated_timers.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>

#include "trellis/core/timer.hpp"

namespace trellis::core {

namespace {

// The simulation-driven timer due soonest by target, or nullopt if none is. Handles are monotonic, so breaking ties on
// them picks timers sharing an expiry in creation order, whatever order the registry's map returns its entries in.
std::optional<TimerRegistry::Entry> NextDueTimer(const TimerRegistry& registry, const time::TimePoint& target) {
  std::optional<TimerRegistry::Entry> next;
  for (const auto& entry : registry.GetEntries()) {
    // Expired() covers a one-shot that has fired and any timer that was stopped; a periodic timer clears it on every
    // rearm.
    const auto* const timer = entry.timer;
    if (!timer->IsSimulationDriven() || timer->Expired() || timer->GetExpiry() > target) {
      continue;
    }
    if (!next.has_value() ||
        std::tuple{timer->GetExpiry(), entry.handle} < std::tuple{next->timer->GetExpiry(), next->handle}) {
      next = entry;
    }
  }
  return next;
}

// Move the simulated clock to time, or leave it where it is if it is already past time. Simulated time only moves
// forwards, and the clock can be ahead of a step: a callback may have stepped it further, or a step of another
// registry may have moved it, the clock being process-global.
void AdvanceClockTo(const time::TimePoint& time) { time::SetSimulatedTime(std::max(time, time::Now())); }

}  // namespace

std::size_t StepSimulatedTimers(const TimerRegistry& registry, const time::TimePoint& target, std::size_t max_fires) {
  if (!time::IsSimulatedClockEnabled()) {
    throw std::logic_error("StepSimulatedTimers requires the simulated clock to be enabled");
  }
  if (target < time::Now()) {
    throw std::invalid_argument("StepSimulatedTimers cannot move the simulated clock backwards");
  }

  // How far this step reaches: target, or further if a callback has stepped the clock past it, since whatever is due by
  // the time the clock shows is due. The timer whose callback took the clock there was mid-fire, so that inner step
  // could not fire it; this is what still catches its later slots up.
  const auto horizon = [&target]() { return std::max(target, time::Now()); };

  std::size_t fired{0};
  // Searched afresh after every fire: the callback just run may have created, reset, stopped or destroyed timers.
  while (const auto next = NextDueTimer(registry, horizon())) {
    if (fired == max_fires) {
      throw std::runtime_error("StepSimulatedTimers fired " + std::to_string(max_fires) +
                               " timers without reaching its target");
    }
    // Passing the horizon rather than the expiry is deliberate. Advancing to each expiry in turn means no timer is
    // ever dispatched late here, so a caller stepping well past a timer's expiry is treated as that timer having
    // fallen behind. That is what lets a rearm policy mean the same thing in a replay as it does on a real clock.
    // The one exception is an expiry the clock has already passed: that timer fires late, at Now().
    AdvanceClockTo(next->timer->GetExpiry());
    next->timer->Fire(horizon());
    ++fired;
  }
  AdvanceClockTo(target);
  return fired;
}

}  // namespace trellis::core
