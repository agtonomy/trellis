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

#include <optional>

#include "trellis/core/discovery/discovery.hpp"
#include "trellis/core/event_loop.hpp"
#include "trellis/core/ipc/proto/rpc/types.hpp"
#include "trellis/network/tcp.hpp"

namespace trellis::core::ipc::proto::rpc {

namespace {

/**
 * @brief Thread to run the RPC work on.
 *
 * @param io_context Reference to the ASIO I/O context.
 */
void ProcessingThread(asio::io_context& io_context) {
  io_context.run();  // This runs the event loop for this thread
}

}  // namespace

/**
 * @brief Generic gRPC-style RPC server using protobuf and TCP.
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
      : work_guard_{asio::make_work_guard(io_context_)},
        rpc_thread_(ProcessingThread, std::ref(io_context_)),
        prototype_{prototype},
        discovery_{discovery},
        tcp_server_{loop, /* port = */ 0,
                    [this](const trellis::core::error_code& ec, network::TCP socket) mutable {
                      if (ec) {
                        return;
                      }
                      auto client = std::make_shared<network::TCP>(std::move(socket));
                      clients_.emplace_back(client);
                      ReceiveNextRequest(client);
                    }},
        discovery_handle_{
            discovery_->RegisterServiceServer(PROTO_SERVICE_T::descriptor()->full_name(), tcp_server_.GetPort())} {}

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
    work_guard_.reset();
    if (rpc_thread_.joinable()) {
      rpc_thread_.join();
    }
  }

 private:
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
      const std::string method_name = method_descriptor->name();

      const google::protobuf::Descriptor* input_desc = method_descriptor->input_type();
      const google::protobuf::Descriptor* output_desc = method_descriptor->output_type();

      const google::protobuf::Message* input_prototype = factory->GetPrototype(input_desc);
      const google::protobuf::Message* output_prototype = factory->GetPrototype(output_desc);

      std::string input_type_desc =
          input_prototype ? discovery::utils::GetProtoMessageDescription(*input_prototype) : "";
      std::string output_type_desc =
          output_prototype ? discovery::utils::GetProtoMessageDescription(*output_prototype) : "";

      map.emplace(std::make_pair(method_name, MethodMetadata{.descriptor = method_descriptor,
                                                             .input_type_name = input_desc->name(),
                                                             .input_type_desc = std::move(input_type_desc),
                                                             .output_type_name = output_desc->name(),
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
    client->AsyncReceive(buffer_.data(), buffer_.size(),
                         [this, client](const trellis::core::error_code& ec, size_t len) {
                           if (ec) {
                             return;  // nothing to do, this socket will get destructed
                           }
                           ProcessRequest(client, buffer_.data(), len);
                           ReceiveNextRequest(client);
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
    asio::post(io_context_, [this, client, req = std::move(req)]() {
      const auto& method_name = req.header().mname();
      const google::protobuf::ServiceDescriptor* service_desc = prototype_->GetDescriptor();
      const google::protobuf::MethodDescriptor* method_desc = service_desc->FindMethodByName(method_name);
      discovery::Response response;
      response.mutable_header()->set_mname(method_name);

      if (!method_desc) {
        response.mutable_header()->set_status(discovery::ServiceHeader::failed);
        response.mutable_header()->set_error("method not found");
      } else {
        std::unique_ptr<google::protobuf::Message> rpc_request(prototype_->GetRequestPrototype(method_desc).New());
        std::unique_ptr<google::protobuf::Message> rpc_response(prototype_->GetResponsePrototype(method_desc).New());
        rpc_request->ParseFromString(req.request());
        prototype_->CallMethod(method_desc, nullptr, rpc_request.get(), rpc_response.get(), nullptr);
        rpc_response->SerializeToString(response.mutable_response());
        response.mutable_header()->set_status(discovery::ServiceHeader::executed);
      }

      // We use a shared pointer so we can copy the reference into the lambda while simultaneously
      // accessing the data pointer when calling AsyncSend
      auto send_buffer = std::make_shared<std::string>();
      response.SerializeToString(send_buffer.get());
      if (send_buffer->size() > kMaxBufferSize) {
        throw std::runtime_error("rpc::Server::ProcessRequest - response size too large!");
      }
      client->AsyncSend(send_buffer->data(), send_buffer->size(),
                        [client, send_buffer](const trellis::core::error_code& ec, size_t bytes_sent) {});
    });
  }

  asio::io_context io_context_{};  ///< Background thread context for method execution
  asio::executor_work_guard<asio::io_context::executor_type> work_guard_;  ///< Keeps io_context alive
  std::thread rpc_thread_;                                        ///< Thread running the RPC handler event loop
  std::array<uint8_t, 65535> buffer_{};                           ///< Receive buffer for incoming messages
  std::shared_ptr<PROTO_SERVICE_T> prototype_{};                  ///< Protobuf service instance
  discovery::DiscoveryPtr discovery_;                             ///< Discovery system for registering the service
  network::TCPServer tcp_server_;                                 ///< TCP server for incoming RPC connections
  MethodsMap methods_{GetMethodsFromService(*prototype_.get())};  ///< Map of method metadata
  discovery::Discovery::RegistrationHandle discovery_handle_;     ///< Handle for unregistering service
  std::deque<std::weak_ptr<network::TCP>> clients_{};             ///< Active client connections
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
