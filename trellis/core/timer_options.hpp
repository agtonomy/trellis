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

#ifndef TRELLIS_CORE_TIMER_OPTIONS_HPP_
#define TRELLIS_CORE_TIMER_OPTIONS_HPP_

namespace trellis::core {

/**
 * RearmPolicy decides where a periodic timer's next expiry lands when it was dispatched late
 *
 * kCatchUp advances one interval from the previous expiry. If the timer is more than one interval
 * behind, that expiry is still in the past, so it fires again immediately and walks forward a slot at
 * a time until it catches up: every missed slot is replayed back-to-back, at the moment the process is
 * already behind.
 *
 * kSkipAligned advances to the first expiry still in the future while staying on the original grid --
 * the smallest previous_expiry + k * interval greater than now. Missed slots are dropped rather than
 * replayed, and because the new expiry is always a whole number of intervals from the old one, the
 * timer keeps its phase and does not drift.
 *
 * A timer whose callback must run once per interval -- anything integrating over slots, counting ticks
 * to derive elapsed time, or advancing a state machine one step per invocation -- needs kCatchUp.
 * Anything that publishes current state, polls a device, or recomputes from scratch is better served
 * by kSkipAligned.
 *
 * Both policies apply under a simulated clock as well, where "behind" means something slightly different.
 * A simulated timer is stepped by Node::UpdateSimulatedClock, which advances the clock to each expiry in
 * turn, so it is never dispatched late in the way a real one is. What stands in for lateness there is the
 * distance still to travel: a caller advancing the clock well past a timer's expiry is treated as a timer
 * that has fallen that far behind. kCatchUp therefore replays every slot the advance passed over, and
 * kSkipAligned fires once and lands on the first slot beyond it. Choosing kSkipAligned consequently changes
 * what a replay produces, which is the point -- a long gap in the data being replayed no longer costs a
 * backlog of callbacks working from inputs that never arrived.
 */
enum class RearmPolicy { kCatchUp = 0, kSkipAligned };

/**
 * TimerOptions is the timer policy carried by an event loop, applying to every timer constructed
 * against it
 *
 * Deliberately an aggregate with no user-declared constructors, so designated initializers work:
 * TimerOptions{.rearm_policy = RearmPolicy::kSkipAligned}. Every default reproduces the behavior
 * timers had before the option existed.
 *
 * Parsing this out of configuration lives in timer_options_config.hpp rather than here, so that a loop
 * carrying options does not have to depend on the configuration library.
 */
struct TimerOptions {
  RearmPolicy rearm_policy{RearmPolicy::kCatchUp};
};

}  // namespace trellis::core

#endif  // TRELLIS_CORE_TIMER_OPTIONS_HPP_
