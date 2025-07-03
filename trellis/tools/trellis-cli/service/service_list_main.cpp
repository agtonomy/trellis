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

#include "VariadicTable.h"
#include "trellis/core/discovery/discovery.hpp"
#include "trellis/tools/trellis-cli/constants.hpp"

namespace trellis {
namespace tools {
namespace cli {

namespace {
static constexpr size_t kMaxMethodsStringLen = 200;  // truncate long strings
}

int service_list_main(int argc, char* argv[]) {
  cxxopts::Options options(service_list_command.data(), service_list_command_desc.data());
  options.add_options()("h,help", "print usage");

  auto result = options.parse(argc, argv);
  if (result.count("help")) {
    std::cout << options.help() << std::endl;
    return 1;
  }

  // Delay to give time for discovery
  trellis::core::EventLoop loop;
  trellis::core::discovery::Discovery discovery("trellis-cli", loop, trellis::core::Config{});
  loop.RunFor(std::chrono::milliseconds(monitor_delay_ms));

  const auto service_samples = discovery.GetServiceSamples();

  VariadicTable<std::string, std::string> vt({"Service Name", "Methods"});

  for (const auto& sample : service_samples) {
    const auto& service = sample.service();

    // Generate list of method names
    std::stringstream methods_ss;
    bool first{false};
    for (const auto& method : service.methods()) {
      if (!first) {
        methods_ss << " ";
      }
      first = false;
      methods_ss << method.mname() << "(" << method.req_type() << " -> " << method.resp_type() << ")";
    }
    std::string methods_str = methods_ss.str();
    if (methods_str.size() > kMaxMethodsStringLen) {
      methods_str = methods_str.substr(0, kMaxMethodsStringLen) + "...";
    }
    // Add the service to the table
    vt.addRow(service.sname(), methods_str);
  }
  vt.print(std::cout);

  return 0;
}

}  // namespace cli
}  // namespace tools
}  // namespace trellis
