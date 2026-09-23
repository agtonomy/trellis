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

#include <exception>
#include <memory>
#include <queue>

#include "trellis/core/discovery/discovery.hpp"
#include "trellis/core/event_loop.hpp"
#include "trellis/core/ipc/proto/rpc/types.hpp"
#include "trellis/network/tcp.hpp"

namespace trellis::core::ipc::proto::rpc {

/**
 * @brief A gRPC-style client for asynchronous request/response calls over TCP using a protobuf-based interface.
 *
 * Instances must be owned by a std::shared_ptr (see Node::CreateServiceClient). Every call gets exactly one
 * callback: destruction fails whatever is still outstanding with kFailure, invoking those callbacks synchronously
 * on the destroying thread before the destructor returns. Owners whose callbacks capture `this` must release the
 * client before the members those callbacks touch are destroyed.
 *
 * Destroy a Client on the thread running its event loop, or once that loop has stopped. Teardown closes the socket
 * and cancels the timeout timer without synchronizing against a running loop.
 *
 * @tparam PROTO_SERVICE_T The protobuf-generated service type.
 */
template <typename PROTO_SERVICE_T>
class Client : public std::enable_shared_from_this<Client<PROTO_SERVICE_T>> {
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
      : loop_{loop}, discovery_{discovery}, callback_handle_{discovery::Discovery::kInvalidCallbackHandle} {
    // Registered from the constructor body, not the member-initializer list. This callback reads members declared
    // after callback_handle_ -- tcp_client_ among them -- and discovery can deliver to it on the loop thread as soon
    // as it is registered, which a loopback instance does by replaying the registrations it already holds. From the
    // body every member is constructed before the first delivery can arrive; from the initializer list tcp_client_
    // was still uninitialized storage, and a delivery that dereferenced it segfaulted.
    callback_handle_ = discovery_->AsyncReceiveServices([this, loop](discovery::Discovery::EventType event,
                                                                     const discovery::Sample& sample) {
      if (sample.service().sname() == PROTO_SERVICE_T::descriptor()->full_name()) {
        const auto tcp_port = sample.service().tcp_port();
        if (event == discovery::Discovery::EventType::kNewUnregistration) {
          if (tcp_client_) {
            trellis::core::Log::Info(
                "Received unregistration for service {} server on port {}, removing TCP connection",
                PROTO_SERVICE_T::descriptor()->full_name(), tcp_port);
            const auto maybe_remote_port = tcp_client_->GetRemotePort();
            if (!maybe_remote_port.has_value() || maybe_remote_port.value() == tcp_port) {
              DropConnection();
            }
          }
        } else if (event == discovery::Discovery::EventType::kNewRegistration) {
          if (!tcp_client_) {
            trellis::core::Log::Info("Received registration for service {} server on port {}, creating TCP connection",
                                     PROTO_SERVICE_T::descriptor()->full_name(), tcp_port);
            tcp_client_ = std::make_shared<network::TCP>(loop, "127.0.0.1", tcp_port);
          } else {
            // if for some reason tcp_client_ is set already, but on the wrong port
            const auto maybe_remote_port = tcp_client_->GetRemotePort();
            if (maybe_remote_port.has_value() && maybe_remote_port.value() != tcp_port) {
              trellis::core::Log::Info(
                  "Received registration for service {} server on port {}, updating existing TCP connection",
                  PROTO_SERVICE_T::descriptor()->full_name(), tcp_port);
              DropConnection();
              tcp_client_ = std::make_shared<network::TCP>(loop, "127.0.0.1", tcp_port);
            }
          }
        }
      }
    });
  }

