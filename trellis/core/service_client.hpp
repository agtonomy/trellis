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

#include "event_loop.hpp"
#include "timer.hpp"

namespace trellis {
namespace core {

enum ServiceCallStatus { kTimedOut = 0, kSuccess = 1, kFailure = 2 };

template <typename RPC_T>
class ServiceClientImpl {
 public:
  template <typename RESP_T>
  using Callback = std::function<void(ServiceCallStatus, const RESP_T*)>;
  ServiceClientImpl(EventLoop loop) : ev_loop_{loop} {}

  template <typename REQ_T, typename RESP_T>
  void CallAsync(const std::string& method_name, const REQ_T& req, Callback<RESP_T> cb, unsigned timeout_ms = 0) {
    Timer timeout_timer{nullptr};
    auto client = std::make_shared<eCAL::protobuf::CServiceClient<RPC_T>>();
    if (timeout_ms != 0) {
      timeout_timer = std::make_shared<TimerImpl>(
          ev_loop_, TimerImpl::Type::kOneShot,
          [cb, client]() mutable {
            // XXX(bsirang): ideally we'd be able to abort the pending async operation
            // here given that we've timed out. However, there doesn't seem to be an API for
            // that in eCAL's service client. Instead we'll check for an expired timer
            // in the client callback.
            if (cb) cb(kTimedOut, nullptr);
          },
          0, timeout_ms);
    }

    auto loop = ev_loop_;
    client->AddResponseCallback(
        [timeout_timer, cb, loop, client](const struct eCAL::SServiceInfo& service_info, const std::string& response) {
          if (timeout_timer) {
            if (timeout_timer->Expired()) {
              // We've already timed out, bail now even if we have a response
              // since we've already called back to the user.
              return;
            }
            timeout_timer->Stop();
          }
          RESP_T resp;
          const ServiceCallStatus status = (service_info.call_state != call_state_executed) ? kFailure : kSuccess;
          if (status == kSuccess) {
            resp.ParseFromString(response);
          }
          // Invoke callback from event loop thread...
          // XXX(bsirang): look into eliiminating the copy of `resp` here
          asio::post(*loop, [status, cb, resp]() {
            if (cb) cb(status, &resp);
          });
        });
    client->CallAsync(method_name, req);
  }

  // TODO(bsirang) implement sync call

 private:
  EventLoop ev_loop_;
};

template <typename RPC_T>
using ServiceClient = std::shared_ptr<ServiceClientImpl<RPC_T>>;

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_SERVICE_CLIENT_HPP
