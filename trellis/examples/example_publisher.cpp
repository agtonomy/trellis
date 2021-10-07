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

#include "trellis/core/logging.hpp"
#include "trellis/core/node.hpp"
#include "trellis/examples/hello_world.pb.h"

using namespace trellis::core;

int main(int argc, char** argv) {
  Node node("Hello World Protobuf Publisher");
  auto config = LoadFromFile("trellis/examples/config.yml");
  const std::string topic_name = config["examples"]["pubsub_topic"].as<std::string>();
  auto publisher = node.CreatePublisher<trellis::examples::HelloWorld>(topic_name);

  // Ask the user to input his name
  Log::Info("Please enter your name: ");
  std::string name;
  std::getline(std::cin, name);

  unsigned int id = 0;

  // If the user wants to maintain control of the main thread, they can
  // call run_once() in a loop like so.
  while (node.RunOnce()) {
    // Let the user input a message
    Log::Info("Type the message you want to send: ");
    std::string message;
    std::getline(std::cin, message);

    // Create a protobuf message object
    trellis::examples::HelloWorld hello_world_message;
    hello_world_message.set_name(name);
    hello_world_message.set_msg(message);
    hello_world_message.set_id(id++);

    // Send the message
    publisher->Send(hello_world_message);
    Log::Info("Sent message!");
  }

  return 0;
}
