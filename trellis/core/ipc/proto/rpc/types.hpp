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
 */

#ifndef TRELLIS_CORE_IPC_PROTO_RPC_TYPES_HPP_
#define TRELLIS_CORE_IPC_PROTO_RPC_TYPES_HPP_

#include <google/protobuf/descriptor.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

namespace trellis::core::ipc::proto::rpc {

enum ServiceCallStatus { kTimedOut = 0, kSuccess = 1, kFailure = 2 };
/// The longest RPC request or response body a peer accepts. A longer length prefix closes the connection instead of
/// allocating it. Far above any message an RPC is expected to carry.
static constexpr uint32_t kMaxFrameSize{256u * 1024u * 1024u};

/// The body length that a frame's 4-byte prefix claims, in host byte order.
inline uint32_t ReadFrameLength(const std::array<uint8_t, sizeof(uint32_t)>& prefix) {
  uint32_t length;
  std::memcpy(&length, prefix.data(), sizeof(length));
  return length;
}

/// Whether a frame of this body length must be refused rather than read.
inline bool IsFrameTooLarge(uint32_t length) { return length > kMaxFrameSize; }

/**
 * @brief Metadata for describing service methods.
 */
struct MethodMetadata {
  const google::protobuf::MethodDescriptor* descriptor;  ///< Protobuf method descriptor
  const std::string input_type_name;                     ///< Name of input message type
  const std::string input_type_desc;                     ///< Description of input message structure
  const std::string output_type_name;                    ///< Name of output message type
  const std::string output_type_desc;                    ///< Description of output message structure
};
using MethodsMap = std::unordered_map<std::string, MethodMetadata>;

}  // namespace trellis::core::ipc::proto::rpc

namespace trellis::core {
// alias for backwards compatability
using ipc::proto::rpc::ServiceCallStatus;
using trellis::core::ipc::proto::rpc::kFailure;
using trellis::core::ipc::proto::rpc::kSuccess;
using trellis::core::ipc::proto::rpc::kTimedOut;
}  // namespace trellis::core

#endif  // TRELLIS_CORE_IPC_PROTO_RPC_TYPES_HPP_
