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

#ifndef TRELLIS_CORE_DISCOVERY_UTILS_HPP_
#define TRELLIS_CORE_DISCOVERY_UTILS_HPP_

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <limits.h>
#include <unistd.h>

#include <fstream>
#include <string>

#include "trellis/core/discovery/types.hpp"
#include "trellis/core/ipc/proto/rpc/types.hpp"

namespace trellis::core::discovery::utils {

/**
 * @brief Create a discovery sample for a proto publisher or subscriber.
 *
 * @param topic The topic name.
 * @param message_desc The serialized message description (FileDescriptorSet).
 * @param message_name The name of the protobuf message type.
 * @param publisher True if creating for a publisher, false for subscriber.
 * @param memory_file_list List of shared memory files used (publisher only).
 * @return discovery::Sample The constructed discovery sample.
 */
discovery::Sample CreateProtoPubSubSample(const std::string& topic, const std::string& message_desc,
                                          const std::string& message_name, bool publisher,
                                          std::vector<std::string> memory_file_list);

/**
 * @brief Create a discovery sample for a service server.
 *
 * @param port The TCP port the service is available on.
 * @param service_name The name of the service.
 * @param methods the mapping of available methods to the associated metadata
 * @return discovery::Sample The constructed service discovery sample.
 */
discovery::Sample CreateServiceServerSample(uint16_t port, const std::string& service_name,
                                            const ipc::proto::rpc::MethodsMap& methods);

/**
 * @brief Get a discovery sample for a node
 * @param node_name The name of the node
 * @return discovery::Sample The constructed service discovery sample.
 */
discovery::Sample GetNodeProcessSample(const std::string& node_name);

/**
 * @brief Retrieve the current system's hostname.
 *
 * @return std::string The hostname.
 */
std::string GetHostname();

/**
 * @brief Serialize a protobuf message's descriptor into a string.
 *
 * @param msg_ The protobuf message instance.
 * @return std::string A serialized FileDescriptorSet describing the message.
 */
std::string GetProtoMessageDescription(const google::protobuf::Message& msg_);

/**
 * @brief Serialize a protobuf message descriptor into a string.
 *
 * @param desc The message descriptor.
 * @return std::string A serialized FileDescriptorSet describing the message.
 */
std::string GetProtoMessageDescription(const google::protobuf::Descriptor* desc);

/**
 * @brief Get the full filesystem path to the current executable.
 *
 * @return std::string The executable path.
 */
std::string GetExecutablePath();

/**
 * @brief Extract the base name (filename only) from a given path.
 *
 * @param path The full path.
 * @return std::string The base filename.
 */
std::string GetBaseName(const std::string& path);

/**
 * @brief Get the first argument of the current process (`argv[0]`).
 *
 * @return std::string The argv[0] string.
 */
std::string GetArgv0();

}  // namespace trellis::core::discovery::utils

#endif  // TRELLIS_CORE_DISCOVERY_UTILS_HPP_
