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
   * @brief Destructor that stops receiving service discovery events.
   */
  ~Client() {
    discovery_->StopReceive(callback_handle_);
    CleanPendingRequest();

    // Clear the request queue and cancel any timers
    while (!queued_requests_.empty()) {
      auto request = queued_requests_.front();
      queued_requests_.pop();
    }

    if (tcp_client_.has_value()) {
      tcp_client_->Cancel();  // Cancel any ongoing operations
      tcp_client_->Close();   // Close the TCP connection
    }
  }

  /**
   * @brief Asynchronously call a method on the remote service. If there is already a call from this client in progress,
   * it will be queued.
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
    if (!tcp_client_.has_value()) {
      RESP_T resp{};
      callback(kFailure, &resp);
      return;
    }

    // Populate request message and serialize it to generate our payload
    discovery::Request request_msg;
    request_msg.mutable_header()->set_mname(std::string(method));
    request.SerializeToString(request_msg.mutable_request());
    auto request_buffer = std::make_shared<std::string>();
    request_msg.SerializeToString(request_buffer.get());

    // Create request
    auto queued_request = std::make_shared<QueuedRequest>(
        loop_, request_buffer,
        [this, callback](const discovery::Response& response) {  // Handle successful response
          RESP_T resp{};
          resp.ParseFromString(response.response());
          callback(kSuccess, &resp);
          CleanPendingAndProcessNext();
        },
        [this, callback]() {  // Handle failure
          RESP_T resp{};
          callback(kFailure, &resp);
          CleanPendingAndProcessNext();
        },
        [this, callback]() {  // Handle timeout
          RESP_T resp{};
          callback(kTimedOut, &resp);
          if (this->tcp_client_) {
            this->tcp_client_->Cancel();  // Cancel the TCP client to avoid further processing
          }
          CleanPendingAndProcessNext();
        },
        timeout_ms);

    queued_requests_.push(queued_request);
    ProcessNextRequest();
  }

 private:
  struct QueuedRequest {
    using SuccessFn = std::function<void(const discovery::Response& response)>;
    using FailureFn = std::function<void()>;
    using TimeoutFn = std::function<void()>;

    std::shared_ptr<std::string> request_buffer;  ///< Buffer for the request payload
    SuccessFn success_fn;                         ///< Function to handle success with response
    FailureFn failure_fn;                         ///< Function to handle failure
    TimeoutFn timeout_fn;                         ///< Function to handle timeout
    const unsigned timeout_ms;                    ///< Timeout in milliseconds

    QueuedRequest(trellis::core::EventLoop loop, std::shared_ptr<std::string> request_buffer, SuccessFn success_fn,
                  FailureFn failure_fn, TimeoutFn timeout_fn, unsigned timeout_ms)
        : request_buffer(request_buffer),
          success_fn(std::move(success_fn)),
          failure_fn(std::move(failure_fn)),
          timeout_fn(std::move(timeout_fn)),
          timeout_ms(timeout_ms) {}
  };

  void CleanPendingRequest() {
    if (pending_timer_) {
      pending_timer_->Stop();
    }
    pending_request_.reset();
  }

  void EnqueueProcessNext() {
    asio::post(*loop_, [this]() { ProcessNextRequest(); });
  }

  void CleanPendingAndProcessNext() {
    CleanPendingRequest();
    EnqueueProcessNext();
  }

  // Triggered by either a new request or a timeout
  void ProcessNextRequest() {
    if (pending_request_) {
      // Already processing a request
      return;
    }

    if (queued_requests_.empty()) {
      // No queued requests to process
      return;
    }

    pending_request_ = std::move(queued_requests_.front());
    queued_requests_.pop();

    if (!pending_request_) {
      throw std::runtime_error("queued request is null, this should not happen");
    }

    // Generate our header payload which contains the size
    const uint32_t size = pending_request_->request_buffer->size();
    auto send_header_buf = std::make_shared<std::array<uint8_t, sizeof(size)>>();
    memcpy(send_header_buf.get(), &size, sizeof(size));

    // Start the timeout timer for this request
    if (pending_request_->timeout_ms > 0) {
      // Use weak_ptr to avoid circular reference and check if client still exists
      std::weak_ptr<QueuedRequest> weak_request = pending_request_;
      pending_timer_ = std::make_shared<OneShotTimerImpl>(
          loop_,
          [this, weak_request](const time::TimePoint&) {
            auto request = weak_request.lock();
            if (request && pending_request_ == request) {
              request->timeout_fn();
            }
          },
          pending_request_->timeout_ms);
    }

    // Drain any stale data from the receive buffer immediately before sending.
    // This handles cases where a previous request timed out but the server eventually
    // sent a response that is still sitting in the socket buffer.
    // TODO (bsirang) We should implement request/response ID tracking to avoid this situation entirely.
    tcp_client_->DrainReceiveBuffer();

    // We chain together 4 events:
    // 1. Send 4-byte request payload size
    // 2. Send request payload
    // 3. Receive 4-byte response payload size
    // 4. Receive response payload
    tcp_client_.value().AsyncSendAll(
        send_header_buf->data(), send_header_buf->size(),
        [this, send_header_buf](const trellis::core::error_code& ec, size_t bytes_sent) {
          if (ec == asio::error::operation_aborted || !pending_request_) {
            return;  // short circuit on timeout
          } else if (ec) {
            pending_request_->failure_fn();
            return;
          }
          // We sent the 4-byte length to the server, now let's send the actual payload
          tcp_client_.value().AsyncSendAll(
              pending_request_->request_buffer->data(), pending_request_->request_buffer->size(),
              [this](const trellis::core::error_code& ec, size_t bytes_sent) {
                if (ec == asio::error::operation_aborted || !pending_request_) {
                  return;  // short circuit on timeout
                } else if (ec) {
                  pending_request_->failure_fn();
                  return;
                }
                // We sent the payload to the server, now let's receive the 4-byte length from the server
                auto receive_header_buf = std::make_shared<std::array<uint8_t, sizeof(uint32_t)>>();
                tcp_client_.value().AsyncReceiveAll(
                    receive_header_buf->data(), receive_header_buf->size(),
                    [this, receive_header_buf](const trellis::core::error_code& ec, size_t /*bytes_received*/) {
                      if (ec == asio::error::operation_aborted || !pending_request_) {
                        return;  // short circuit on timeout
                      } else if (ec) {
                        pending_request_->failure_fn();
                        return;
                      }

                      // Since performance is not critical for RPCs, and because we don't know the receive payload
                      // size ahead of time, we'll dynamically allocate the buffer size. The protocol does not
                      // enforce any specific limit on payload size beyond the 32-bit length field.
                      const uint32_t length = *reinterpret_cast<uint32_t*>(receive_header_buf->data());
                      auto receive_buffer = std::make_shared<std::vector<uint8_t>>(length);
                      tcp_client_.value().AsyncReceiveAll(
                          receive_buffer->data(), receive_buffer->size(),
                          [this, receive_buffer](const trellis::core::error_code& ec, size_t bytes_received) {
                            if (ec == asio::error::operation_aborted || !pending_request_) {
                              return;  // short circuit on timeout
                            }

                            if (ec) {
                              pending_request_->failure_fn();
                              return;
                            }

                            discovery::Response response;
                            response.ParseFromArray(receive_buffer->data(), bytes_received);
                            if (response.header().status() == discovery::ServiceHeader::failed) {
                              pending_request_->failure_fn();
                            } else {
                              pending_request_->success_fn(response);
                            }
                          });
                    });
              });
        });
  }

 private:
  trellis::core::EventLoop loop_;                               ///< Event loop to run the client on
  discovery::DiscoveryPtr discovery_;                           ///< Pointer to the discovery service
  discovery::Discovery::CallbackHandle callback_handle_;        ///< Handle for the discovery callback
  std::optional<network::TCP> tcp_client_;                      ///< Active TCP client, if connected
  std::queue<std::shared_ptr<QueuedRequest>> queued_requests_;  ///< Queue of requests
  std::shared_ptr<QueuedRequest> pending_request_;              ///< Currently processing request, if any
  core::Timer pending_timer_;                                   ///< Timer for pending request, if any
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