  /**
   * @brief Destructor that stops receiving service discovery events and fails any outstanding call.
   */
  ~Client() {
    discovery_->StopReceive(callback_handle_);

    // Drop the connection before running a single callback. CallAsync() and ProcessNextRequest() both refuse to
    // start work without one, so a callback below that issues another call fails inline instead of reaching
    // shared_from_this(), which throws once destruction has begun and would terminate from a destructor.
    if (tcp_client_) {
      DropConnection();
    }

    // A user callback that throws here would escape the destructor and terminate the process.
    const auto fail = [](const std::shared_ptr<QueuedRequest>& request) {
      try {
        request->failure_fn();
      } catch (const std::exception& e) {
        trellis::core::Log::Error("Callback for service {} threw while failing a call during client destruction: {}",
                                  PROTO_SERVICE_T::descriptor()->full_name(), e.what());
      } catch (...) {
        trellis::core::Log::Error(
            "Callback for service {} threw a non-std::exception while failing a call during client destruction",
            PROTO_SERVICE_T::descriptor()->full_name());
      }
    };

    // Swap the queue out so nothing can extend the drain loop, then fail everything still outstanding.
    std::queue<std::shared_ptr<QueuedRequest>> queued;
    queued.swap(queued_requests_);
    if (pending_request_) {
      const auto request = pending_request_;  // failure_fn() resets pending_request_
      fail(request);
    }
    CleanPendingRequest();
    while (!queued.empty()) {
      const auto request = queued.front();
      queued.pop();
      fail(request);
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
   * @param timeout_ms Timeout in milliseconds; 0 means the call waits indefinitely for a response.
   */
  template <typename REQ_T, typename RESP_T>
  void CallAsync(std::string_view method, REQ_T request, ResponseCallback<RESP_T> callback, unsigned timeout_ms = 0) {
    if (!tcp_client_) {
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
          FinishPendingRequest();
        },
        [this, callback]() {  // Handle failure
          RESP_T resp{};
          callback(kFailure, &resp);
          FinishPendingRequest();
        },
        [this, callback]() {  // Handle timeout
          RESP_T resp{};
          callback(kTimedOut, &resp);
          if (this->tcp_client_) {
            trellis::core::error_code ec;
            this->tcp_client_->Cancel(ec);  // Cancel the TCP client to avoid further processing
          }
          FinishPendingRequest();
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

  // Closing the socket lets the in-flight request fail on the event loop. Discovery calls us holding its callback
  // lock, so user callbacks must not run here, and an exception would take down Node::Run().
  void DropConnection() {
    trellis::core::error_code ec;
    tcp_client_->Close(ec);
    if (ec) {
      trellis::core::Log::Warn("Failed to close TCP connection for service {}: {}",
                               PROTO_SERVICE_T::descriptor()->full_name(), ec.message());
    }
    tcp_client_.reset();
  }

  void CleanPendingRequest() {
    if (pending_timer_) {
      pending_timer_->Stop();
    }
    pending_request_.reset();
  }

  // Hands the next request to a later turn of the loop rather than starting it here. Completing a request can
  // complete the one behind it -- a queue draining against a dropped connection fails every entry in turn -- and
  // going through the loop keeps that cascade from nesting one request's start inside the previous one's completion.
  void ScheduleProcessNext() {
    // ~Client() reaches here while failing its outstanding requests, by which point shared_from_this() throws.
    const std::weak_ptr<Client> weak_self = this->weak_from_this();
    asio::post(*loop_, [weak_self]() {
      if (auto self = weak_self.lock()) {
        self->ProcessNextRequest();
      }
    });
  }

  // Call when the pending request is done, whatever its outcome. Clears it and lets the next one start.
  void FinishPendingRequest() {
    CleanPendingRequest();
    ScheduleProcessNext();
  }

  // Triggered by a new request or by the completion of the previous one
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

    if (!tcp_client_) {
      // Nothing can complete this request without a connection. Failing it schedules the next, which fails too.
      auto request = pending_request_;  // failure_fn() resets pending_request_
      request->failure_fn();
      return;
    }

    // Generate our header payload which contains the size
    const uint32_t size = pending_request_->request_buffer->size();
    auto send_header_buf = std::make_shared<std::array<uint8_t, sizeof(size)>>();
    memcpy(send_header_buf.get(), &size, sizeof(size));

    // Each handler needs the socket and request it started on: tcp_client_ may be gone or replaced by the time it
    // runs. A client not owned by a shared_ptr throws here rather than silently dropping every handler.
    const std::weak_ptr<Client> weak_self = this->shared_from_this();
    const auto request = pending_request_;
    const auto tcp = tcp_client_;

    // Start the timeout timer for this request
    if (request->timeout_ms > 0) {
      // Stop() cannot recall a completion asio has already queued, so the handler may run after ~Client() and must
      // not touch a raw this. weak_request keeps the timer from holding a completed request alive.
      std::weak_ptr<QueuedRequest> weak_request = request;
      pending_timer_ = std::make_shared<OneShotTimerImpl>(
          loop_,
          [weak_self, weak_request](const time::TimePoint&) {
            const auto self = weak_self.lock();
            const auto timed_out = weak_request.lock();
            if (self && timed_out && self->pending_request_ == timed_out) {
              timed_out->timeout_fn();
            }
          },
          request->timeout_ms, TimerKind::kManagement);
    }

    // Drain any stale data from the receive buffer immediately before sending.
    // This handles cases where a previous request timed out but the server eventually
    // sent a response that is still sitting in the socket buffer.
    // TODO (bsirang) We should implement request/response ID tracking to avoid this situation entirely.
    tcp->DrainReceiveBuffer();

    // We chain together 4 events:
    // 1. Send 4-byte request payload size
    // 2. Send request payload
    // 3. Receive 4-byte response payload size
    // 4. Receive response payload
    tcp->AsyncSendAll(
        send_header_buf->data(), send_header_buf->size(),
        [weak_self, request, tcp, send_header_buf](const trellis::core::error_code& ec, size_t bytes_sent) {
          auto self = weak_self.lock();
          if (!self || self->pending_request_ != request) {
            return;  // client gone, or this request already completed
          } else if (ec) {
            request->failure_fn();  // operation_aborted lands here too: a closed socket has to fail the request
            return;
          }
          // We sent the 4-byte length to the server, now let's send the actual payload
          tcp->AsyncSendAll(request->request_buffer->data(), request->request_buffer->size(),
                            [weak_self, request, tcp](const trellis::core::error_code& ec, size_t bytes_sent) {
                              auto self = weak_self.lock();
                              if (!self || self->pending_request_ != request) {
                                return;
                              } else if (ec) {
                                request->failure_fn();
                                return;
                              }
                              // We sent the payload to the server, now let's receive the 4-byte length from the server
                              auto receive_header_buf = std::make_shared<std::array<uint8_t, sizeof(uint32_t)>>();
                              tcp->AsyncReceiveAll(
                                  receive_header_buf->data(), receive_header_buf->size(),
                                  [weak_self, request, tcp, receive_header_buf](const trellis::core::error_code& ec,
                                                                                size_t /*bytes_received*/) {
                                    auto self = weak_self.lock();
                                    if (!self || self->pending_request_ != request) {
                                      return;
                                    } else if (ec) {
                                      request->failure_fn();
                                      return;
                                    }

                                    // Since performance is not critical for RPCs, and because we don't know the receive
                                    // payload size ahead of time, we'll dynamically allocate the buffer size. The
                                    // protocol does not enforce any specific limit on payload size beyond the 32-bit
                                    // length field.
                                    const uint32_t length = *reinterpret_cast<uint32_t*>(receive_header_buf->data());
                                    auto receive_buffer = std::make_shared<std::vector<uint8_t>>(length);
                                    // tcp is captured to keep the socket alive: AsyncReceiveAll continues partial
                                    // reads through it.
                                    tcp->AsyncReceiveAll(
                                        receive_buffer->data(), receive_buffer->size(),
                                        [weak_self, request, tcp, receive_buffer](const trellis::core::error_code& ec,
                                                                                  size_t bytes_received) {
                                          auto self = weak_self.lock();
                                          if (!self || self->pending_request_ != request) {
                                            return;
                                          } else if (ec) {
                                            request->failure_fn();
                                            return;
                                          }

                                          discovery::Response response;
                                          response.ParseFromArray(receive_buffer->data(), bytes_received);
                                          if (response.header().status() == discovery::ServiceHeader::failed) {
                                            request->failure_fn();
                                          } else {
                                            request->success_fn(response);
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
  std::shared_ptr<network::TCP> tcp_client_;                    ///< Active TCP client, if connected
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
