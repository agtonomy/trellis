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

#include "trellis/examples/publisher/app.hpp"

namespace trellis {
namespace examples {
namespace publisher {

using namespace trellis::core;

App::App(Node& node)
    : publisher_{node.CreatePublisher<trellis::examples::proto::HelloWorld>(
          node.GetConfig()["examples"]["publisher"]["topic"].as<std::string>())},
      timer_{node.CreateTimer(
          node.GetConfig()["examples"]["publisher"]["interval_ms"].as<unsigned>(),
          [this](const trellis::core::time::TimePoint&) { Tick(); },
          node.GetConfig()["examples"]["publisher"]["initial_delay_ms"].as<unsigned>())},
      repeated_field_len_{node.GetConfig()["examples"]["publisher"]["repeated_field_len"].as<unsigned>()} {
  Log::Debug("Starting publisher example");
}

void App::Tick() {
  const unsigned msg_number = send_count_++;
  Log::Info("Publishing message number {}", msg_number);

  trellis::examples::proto::HelloWorld msg;
  msg.set_id(msg_number);
  for (size_t i = 0; i < repeated_field_len_; ++i) {
    msg.add_floats(1.0 * i);
    msg.add_ints(i);
  }
  publisher_->Send(msg);
}

}  // namespace publisher
}  // namespace examples
}  // namespace trellis
