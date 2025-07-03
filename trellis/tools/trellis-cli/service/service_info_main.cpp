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

#include <cxxopts.hpp>
#include <thread>

#include "trellis/core/discovery/discovery.hpp"
#include "trellis/tools/trellis-cli/constants.hpp"

namespace trellis {
namespace tools {
namespace cli {

int service_info_main(int argc, char* argv[]) {
  cxxopts::Options options(service_info_command.data(), service_info_command_desc.data());
  options.add_options()("s,service", "service name", cxxopts::value<std::string>())("h,help", "print usage");

  auto result = options.parse(argc, argv);
  if (result.count("help") || !result.count("service")) {
    std::cout << options.help() << std::endl;
    return 1;
  }

  const std::string service_name = result["service"].as<std::string>();

  // Delay to give time for discovery
  trellis::core::EventLoop loop;
  trellis::core::discovery::Discovery discovery("trellis-cli", loop, trellis::core::Config{});
  loop.RunFor(std::chrono::milliseconds(monitor_delay_ms));

  const auto service_samples = discovery.GetServiceSamples();
  for (const auto& sample : service_samples) {
    const auto& service = sample.service();
    if (service.sname() == service_name) {
      // Display service header
      std::cout << "┌─ RPC Service: " << service.sname() << " ─" << std::endl;
      std::cout << "│" << std::endl;

      // Basic service info
      std::cout << "│ Host:        " << service.hname() << std::endl;
      std::cout << "│ Process:     " << service.uname() << " (PID: " << service.pid() << ")" << std::endl;
      std::cout << "│ Executable:  " << service.pname() << std::endl;
      std::cout << "│ TCP Port:    " << service.tcp_port() << std::endl;
      std::cout << "│" << std::endl;

      // Methods section
      if (service.methods_size() > 0) {
        std::cout << "│ Methods (" << service.methods_size() << "):" << std::endl;
        std::cout << "│" << std::endl;

        for (const auto& method : service.methods()) {
          std::cout << "│   • " << method.mname() << std::endl;
          std::cout << "│     Request:  " << method.req_type() << std::endl;
          std::cout << "│     Response: " << method.resp_type() << std::endl;
          // TODO (bsirang) display call count after it's implemented
          std::cout << "│" << std::endl;
        }
      } else {
        std::cout << "│ No methods registered" << std::endl;
        std::cout << "│" << std::endl;
      }

      std::cout << "└─────────────────────────────────────────" << std::endl;
      return 0;  // Exit early since we found the service
    }
  }

  std::cout << "Service '" << service_name << "' not found" << std::endl;
  return 1;
}

}  // namespace cli
}  // namespace tools
}  // namespace trellis
