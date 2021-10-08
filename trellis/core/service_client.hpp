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

#ifndef TRELLIS_CORE_SERVICE_CLIENT_HPP
#define TRELLIS_CORE_SERVICE_CLIENT_HPP

#include <ecal/msg/protobuf/client.h>

#include <string>

namespace trellis {
namespace core {

template <typename RPC_T>
class ServiceClientImpl {
 public:
  template <typename RESP_T>
  using Callback = std::function<void(const RESP_T*)>;
  ServiceClientImpl() {}

  template <typename REQ_T, typename RESP_T>
  void CallAsync(const std::string& method_name, const REQ_T& req, Callback<RESP_T> cb) {
    auto callback_wrapper = [cb](const struct eCAL::SServiceInfo& service_info, const std::string& response) {
      if (service_info.call_state != call_state_executed) {
        if (cb) cb(nullptr);
      } else {
        RESP_T resp;
        resp.ParseFromString(response);
        if (cb) cb(&resp);
      }
    };
    client_.AddResponseCallback(callback_wrapper);
    client_.CallAsync(method_name, req);
  }

  // TODO(bsirang) implement sync call

 private:
  eCAL::protobuf::CServiceClient<RPC_T> client_;
};

template <typename RPC_T>
using ServiceClient = std::shared_ptr<ServiceClientImpl<RPC_T>>;

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_SERVICE_CLIENT_HPP
