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

#ifndef TRELLIS_CORE_IPC_PROTO_RPC_SERVER_HPP_
#define TRELLIS_CORE_IPC_PROTO_RPC_SERVER_HPP_

#include <google/protobuf/descriptor.h>

#include <memory>
#include <optional>
#include <thread>

#include "trellis/core/discovery/discovery.hpp"
#include "trellis/core/event_loop.hpp"
#include "trellis/core/ipc/proto/rpc/types.hpp"
#include "trellis/network/tcp.hpp"

namespace trellis::core::ipc::proto::rpc {

/**
 * @brief Generic gRPC-style RPC server using protobuf and TCP.
 *
 * Handlers run one at a time on a thread this server owns, not on the event loop. A handler may call out to another
 * service. It must not block waiting on a reply from this one: the handler thread is already inside the outer call,
 * so the nested request is never dispatched. ~Server() then joins that thread with no timeout, hanging shutdown.
 *
 * ~Server() drops the handlers that have not started and waits for the running one. The running handler keeps the
 * service implementation alive and the handler thread keeps the loop, but nothing else: a service must own whatever
 * its handlers use. The handler hands the service to the loop with its reply, so a last owner releases it on the
 * loop's thread, or in place once the loop is stopped, as the service's members (a Client, say) require. Stopping does
 * not wait for the loop's current callback, which may still be using what the service owns. A reply the loop never
 * runs (it stops after the handler checks, or is never run again) keeps the service until the loop is destroyed, and
 * for good if the service holds the loop.
 *
 * Replies go out on the event loop, so a reply still owed when the loop stops is lost. Shutdown must destroy the
 * Server, not merely stop the loop. ~Server() closes the caller sockets, and nothing else tells a caller in another
 * process to give up: one left with an open socket sees no reply and no error, and waits forever if it passed
 * timeout_ms = 0.
 *
 * @tparam PROTO_SERVICE_T The generated protobuf service class to serve.
 */
template <typename PROTO_SERVICE_T>
class Server {
 public:
  using TCPClientPointer = std::shared_ptr<network::TCP>;

  /**
   * @brief Construct a new Server instance.
   *
   * @param prototype Shared pointer to a protobuf service implementation.
   * @param loop The event loop used for TCP server binding.
   * @param discovery Shared pointer to service discovery instance.
   */
  Server(std::shared_ptr<PROTO_SERVICE_T> prototype, trellis::core::EventLoop loop, discovery::DiscoveryPtr discovery)
      : loop_{loop},
        rpc_thread_([handler_loop = std::optional{handler_loop_}, loop]() mutable {
          handler_loop->Run();
          handler_loop.reset();  // before the loop: the handlers it drops own sockets on the loop
        }),
        prototype_{prototype},
        discovery_{std::move(discovery)},
        tcp_server_{loop, /* port = */ 0, [this](const trellis::core::error_code& ec, network::TCP socket) mutable {
                      if (ec) {
                        return;
                      }
                      auto client = std::make_shared<network::TCP>(std::move(socket));
                      clients_.emplace_back(client);
                      ReceiveNextRequest(client);
                    }} {
    // Registered here, not in the member-initializer list. Registering publishes our port, and a loopback Discovery
    // hands it to clients on the loop thread at once. A client can connect, and the accept handler above can run,
    // before any member later in that list exists.
    discovery_handle_ = discovery_->RegisterServiceServer(std::string{PROTO_SERVICE_T::descriptor()->full_name()},
                                                          tcp_server_.GetPort(), methods_);
  }

  /**
   * @brief Destroy the Server instance and clean up clients and threads.
   */
  ~Server() {
    discovery_->Unregister(discovery_handle_);
    for (auto& client : clients_) {
      if (auto shared = client.lock()) {
        shared->Cancel();
        shared->Close();
      }
    }
    // Stopped rather than left to run out of work: rpc_thread_ holds its own copy of the loop, which keeps it busy.
    handler_loop_.Stop();  // requests not yet started have no socket left to answer on
    if (rpc_thread_.joinable()) {
      rpc_thread_.join();
    }
  }

