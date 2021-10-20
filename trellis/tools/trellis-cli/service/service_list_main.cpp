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

#include "trellis/tools/trellis-cli/constants.hpp"
#include "trellis/tools/trellis-cli/monitoring_utils.hpp"

namespace trellis {
namespace tools {
namespace cli {

int service_list_main(int argc, char* argv[]) {
  cxxopts::Options options(service_list_command.data(), service_list_command_desc.data());
  options.add_options()("h,help", "print usage");

  auto result = options.parse(argc, argv);
  if (result.count("help")) {
    std::cout << options.help() << std::endl;
    return 1;
  }

  eCAL::Initialize(0, nullptr, root_command.data(), eCAL::Init::All);

  // Delay to give time for discovery
  std::this_thread::sleep_for(std::chrono::milliseconds(monitor_delay_ms));

  MonitorUtil mutil;
  mutil.PrintServices();

  eCAL::Finalize();
  return 0;
}

}  // namespace cli
}  // namespace tools
}  // namespace trellis
