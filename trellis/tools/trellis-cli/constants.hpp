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

#ifndef TRELLIS_TOOLS_TRELLIS_CLI_CONSTANTS_HPP
#define TRELLIS_TOOLS_TRELLIS_CLI_CONSTANTS_HPP

#include <string>

namespace trellis {
namespace tools {
namespace cli {
namespace {

constexpr std::string_view root_command{"trellis-cli"};

// Full command strings
constexpr std::string_view topic_publish_command{"trellis-cli topic publish"};
constexpr std::string_view topic_echo_command{"trellis-cli topic echo"};
constexpr std::string_view topic_list_command{"trellis-cli topic list"};
constexpr std::string_view node_list_command{"trellis-cli node list"};
constexpr std::string_view service_list_command{"trellis-cli service list"};
constexpr std::string_view service_info_command{"trellis-cli service info"};

// Command descriptions
constexpr std::string_view topic_desc{"analyze pub/sub topics"};
constexpr std::string_view node_desc{"analyze nodes (processes)"};
constexpr std::string_view service_desc{"analyze rpc services"};
constexpr std::string_view topic_publish_command_desc{"publish messages to a given topic"};
constexpr std::string_view topic_echo_command_desc{"echo messages from a given topic"};
constexpr std::string_view topic_list_command_desc{"list info about available topics"};
constexpr std::string_view node_list_command_desc{"list online nodes"};
constexpr std::string_view service_list_command_desc{"list active services"};
constexpr std::string_view service_info_command_desc{"display info about an rpc service"};

// Delay to allow discovery / monitoring information to propogage
constexpr unsigned monitor_delay_ms = 1000U;

}  // namespace
}  // namespace cli
}  // namespace tools
}  // namespace trellis

#endif  // TRELLIS_TOOLS_TRELLIS_CLI_CONSTANTS_HPP
