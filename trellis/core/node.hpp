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

#ifndef TRELLIS_CORE_NODE_HPP
#define TRELLIS_CORE_NODE_HPP

#include <ecal/ecal.h>

#include <asio.hpp>
#include <functional>
#include <string>

#include "bind.hpp"
#include "config.hpp"
#include "event_loop.hpp"
#include "logging.hpp"
#include "publisher.hpp"
#include "service_client.hpp"
#include "service_server.hpp"
#include "subscriber.hpp"
#include "time.hpp"
#include "timer.hpp"

namespace trellis {
namespace core {

class Node {
 public:
  Node(std::string name);

  // Moving/copying not allowed
  Node(const Node&) = delete;
  Node(Node&&) = delete;
  Node& operator=(const Node&) = delete;
  Node& operator=(Node&&) = delete;

  template <typename T>
  Publisher<T> CreatePublisher(std::string topic) const {
    return std::make_shared<PublisherClass<T>>(topic.c_str());
  }

  DynamicPublisher CreateDynamicPublisher(std::string topic, std::shared_ptr<google::protobuf::Message> msg) {
    return std::make_shared<DynamicPublisherImpl>(topic, msg);
  }

  DynamicPublisher CreateDynamicPublisher(std::string topic, std::string proto_type_name) {
    return std::make_shared<DynamicPublisherImpl>(topic, proto_type_name);
  }

  template <typename T>
  Subscriber<T> CreateSubscriber(std::string topic, std::function<void(const T&)> callback) const {
    return std::make_shared<SubscriberImpl<T>>(topic.c_str(), callback);
  }

  DynamicSubscriber CreateDynamicSubscriber(std::string topic,
                                            std::function<void(const google::protobuf::Message&)> callback) {
    DynamicSubscriber subscriber = std::make_shared<DynamicSubscriberClass>(topic);
    // TODO(bsirang) consider passing time_ and clock_ to user
    auto callback_wrapper = [callback](const char* topic_name_, const google::protobuf::Message& msg_,
                                       long long time_) { callback(msg_); };
    subscriber->AddReceiveCallback(callback_wrapper);
    return subscriber;
  }

  template <typename RPC_T>
  ServiceClient<RPC_T> CreateServiceClient() const {
    return std::make_shared<ServiceClientImpl<RPC_T>>();
  }

  template <typename RPC_T>
  ServiceServer<RPC_T> CreateServiceServer(std::shared_ptr<RPC_T> rpc) const {
    return std::make_shared<ServiceServerClass<RPC_T>>(rpc);
  }

  Timer CreateTimer(unsigned interval_ms, TimerImpl::Callback callback, unsigned initial_delay_ms = 0) const;

  Timer CreateOneShotTimer(unsigned initial_delay_ms, TimerImpl::Callback callback) const;

  int Run();

  bool RunOnce();

  EventLoop GetEventLoop() const { return ev_loop_; }

 private:
  const std::string name_;
  EventLoop ev_loop_;
};

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_NODE_HPP
