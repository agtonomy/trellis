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

#ifndef TRELLIS_TOOLS_TRELLIS_CLI_COMMAND_HANDLERS_HPP
#define TRELLIS_TOOLS_TRELLIS_CLI_COMMAND_HANDLERS_HPP

#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>

namespace trellis {
namespace tools {
namespace cli {

using CommandFunction = std::function<int(void)>;
struct CommandInfo {
  std::string description;
  CommandFunction handler;
};
using HandlersMap = std::unordered_map<std::string, cli::CommandInfo>;

namespace {
inline void PrintCommandsHelp(const std::string command, const cli::HandlersMap& handlers) {
  std::cout << std::endl << "Available " << command << " subcommands: " << std::endl;
  for (const auto& item : handlers) {
    const std::string& command = item.first;
    const std::string& description = item.second.description;
    std::cout << "\t" << command << ": " << description << std::endl;
  }
}

inline std::string ShiftCommand(int& argc, char**& argv) {
  const std::string command = (argc > 1) ? std::string(argv[1]) : "";
  if (argc > 1) {
    --argc;
    ++argv;
  }
  return command;
}
}  // namespace

inline int RunCommand(const std::string command, const std::string subcommand, const cli::HandlersMap& handlers) {
  auto it = handlers.find(subcommand);
  if (it == handlers.end()) {
    std::cerr << "Unrecognized subcommand: " << subcommand << std::endl;
    PrintCommandsHelp(command, handlers);
    return 1;
  }
  return it->second.handler();
}

// add all command handler main function declarations here
int topic_main(int argc, char* argv[]);

}  // namespace cli
}  // namespace tools
}  // namespace trellis

#endif  // TRELLIS_TOOLS_TRELLIS_CLI_COMMAND_HANDLERS_HPP