 private:
  /**
   * @brief Extract method metadata from a protobuf service implementation.
   *
   * @param service Reference to the protobuf service.
   * @return Map from method name to metadata.
   */
  static MethodsMap GetMethodsFromService(PROTO_SERVICE_T& service) {
    MethodsMap map;
    const google::protobuf::ServiceDescriptor* service_descriptor = service.GetDescriptor();
    google::protobuf::MessageFactory* factory = google::protobuf::MessageFactory::generated_factory();

    for (int i = 0; i < service_descriptor->method_count(); ++i) {
      const google::protobuf::MethodDescriptor* method_descriptor = service_descriptor->method(i);
      std::string method_name{method_descriptor->name()};

      const google::protobuf::Descriptor* input_desc = method_descriptor->input_type();
      const google::protobuf::Descriptor* output_desc = method_descriptor->output_type();

      const google::protobuf::Message* input_prototype = factory->GetPrototype(input_desc);
      const google::protobuf::Message* output_prototype = factory->GetPrototype(output_desc);

      std::string input_type_desc =
          input_prototype ? discovery::utils::GetProtoMessageDescription(*input_prototype) : "";
      std::string output_type_desc =
          output_prototype ? discovery::utils::GetProtoMessageDescription(*output_prototype) : "";

      map.emplace(std::make_pair(method_name, MethodMetadata{.descriptor = method_descriptor,
                                                             .input_type_name = std::string{input_desc->name()},
                                                             .input_type_desc = std::move(input_type_desc),
                                                             .output_type_name = std::string{output_desc->name()},
                                                             .output_type_desc = std::move(output_type_desc)}));
    }

    return map;
  }

  /**
   * @brief Begin asynchronous receive loop for the given client.
   *
   * @param client Shared pointer to the TCP client.
   */
  void ReceiveNextRequest(TCPClientPointer client) {
    // We chain together two receive attempts
    // 1. Receive 4-byte request payload size
    // 2. Receive request payload
    auto receive_header_buf = std::make_shared<std::array<uint8_t, sizeof(uint32_t)>>();
    client->AsyncReceiveAll(receive_header_buf->data(), receive_header_buf->size(),
                            [this, receive_header_buf, client](const trellis::core::error_code& ec, size_t len) {
                              if (ec) {
                                return;  // nothing to do, this socket will get destructed
                              }
                              const uint32_t length = *reinterpret_cast<uint32_t*>(receive_header_buf->data());
                              auto receive_buffer = std::make_shared<std::vector<uint8_t>>(length);
                              client->AsyncReceiveAll(
                                  receive_buffer->data(), receive_buffer->size(),
                                  [this, receive_buffer, client](const trellis::core::error_code& ec, size_t len) {
                                    if (ec) {
                                      return;  // nothing to do, this socket will get destructed
                                    }
                                    ProcessRequest(client, receive_buffer->data(), len);
                                    ReceiveNextRequest(client);
                                  });
                            });
  }

