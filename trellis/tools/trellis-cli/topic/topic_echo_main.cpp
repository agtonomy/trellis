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

#include <google/protobuf/util/json_util.h>

#include <cxxopts.hpp>

#include "trellis/core/node.hpp"

namespace trellis {
namespace tools {
namespace cli {

using namespace trellis::core;

int topic_echo_main(int argc, char* argv[]) {
  cxxopts::Options options("trellis-cli topic echo", "echo messages from a given topic");
  options.add_options()("t,topic", "topic name", cxxopts::value<std::string>())("h,help", "print usage");

  auto result = options.parse(argc, argv);
  if (result.count("help") || !result.count("topic")) {
    std::cout << options.help() << std::endl;
    return 1;
  }
  const std::string topic = result["topic"].as<std::string>();

  Node node("trellis-cli");
  auto sub = node.CreateDynamicSubscriber(topic, [](const google::protobuf::Message& msg) {
    // convert to JSON and print
    std::string json;
    google::protobuf::util::JsonPrintOptions json_options;
    json_options.add_whitespace = true;
    json_options.always_print_primitive_fields = true;
    json_options.always_print_enums_as_ints = false;
    json_options.preserve_proto_field_names = false;
    auto r = google::protobuf::util::MessageToJsonString(msg, &json, json_options);
    std::cout << json << std::endl;
  });

  std::cout << "Echoing messages on \"" << topic << "\"" << std::endl;

  node.Run();
  return 0;
}

}  // namespace cli
}  // namespace tools
}  // namespace trellis
