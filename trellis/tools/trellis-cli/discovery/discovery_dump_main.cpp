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

#include <google/protobuf/util/json_util.h>

#include <cxxopts.hpp>
#include <sstream>
#include <thread>

#include "trellis/core/health_monitor.hpp"
#include "trellis/core/node.hpp"
#include "trellis/tools/trellis-cli/constants.hpp"

namespace trellis {
namespace tools {
namespace cli {

int discovery_dump_main(int argc, char* argv[]) {
  trellis::core::EventLoop loop;
  trellis::core::discovery::Discovery discovery("trellis-cli", loop, trellis::core::Config{});
  loop.RunFor(std::chrono::milliseconds(monitor_delay_ms));
  const auto pubsub_samples = discovery.GetPubSubSamples();
  const auto service_samples = discovery.GetServiceSamples();
  const auto process_samples = discovery.GetProcessSamples();

  google::protobuf::util::JsonPrintOptions options;
  options.add_whitespace = true;
  options.preserve_proto_field_names = true;

  std::string json_string;
  for (const auto& sample : pubsub_samples) {
    json_string.clear();
    const auto status = google::protobuf::util::MessageToJsonString(sample, &json_string, options);
    if (!status.ok()) {
      std::ostringstream ss;
      ss << status;
      throw std::runtime_error(ss.str());
    }
    std::cout << json_string << std::endl;
  }
  for (const auto& sample : service_samples) {
    json_string.clear();
    const auto status = google::protobuf::util::MessageToJsonString(sample, &json_string, options);
    if (!status.ok()) {
      std::ostringstream ss;
      ss << status;
      throw std::runtime_error(ss.str());
    }
    std::cout << json_string << std::endl;
  }
  for (const auto& sample : process_samples) {
    json_string.clear();
    const auto status = google::protobuf::util::MessageToJsonString(sample, &json_string, options);
    if (!status.ok()) {
      std::ostringstream ss;
      ss << status;
      throw std::runtime_error(ss.str());
    }
    std::cout << json_string << std::endl;
  }
  return 0;
}

}  // namespace cli
}  // namespace tools
}  // namespace trellis