  /**
   * @brief Process a received request and invoke the appropriate protobuf method.
   *
   * @param client Shared pointer to the TCP client.
   * @param data Pointer to raw request data.
   * @param len Length of the data in bytes.
   */
  void ProcessRequest(TCPClientPointer client, void* data, size_t len) {
    discovery::Request req;
    req.ParseFromArray(data, len);

    // After parsing the request, we perform the remaining work on the background thread. The loop's io_context outlives
    // this, since the handler thread holds the loop.
    asio::post(*handler_loop_, [service = std::weak_ptr<PROTO_SERVICE_T>{prototype_}, loop_context = &*loop_, client,
                                req = std::move(req)]() {
      // Owned while this runs, so a Server destroyed meanwhile does not take the service with it.
      auto prototype = service.lock();
      if (!prototype) {
        return;
      }
      const auto& method_name = req.header().mname();
      const google::protobuf::ServiceDescriptor* service_desc = prototype->GetDescriptor();
      const google::protobuf::MethodDescriptor* method_desc = service_desc->FindMethodByName(method_name);
      discovery::Response response;
      response.mutable_header()->set_mname(method_name);

      if (!method_desc) {
        response.mutable_header()->set_status(discovery::ServiceHeader::failed);
        response.mutable_header()->set_error("method not found");
      } else {
        std::unique_ptr<google::protobuf::Message> rpc_request(prototype->GetRequestPrototype(method_desc).New());
        std::unique_ptr<google::protobuf::Message> rpc_response(prototype->GetResponsePrototype(method_desc).New());
        rpc_request->ParseFromString(req.request());
        prototype->CallMethod(method_desc, nullptr, rpc_request.get(), rpc_response.get(), nullptr);
        rpc_response->SerializeToString(response.mutable_response());
        response.mutable_header()->set_status(discovery::ServiceHeader::executed);
      }

      // We use a shared pointer so we can copy the reference into the lambda while simultaneously
      // accessing the data pointer when calling AsyncSend
      auto send_buffer = std::make_shared<std::string>();
      response.SerializeToString(send_buffer.get());
      uint32_t size = send_buffer->size();
      auto send_header_buf = std::make_shared<std::array<uint8_t, sizeof(size)>>();
      memcpy(send_header_buf.get(), &size, sizeof(size));

      // The service goes to the loop with the reply, so that if this is its last owner, it is released on the loop's
      // thread. A stopped loop runs nothing more, and releasing it here is then allowed.
      if (loop_context->stopped()) {
        return;
      }

      // This runs on the RPC thread, but the socket belongs to the event loop, which is already waiting on it for
      // the next request. Post the send to the socket's own executor so operations are always started from the
      // thread that owns it. Two replies must still never overlap on one socket, since AsyncSendAll chains partial
      // sends; rpc::Client keeps one call in flight per socket, so they cannot.
      // We chain together two send attempts
      // 1. Send 4-byte response payload size
      // 2. Send response payload
      asio::post(client->GetExecutor(), [client, send_header_buf, send_buffer, prototype = std::move(prototype)]() {
        client->AsyncSendAll(send_header_buf->data(), send_header_buf->size(),
                             [send_header_buf, send_buffer, client](const trellis::core::error_code&, size_t) {
                               client->AsyncSendAll(send_buffer->data(), send_buffer->size(),
                                                    [client, send_buffer](const trellis::core::error_code&, size_t) {});
                             });
      });
    });
  }

  // loop_ before handler_loop_, so the handlers it drops release their sockets while the loop still exists.
  trellis::core::EventLoop loop_;  ///< Loop the sockets belong to
  /// Where handlers run. Shared with the handler thread, which holds what it uses instead of reaching into the server.
  /// It has no timer registry, so a timer built on it would go untracked.
  trellis::core::EventLoop handler_loop_;
  std::thread rpc_thread_;                        ///< Thread running the RPC handler event loop
  std::shared_ptr<PROTO_SERVICE_T> prototype_{};  ///< Protobuf service instance
  discovery::DiscoveryPtr discovery_;             ///< Discovery system for registering the service
  // clients_ before tcp_server_: the accept handler writes here, and can fire as soon as tcp_server_ is built.
  std::deque<std::weak_ptr<network::TCP>> clients_{};             ///< Active client connections
  network::TCPServer tcp_server_;                                 ///< TCP server for incoming RPC connections
  MethodsMap methods_{GetMethodsFromService(*prototype_.get())};  ///< Map of method metadata
  discovery::Discovery::RegistrationHandle discovery_handle_{
      discovery::Discovery::kInvalidRegistrationHandle};  ///< Handle for unregistering service
};

}  // namespace trellis::core::ipc::proto::rpc

namespace trellis::core {

/**
 * @brief Alias for backwards compatibility with legacy Trellis code.
 *
 * @tparam PROTO_MSG_T The protobuf service type.
 */
template <typename PROTO_MSG_T>
using ServiceServer = std::shared_ptr<ipc::proto::rpc::Server<PROTO_MSG_T>>;

}  // namespace trellis::core

#endif  // TRELLIS_CORE_IPC_PROTO_RPC_SERVER_HPP_
