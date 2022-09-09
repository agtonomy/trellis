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

  ServiceClientImpl(EventLoop loop)
      : ev_loop_{loop},
        client_{std::make_unique<eCAL::protobuf::CServiceClient<RPC_T>>()},
        priv_ev_loop_{trellis::core::CreateEventLoop()},
        work_guard_{asio::make_work_guard(*priv_ev_loop_)},
        async_thread_{[this]() { priv_ev_loop_->run(); }} {}

  ~ServiceClientImpl() {
    if (priv_ev_loop_) {
      priv_ev_loop_->stop();
    }
    if (async_thread_.joinable()) {
      async_thread_.join();
    }
  }

  template <typename REQ_T, typename RESP_T>
  void CallAsync(const std::string& method_name, const REQ_T& req, Callback<RESP_T> cb, unsigned timeout_ms = 0) {
    // XXX(bsirang): look into eliiminating the copy of `req` here
    asio::post(*priv_ev_loop_, [this, cb, method_name, req, timeout_ms]() {
      if (!client_->IsConnected()) {
        asio::post(*priv_ev_loop_, [cb]() {
          if (cb) cb(kFailure, nullptr);
        });
        return;
      }
      eCAL::ServiceResponseVecT service_response_vec;
      int temp_timeout = timeout_ms == 0 ? -1 : static_cast<int>(timeout_ms);
      const bool success = client_->Call(method_name, req, temp_timeout, &service_response_vec);
      if (success) {
        for (const auto& service_response : service_response_vec) {
          const ServiceCallStatus status = (service_response.call_state != call_state_executed) ? kFailure : kSuccess;
          RESP_T resp{};
          if (status == kSuccess) {
            resp.ParseFromString(service_response.response);
          }
          // Invoke callback from event loop thread...
          // XXX(bsirang): look into eliiminating the copy of `resp` here
          asio::post(*priv_ev_loop_, [status, cb, resp]() {
            if (cb) cb(status, &resp);
          });
        }
      } else {
        asio::post(*priv_ev_loop_, [cb]() {
          if (cb) cb(kTimedOut, nullptr);
        });
      }
    });
  }

  // TODO(bsirang) implement sync call

 private:
  EventLoop ev_loop_;
  std::unique_ptr<eCAL::protobuf::CServiceClient<RPC_T>> client_;
  EventLoop priv_ev_loop_;
  asio::executor_work_guard<typename asio::io_context::executor_type> work_guard_;
  std::thread async_thread_;
};

template <typename RPC_T>
using ServiceClient = std::shared_ptr<ServiceClientImpl<RPC_T>>;

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_SERVICE_CLIENT_HPP
