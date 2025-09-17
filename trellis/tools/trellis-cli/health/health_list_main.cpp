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

#include <cxxopts.hpp>
#include <thread>

#include "trellis/core/health_monitor.hpp"
#include "trellis/core/node.hpp"
#include "trellis/tools/trellis-cli/constants.hpp"
#include "trellis/utils/formatting/table.hpp"

namespace trellis {
namespace tools {
namespace cli {

int health_list_main(int argc, char* argv[]) {
  cxxopts::Options options(health_list_command.data(), health_list_command_desc.data());
  options.add_options()("h,help", "print usage");

  const auto result = options.parse(argc, argv);
  if (result.count("help")) {
    std::cout << options.help() << std::endl;
    return 1;
  }

  trellis::core::time::TimePoint start_time{trellis::core::time::Now()};
  trellis::core::time::TimePoint last_event_time{trellis::core::time::Now()};
  bool got_event{false};

  trellis::core::Node node(root_command.data(), {});
  trellis::core::HealthMonitor monitor{
      node.GetEventLoop(),
      {},
      [&node](unsigned interval_ms, trellis::core::TimerImpl::Callback cb) {
        return node.CreateOneShotTimer(interval_ms, cb);
      },
      [&node](const std::string& topic, trellis::core::SubscriberImpl<trellis::core::HealthHistory>::Callback cb) {
        return node.CreateSubscriber<trellis::core::HealthHistory>(topic, cb);
      },
      [&last_event_time, &got_event](const std::string& app_name, trellis::core::HealthMonitor::Event even,
                                     const trellis::core::time::TimePoint& now) {
        last_event_time = now;
        got_event = true;
      }};

  std::cout << "Collecting data..." << std::endl;
  while (true) {
    node.RunN(kHealthMonitorRunLoopCycles);
    const auto time_since_start_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(trellis::core::time::Now() - start_time).count();
    const auto time_since_last_event_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(trellis::core::time::Now() - last_event_time).count();
    const bool timing_out = (time_since_start_ms > kHealthMonitorTimeoutMs);
    const bool events_went_idle = (got_event && time_since_last_event_ms > kHealthMonitorQuietTimeMs);
    if (timing_out || events_went_idle) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kHealthMonitorRunIntervalMs));
  }
  node.Stop();

  trellis::utils::formatting::Table<std::string, std::string, trellis::core::Health::Code, std::string> table(
      {"Node", "Health State", "Code", "Description"});
  const auto node_name_set = monitor.GetNodeNames();
  for (const auto& name : node_name_set) {
    const auto& status = monitor.GetLastHealthUpdate(name);
    table.AddRow(name, trellis::core::HealthState_Name(status.health_state()), status.status_code(),
                 status.status_description());
  }

  table.Print(std::cout);
  return 0;
}

}  // namespace cli
}  // namespace tools
}  // namespace trellis
