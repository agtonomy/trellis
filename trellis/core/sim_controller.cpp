/*
 * Copyright (C) 2025 Agtonomy
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

#include "trellis/core/sim_controller.hpp"

#include <memory>
#include <string>

#include "trellis/core/logging.hpp"
#include "trellis/core/node.hpp"
#include "trellis/core/time.hpp"

namespace trellis::core {

SimController::SimController(Node& node, std::string_view clock_topic) : node_{node} {
  // Use the normal subscriber path. Its built-in trigger advances simulated time to each message's
  // send_time -- the single mechanism every topic uses -- so this controller does not call
  // UpdateSimulatedClock itself. The contract that send_time carries the authoritative simulated time is
  // guaranteed by the publish side (Node::BroadcastSimulatedClock). OnClock only caches the epoch and
  // monitors for anomalies.
  //
  // ORDERING NOTE: the subscriber advances the clock (via update_sim_fn_) BEFORE invoking this callback
  // (see SubscriberImpl::ReceiveData in subscriber.hpp), so the epoch is cached AFTER the clock has reached
  // the SimClock's send_time. That is intentional and safe: nothing reads current_epoch_ mid-step. Its only
  // consumer is the periodic SimNodeReport (a later phase), which reads it after a clock message is fully
  // processed, by which point both the clock and the cached epoch reflect the same message. Timers fired
  // during the step briefly observe the previous epoch, but no code depends on the epoch during a step.
  clock_subscriber_ = node_.CreateSubscriber<SimClock>(
      std::string{clock_topic}, [this](const time::TimePoint&, const time::TimePoint& send_time,
                                       std::unique_ptr<SimClock> msg) { OnClock(send_time, *msg); });
}

void SimController::OnClock(const time::TimePoint& send_time, const SimClock& clock) {
  if (clock.epoch() < current_epoch_) {
    Log::Warn("SimClock epoch went backwards: received {} after {}", clock.epoch(), current_epoch_);
  }

  // send_time is what actually drives the clock; target_time is advisory. A mismatch indicates a bug in
  // the publish side, since BroadcastSimulatedClock is supposed to send the two as the same value.
  const auto target_time = time::TimePointFromTimestamp(clock.target_time());
  if (target_time != send_time) {
    Log::Warn("SimClock send_time ({:.6f}) does not match target_time ({:.6f}); time follows send_time",
              time::TimePointToSeconds(send_time), time::TimePointToSeconds(target_time));
  }

  current_epoch_ = clock.epoch();
}

}  // namespace trellis::core
