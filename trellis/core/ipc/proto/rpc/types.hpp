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

namespace trellis::core::ipc::proto::rpc {

enum ServiceCallStatus { kTimedOut = 0, kSuccess = 1, kFailure = 2 };
static constexpr unsigned kMaxBufferSize{65535u};  // TCP max

}  // namespace trellis::core::ipc::proto::rpc

namespace trellis::core {
// alias for backwards compatability
using ipc::proto::rpc::ServiceCallStatus;
using trellis::core::ipc::proto::rpc::kFailure;
using trellis::core::ipc::proto::rpc::kSuccess;
using trellis::core::ipc::proto::rpc::kTimedOut;
}  // namespace trellis::core

#endif  // TRELLIS_CORE_IPC_PROTO_RPC_TYPES_HPP_
