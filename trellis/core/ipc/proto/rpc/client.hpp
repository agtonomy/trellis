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

#ifndef TRELLIS_CORE_IPC_PROTO_RPC_CLIENT_HPP_
#define TRELLIS_CORE_IPC_PROTO_RPC_CLIENT_HPP_

#include <optional>

#include "trellis/core/discovery/discovery.hpp"
#include "trellis/core/event_loop.hpp"
#include "trellis/core/ipc/proto/rpc/types.hpp"
#include "trellis/network/tcp.hpp"

namespace trellis::core::ipc::proto::rpc {

/**
 * @brief A gRPC-style client for asynchronous request/response calls over TCP using a protobuf-based interface.
 *
 * @tparam PROTO_SERVICE_T The protobuf-generated service type.
 */
template <typename PROTO_SERVICE_T>
class Client {
 public:
  /**
   * @brief Type alias for the response callback function.
   *
   * @tparam RESP_T The type of the expected protobuf response message.
   * @param status Status of the RPC call (success or failure).
   * @param response Pointer to the response message (may be empty in case of failure).
   */
  template <typename RESP_T>
  using ResponseCallback = std::function<void(ServiceCallStatus, const RESP_T*)>;

  /**
   * @brief Construct a new Client object.
   *
   * Subscribes to service discovery updates and creates a TCP client upon service registration.
   *
   * @param loop The event loop to run asynchronous TCP operations.
   * @param discovery Shared pointer to the service discovery instance.
   */
  Client(trellis::core::EventLoop loop, discovery::DiscoveryPtr discovery)
      : loop_{loop},
        discovery_{discovery},
        callback_handle_{discovery->AsyncReceiveServices(
            [this, loop](discovery::Discovery::EventType event, const discovery::Sample& sample) {
              if (sample.service().sname() == PROTO_SERVICE_T::descriptor()->full_name()) {
                const auto tcp_port = sample.service().tcp_port();
                if (event == discovery::Discovery::EventType::kNewUnregistration) {
                  if (tcp_client_.has_value()) {
                    const auto maybe_remote_port = tcp_client_.value().GetRemotePort();
                    if (!maybe_remote_port.has_value() || maybe_remote_port.value() == tcp_port) {
                      tcp_client_.reset();
                    }
                  }
                } else if (event == discovery::Discovery::EventType::kNewRegistration) {
                  if (!tcp_client_.has_value()) {
                    tcp_client_ = network::TCP(loop, "127.0.0.1", tcp_port);
                  }
                }
              }
            })} {}

