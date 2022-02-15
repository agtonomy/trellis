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

#include "json.hpp"
#include "trellis/core/node.hpp"
#include "trellis/tools/trellis-cli/constants.hpp"

namespace trellis {
namespace tools {
namespace cli {

using namespace trellis::core;

namespace {
time::TimePoint last_echo_time_;
}

int topic_echo_main(int argc, char* argv[]) {
  cxxopts::Options options(topic_echo_command.data(), topic_echo_command_desc.data());
  options.add_options()("t,topic", "topic name(s)", cxxopts::value<std::vector<std::string>>())(
      "r,rate", "max echo rate in hz", cxxopts::value<int>()->default_value("0"))(
      "w,whitespace", "add whitespace to output")("v,verbose", "include non-essential output")("h,help", "print usage")(
      "s,stamp", "inject timestamp into message output");

  auto result = options.parse(argc, argv);
  if (result.count("help") || !result.count("topic")) {
    std::cout << options.help() << std::endl;
    return 1;
  }

  const auto topics = result["topic"].as<std::vector<std::string>>();
  const int rate = std::clamp(result["rate"].as<int>(), 0, 1000);
  unsigned throttle_interval_ms = (rate > 0) ? 1000.0 / static_cast<unsigned>(rate) : 0;
  const bool add_whitespace = result.count("whitespace");
  const bool verbose = result.count("verbose");
  const bool timestamp = result.count("stamp");

  Node node(root_command.data());
  std::vector<trellis::core::DynamicSubscriber> subs;
  auto ev = node.GetEventLoop();
  for (const auto& topic : topics) {
    auto sub = node.CreateDynamicSubscriber(topic, [ev, throttle_interval_ms, add_whitespace,
                                                    timestamp](const google::protobuf::Message& msg) {
      if (throttle_interval_ms != 0) {
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(time::Now() - last_echo_time_).count();
        if (elapsed_ms <= throttle_interval_ms) {
          return;  // rate throttle
        }
      }
      // convert to JSON and print
      std::string json_raw;
      google::protobuf::util::JsonPrintOptions json_options;
      json_options.add_whitespace = add_whitespace;
      json_options.always_print_primitive_fields = true;
      json_options.always_print_enums_as_ints = false;
      json_options.preserve_proto_field_names = false;
      auto status = google::protobuf::util::MessageToJsonString(msg, &json_raw, json_options);
      if (!status.ok()) {
        std::ostringstream ss;
        ss << status;
        throw std::runtime_error(ss.str());
      }

      last_echo_time_ = time::Now();

      if (timestamp) {
        nlohmann::json json_obj = nlohmann::json::parse(json_raw);
        auto it = json_obj.find("timestamp");
        // Make sure timestamp key doesn't already exist...
        if (it == json_obj.end()) {
          json_obj["timestamp"] = time::TimePointToSeconds(last_echo_time_);
          json_raw = json_obj.dump();
        }
      }

      // Post the output to the event loop to remove synchronization problems between multiple subscribers
      asio::post(*ev, [json_raw]() { std::cout << json_raw << std::endl; });
    });

    subs.push_back(sub);
  }

  if (verbose) {
    std::cout << "Echoing messages on";
    for (const auto& topic : topics) {
      std::cout << " \"" << topic << "\"";
    }
    if (rate != 0) {
      std::cout << " with max rate " << rate << " Hz and";
    }
    std::cout << " with " << (add_whitespace ? "multi" : "one") << "-line output.";
    std::cout << std::endl;
  }

  node.Run();
  return 0;
}

}  // namespace cli
}  // namespace tools
}  // namespace trellis
