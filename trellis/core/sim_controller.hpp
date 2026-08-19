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

#ifndef TRELLIS_CORE_SIM_CONTROLLER_HPP_
#define TRELLIS_CORE_SIM_CONTROLLER_HPP_

#include <cstdint>
#include <string_view>

#include "trellis/core/sim_clock.pb.h"
#include "trellis/core/subscriber.hpp"
#include "trellis/core/time.hpp"

namespace trellis::core {

class Node;

/**
 * @brief Subscribes a node to the simulated clock topic and tracks the simulator-owned epoch.
 *
 * Constructed by Node when the trellis.simulated_clock.enabled config flag is set. It subscribes to the
 * configured clock topic so the node receives SimClock updates from the simulation engine.
 *
 * The clock is advanced by the subscriber's built-in trigger, which moves simulated time to each
 * message's send_time -- the same single mechanism every other topic uses. Node::BroadcastSimulatedClock
 * guarantees send_time carries the authoritative simulated time. This controller therefore does not move
 * the clock itself; it only caches the current epoch (held one layer above the epoch-agnostic
 * Node::UpdateSimulatedClock) and monitors incoming updates for anomalies.
 */
class SimController {
 public:
  static constexpr std::string_view kDefaultClockTopic = "/trellis/sim/clock";

  /**
   * @param node the owning node; used to create the clock subscriber.
   * @param clock_topic the topic on which the simulator publishes SimClock messages.
   */
  SimController(Node& node, std::string_view clock_topic);

  /// @return the most recent epoch received from the simulator (0 before the first SimClock).
  uint64_t GetCurrentEpoch() const { return current_epoch_; }

 private:
  void OnClock(const time::TimePoint& send_time, const SimClock& clock);

  Node& node_;
  uint64_t current_epoch_{0};
  Subscriber<SimClock> clock_subscriber_;
};

}  // namespace trellis::core

#endif  // TRELLIS_CORE_SIM_CONTROLLER_HPP_
