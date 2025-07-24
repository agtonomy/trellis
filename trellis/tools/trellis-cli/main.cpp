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

#include "command_handlers.hpp"
#include "constants.hpp"

using namespace trellis::tools;

int main(int argc, char* argv[]) {
  const std::string command = cli::ShiftCommand(argc, argv);

  cli::HandlersMap handlers{
      {"topic", {cli::topic_desc.data(), [argc, argv]() { return cli::topic_main(argc, argv); }}},
      {"node", {cli::node_desc.data(), [argc, argv]() { return cli::node_main(argc, argv); }}},
      {"service", {cli::service_desc.data(), [argc, argv]() { return cli::service_main(argc, argv); }}},
      {"health", {cli::health_desc.data(), [argc, argv]() { return cli::health_main(argc, argv); }}},
      {"discovery", {cli::discovery_desc.data(), [argc, argv]() { return cli::discovery_main(argc, argv); }}},
  };

  if (command.empty()) {
    std::cout << "Must specify a command... " << std::endl;
    cli::PrintCommandsHelp(cli::root_command.data(), handlers);
    return 0;
  }

  return cli::RunCommand(cli::root_command.data(), command, handlers);
}
