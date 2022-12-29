/*
 * Copyright (C) 2022 Agtonomy
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

#include "app.hpp"

namespace trellis {
namespace examples {
namespace subscriber {

App::App(trellis::core::Node& node)
    : node_{node},
      inputs_{node,
              {{node.GetConfig()["examples"]["publisher"]["topic"].as<std::string>()}},
              [this](const std::string& topic, const trellis::examples::proto::HelloWorld& msg,
                     const trellis::core::time::TimePoint& now,
                     const trellis::core::time::TimePoint& msgtime) { NewMessage(topic, msg, now, msgtime); },
              {{2000U}},
              {{[this](const std::string&, const trellis::core::time::TimePoint&) {
                trellis::core::Log::Warn("Watchdog tripped on inbound messages!");
                node_.UpdateHealth(trellis::core::HealthState::HEALTH_STATE_CRITICAL, 0x01, "Inputs timed out");
              }}}} {}

void App::NewMessage(const std::string& topic, const trellis::examples::proto::HelloWorld& msg,
                     const trellis::core::time::TimePoint& now, const trellis::core::time::TimePoint& msgtime) {
  node_.UpdateHealth(trellis::core::HealthState::HEALTH_STATE_NORMAL);
  ++receive_count_;
  trellis::core::Log::Info("Receive count {} on topic {} with message number {} (now = {} msgtime = {})",
                           receive_count_, topic, msg.id(), trellis::core::time::TimePointToSeconds(now),
                           trellis::core::time::TimePointToSeconds(msgtime));
}

}  // namespace subscriber
}  // namespace examples
}  // namespace trellis
