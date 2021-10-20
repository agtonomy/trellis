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

#include "trellis/tools/trellis-cli/command_handlers.hpp"
#include "trellis/tools/trellis-cli/constants.hpp"

namespace trellis {
namespace tools {
namespace cli {

int topic_main(int argc, char* argv[]) {
  const std::string subcommand = cli::ShiftCommand(argc, argv);
  HandlersMap handlers{
      {"publish", {topic_publish_command_desc.data(), [argc, argv]() { return cli::topic_publish_main(argc, argv); }}},
      {"list", {topic_list_command_desc.data(), [argc, argv]() { return cli::topic_list_main(argc, argv); }}},
      {"echo", {topic_echo_command_desc.data(), [argc, argv]() { return cli::topic_echo_main(argc, argv); }}},
  };
  if (subcommand.empty()) {
    std::cout << "Must specify a subcommand... " << std::endl;
    cli::PrintCommandsHelp("topic", handlers);
    return 0;
  }
  return cli::RunCommand("topic", subcommand, handlers);
}

}  // namespace cli
}  // namespace tools
}  // namespace trellis
