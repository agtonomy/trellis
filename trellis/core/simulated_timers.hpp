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

#ifndef TRELLIS_CORE_SIMULATED_TIMERS_HPP_
#define TRELLIS_CORE_SIMULATED_TIMERS_HPP_

#include <cstddef>

#include "trellis/core/time.hpp"
#include "trellis/core/timer_registry.hpp"

namespace trellis::core {

/**
 * StepSimulatedTimers advance the simulated clock to target, firing every simulation-driven timer in registry that
 * falls due by then
 *
 * Timers fire in expiry order, and timers sharing an expiry fire in creation order. The clock moves to each timer's
 * expiry before it fires, so its callback sees the moment it was scheduled for, and is left at target. The registry is
 * searched again after every fire, so a timer that a callback creates or resets fires in this same step if it falls
 * due by target.
 *
 * Timers asio drives are skipped: their expiry is a steady clock reading, not comparable with simulated time. See
 * TimerImpl::IsSimulationDriven.
 *
 * Callbacks run on the calling thread, so call this from the thread that runs the timers' loop, or while nothing
 * runs it.
 *
 * @param registry the timers to step
 * @param target the time to advance to
 * @param max_fires safety bound against timers that keep falling due without the clock moving, such as a zero-delay
 *        one-shot that resets itself from its own callback
 * @return the number of timers fired
 * @throws std::logic_error if the simulated clock is not enabled
 * @throws std::invalid_argument if target is before time::Now()
 * @throws std::runtime_error if more than max_fires timers fall due; the clock is left at the last one fired
 */
std::size_t StepSimulatedTimers(const TimerRegistry& registry, const time::TimePoint& target,
                                std::size_t max_fires = 1'000'000);

}  // namespace trellis::core

#endif  // TRELLIS_CORE_SIMULATED_TIMERS_HPP_
