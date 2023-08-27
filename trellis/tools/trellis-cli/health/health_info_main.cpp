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

#include <google/protobuf/util/json_util.h>

#include <cxxopts.hpp>
#include <thread>

#include "trellis/core/health_monitor.hpp"
#include "trellis/core/node.hpp"
#include "trellis/tools/trellis-cli/constants.hpp"

namespace trellis {
namespace tools {
namespace cli {

int health_info_main(int argc, char* argv[]) {
  cxxopts::Options options(service_info_command.data(), service_info_command_desc.data());
  options.add_options()("n,node", "node name", cxxopts::value<std::string>())("h,help", "print usage");

  const auto result = options.parse(argc, argv);
  if (result.count("help") || !result.count("node")) {
    std::cout << options.help() << std::endl;
    return 1;
  }

  const std::string expected_node_name = result["node"].as<std::string>();

  trellis::core::time::TimePoint start_time{trellis::core::time::Now()};
  bool got_node_update{false};

  trellis::core::Node node(root_command.data(), {});
  trellis::core::HealthMonitor monitor{
      node.GetEventLoop(),
      {},
      [&node](unsigned interval_ms, trellis::core::TimerImpl::Callback cb) {
        return node.CreateOneShotTimer(interval_ms, cb);
      },
      [&node](const std::string& topic,
              trellis::core::SubscriberImpl<trellis::core::HealthHistory,
                                            trellis::core::HealthMonitor::kMemoryPoolSize>::Callback cb) {
        return node.CreateSubscriber<trellis::core::HealthHistory, trellis::core::HealthMonitor::kMemoryPoolSize>(topic,
                                                                                                                  cb);
      },
      [expected_node_name, &got_node_update](const std::string& node_name, trellis::core::HealthMonitor::Event event,
                                             const trellis::core::time::TimePoint&) {
        if (!got_node_update) {
          got_node_update = (node_name == expected_node_name);
        }
      }};

  std::cout << "Collecting data..." << std::endl;
  while (true) {
    node.RunN(kHealthMonitorRunLoopCycles);
    const auto time_since_start_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(trellis::core::time::Now() - start_time).count();
    const bool timing_out = (time_since_start_ms > kHealthMonitorTimeoutMs);
    if (timing_out || got_node_update) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kHealthMonitorRunIntervalMs));
  }
  node.Stop();

  if (got_node_update) {
    google::protobuf::util::JsonPrintOptions json_options;
    json_options.add_whitespace = true;
    json_options.always_print_primitive_fields = true;
    json_options.always_print_enums_as_ints = false;
    json_options.preserve_proto_field_names = false;
    const auto& msg = monitor.GetHealthHistory(expected_node_name);
    for (const auto& update : msg) {
      std::string json_raw;
      const auto status = google::protobuf::util::MessageToJsonString(update, &json_raw, json_options);
      if (!status.ok()) {
        std::ostringstream ss;
        ss << status;
        throw std::runtime_error(ss.str());
      }
      std::cout << json_raw << std::endl;
    }
  } else {
    std::cout << "Did not receive health update for " << expected_node_name << std::endl;
  }

  return 0;
}

}  // namespace cli
}  // namespace tools
}  // namespace trellis
