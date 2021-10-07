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

#ifndef TRELLIS_CORE_SERVICE_SERVER_HPP
#define TRELLIS_CORE_SERVICE_SERVER_HPP

#include <ecal/msg/protobuf/server.h>
#include <google/protobuf/service.h>

#include <functional>
#include <string>

namespace trellis {
namespace core {
namespace {

/**
 * ServiceHandler A templated subclass of a protobuf service class, which
 * overrides a "Call" method. This is useful for proto services with a single
 * rpc method called "Call".
 * e.g.
 * service FooService
 * {
 *  rpc Call (InputMessageType) returns (OutputMessageType);
 * }
 *
 * @see CreateServiceServer()
 */
template <typename SERVICE_T, typename REQ_T, typename RESP_T>
class ServiceHandler : public SERVICE_T {
 public:
  using HandlerFunction = std::function<void(const REQ_T&, RESP_T&)>;
  ServiceHandler(HandlerFunction handler) : handler_{handler} {}

  void Call(::google::protobuf::RpcController* /* controller_ */, const REQ_T* request_, RESP_T* response_,
            ::google::protobuf::Closure* /* done_ */) override {
    if (handler_) {
      handler_(*request_, *response_);
    }
  }

 private:
  HandlerFunction handler_;
};
}  // namespace

template <typename SERVICE_T>
using ServiceServerClass = eCAL::protobuf::CServiceServer<SERVICE_T>;

template <typename SERVICE_T>
using ServiceServer = std::shared_ptr<ServiceServerClass<SERVICE_T>>;

/**
 * CreateServiceServer create an instance of the ServiceHandler class.
 * Needs to specify service type, request type, and response type.
 */
template <typename SERVICE_T, typename REQ_T, typename RESP_T>
ServiceServer<SERVICE_T> CreateServiceServer(
    typename ServiceHandler<SERVICE_T, REQ_T, RESP_T>::HandlerFunction handle_fn) {
  return ServiceServer<SERVICE_T>(std::make_shared<ServiceHandler<SERVICE_T, REQ_T, RESP_T>>(handle_fn));
}
}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_SERVICE_SERVER_HPP