  /**
   * @brief Asynchronously call a method on the remote service.
   *
   * @tparam REQ_T The request protobuf message type.
   * @tparam RESP_T The expected response protobuf message type.
   * @param method Name of the method to invoke.
   * @param request The request message object.
   * @param callback A callback to handle the response.
   * @param timeout_ms Optional timeout in milliseconds (currently unused).
   */
  template <typename REQ_T, typename RESP_T>
  void CallAsync(std::string_view method, REQ_T request, ResponseCallback<RESP_T> callback, unsigned timeout_ms = 0) {
    if (!tcp_client_.has_value() || call_pending_.load()) {
      RESP_T resp{};
      callback(kFailure, &resp);
      return;
    }
    call_pending_ = true;
    if (timeout_ms > 0) {
      timeout_timer_ = std::make_shared<TimerImpl>(
          loop_, TimerImpl::Type::kOneShot,
          [this, callback](const time::TimePoint&) {
            if (call_pending_.load() && tcp_client_.has_value()) {
              // Clear flag first so TCP callbacks see it
              call_pending_ = false;
              // Reset client to force a reconnection
              tcp_client_.value().Cancel();
              tcp_client_.reset();
              RESP_T resp{};
              callback(kTimedOut, &resp);
            }
          },
          0, timeout_ms);
    }

    // Populate request message and serialize it to generate our payload
    discovery::Request request_msg;
    request_msg.mutable_header()->set_mname(std::string(method));
    request.SerializeToString(request_msg.mutable_request());
    auto request_buffer = std::make_shared<std::string>();
    request_msg.SerializeToString(request_buffer.get());

    // Generate our header payload which contains the size
    const uint32_t size = request_buffer->size();
    auto send_header_buf = std::make_shared<std::array<uint8_t, sizeof(size)>>();
    memcpy(send_header_buf.get(), &size, sizeof(size));

    // We chain together 4 events:
    // 1. Send 4-byte request payload size
    // 2. Send request payload
    // 3. Receive 4-byte response payload size
    // 4. Receive response payload
    tcp_client_.value().AsyncSendAll(
        send_header_buf->data(), send_header_buf->size(),
        [this, send_header_buf, request_buffer, callback](const trellis::core::error_code& ec, size_t bytes_sent) {
          if (ec == asio::error::operation_aborted || !call_pending_.load()) {
            return;  // short circuit on timeout
          } else if (ec) {
            call_pending_ = false;
            RESP_T resp{};
            callback(kFailure, &resp);
            return;
          }
          // We sent the 4-byte length to the server, now let's send the actual payload
          tcp_client_.value().AsyncSendAll(
              request_buffer->data(), request_buffer->size(),
              [this, request_buffer, callback = std::move(callback)](const trellis::core::error_code& ec,
                                                                     size_t bytes_sent) {
                if (ec == asio::error::operation_aborted || !call_pending_.load()) {
                  return;  // short circuit on timeout
                } else if (ec) {
                  call_pending_ = false;
                  RESP_T resp{};
                  callback(kFailure, &resp);
                  return;
                }
                // We sent the payload to the server, now let's receive the 4-byte length from the server
                auto receive_header_buf = std::make_shared<std::array<uint8_t, sizeof(uint32_t)>>();
                tcp_client_.value().AsyncReceiveAll(
                    receive_header_buf->data(), receive_header_buf->size(),
                    [this, receive_header_buf, callback = std::move(callback)](const trellis::core::error_code& ec,
                                                                               size_t /*bytes_received*/) {
                      if (ec == asio::error::operation_aborted || !call_pending_.load()) {
                        return;  // short circuit on timeout
                      } else if (ec) {
                        call_pending_ = false;
                        RESP_T rpc_response{};
                        callback(kFailure, &rpc_response);
                        return;
                      }

                      // Since performance is not critical for RPCs, and because we don't know the receive payload size
                      // ahead of time, we'll dynamically allocate the buffer size. The protocol does not enforce any
                      // specific limit on payload size beyond the 32-bit length field.
                      const uint32_t length = *reinterpret_cast<uint32_t*>(receive_header_buf->data());
                      auto receive_buffer = std::make_shared<std::vector<uint8_t>>(length);
                      tcp_client_.value().AsyncReceiveAll(
                          receive_buffer->data(), receive_buffer->size(),
                          [this, receive_buffer, callback = std::move(callback)](const trellis::core::error_code& ec,
                                                                                 size_t bytes_received) {
                            call_pending_ = false;
                            if (timeout_timer_) timeout_timer_.reset();
                            RESP_T rpc_response{};
                            if (ec) {
                              callback(kFailure, &rpc_response);
                              return;
                            }

                            discovery::Response response;
                            response.ParseFromArray(receive_buffer->data(), bytes_received);
                            if (response.header().status() == discovery::ServiceHeader::failed) {
                              callback(kFailure, &rpc_response);
                            } else {
                              rpc_response.ParseFromString(response.response());
                              callback(kSuccess, &rpc_response);
                            }
                          });
                    });
              });
        });
  }

  /**
   * @brief Destructor that stops receiving service discovery events.
   */
  ~Client() { discovery_->StopReceive(callback_handle_); }

 private:
  trellis::core::EventLoop loop_;                         ///< Event loop to run the client on
  discovery::DiscoveryPtr discovery_;                     ///< Pointer to the discovery service
  discovery::Discovery::CallbackHandle callback_handle_;  ///< Handle for the discovery callback
  std::optional<network::TCP> tcp_client_;                ///< Active TCP client, if connected
  std::atomic<bool> call_pending_{false};                 ///< Indicates whether a call is currently in progress
  core::Timer timeout_timer_;
};

}  // namespace trellis::core::ipc::proto::rpc

namespace trellis::core {

/**
 * @brief Backwards-compatible alias for proto::rpc::Client.
 *
 * @tparam PROTO_MSG_T The protobuf service definition type.
 */
template <typename PROTO_MSG_T>
using ServiceClient = std::shared_ptr<ipc::proto::rpc::Client<PROTO_MSG_T>>;

}  // namespace trellis::core

#endif  // TRELLIS_CORE_IPC_PROTO_RPC_CLIENT_HPP_
