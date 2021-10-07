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

#include <sstream>

#include "trellis/core/logging.hpp"
#include "trellis/core/node.hpp"
#include "trellis/examples/hello_world.pb.h"

using namespace trellis::core;

void HelloWorldCallback(const trellis::examples::HelloWorld& hello_world_msg) {
  std::stringstream msg;
  msg << hello_world_msg.name() << " sent a message with ID " << hello_world_msg.id() << ":" << std::endl
      << hello_world_msg.msg() << std::endl;
  Log::Info(msg.str());
}

int main(int argc, char** argv) {
  Node node("Hello World Protobuf Subscriber");
  auto config = LoadFromFile("trellis/examples/config.yml");
  const std::string topic_name = config["examples"]["pubsub_topic"].as<std::string>();
  auto subscriber = node.CreateSubscriber<trellis::examples::HelloWorld>(topic_name, HelloWorldCallback);

  return node.Run();
}
