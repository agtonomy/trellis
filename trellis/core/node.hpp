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

/**
 * Node A class to represent each actor in the actor pattern
 *
 * A single instance of this class is intended to be used by each Trellis
 * application. It provides methods for constructing the various Trellis
 * primitives. Examples include pub/sub handles and timers.
 *
 * The lifecycle of this class should be coupled to the lifecycle of the
 * application using it. Each instance of this class manages the underlying
 * threads that drive the IPC and asynchronous IO.
 */
class Node {
 public:
  /**
   * Node Construct an instance
   *
   * @param name the name of the application this instance represents
   */
  Node(std::string name);
  ~Node();

  // Moving/copying not allowed
  Node(const Node&) = delete;
  Node(Node&&) = delete;
  Node& operator=(const Node&) = delete;
  Node& operator=(Node&&) = delete;

  /**
   * CreatePublisher create a new handle for a publisher
   *
   * @tparam T the message type that will be published by this handle
   * @param topic the topic name to publish to
   *
   * @return a handle to a publisher instance
   */
  template <typename T>
  Publisher<T> CreatePublisher(std::string topic) const {
    return std::make_shared<PublisherClass<T>>(topic.c_str());
  }

  /**
   * CreateSubscriber create a new handle for a subscriber
   *
   * @tparam the message type that we expect to receive from the publisher
   * @param topic the topic name to subscribe to
   * @param callback the function to call for every new inbound message
   *
   * @return a subscriber handle
   */
  template <typename T>
  Subscriber<T> CreateSubscriber(std::string topic, std::function<void(const T&)> callback) const {
    return std::make_shared<SubscriberImpl<T>>(topic.c_str(), callback);
  }

  /**
   * CreateDynamicPublisher create a handle to a publisher for message types not known at compile time.
   *
   * @param topic the topic name to publish to
   * @param msg an instance of google::protobuf::Message that contains the appropriate message schema
   *
   * @return a publisher handle
   */
  DynamicPublisher CreateDynamicPublisher(std::string topic, std::shared_ptr<google::protobuf::Message> msg) {
    return std::make_shared<DynamicPublisherImpl>(topic, msg);
  }

  /**
   * CreateDynamicPublisher create a handle to a publisher for message types not known at compile time.
   *
   * @param topic the topic name to publish to
   * @param proto_type_name a string representing the message name to be used
   *
   * Note: the string must be a fully qualified proto name (e.g. my.proto.package.MyMessage)
   * The compiled message must be linked together with this library in order for it to be found at runtime.
   *
   * @return a publisher handle
   */
  DynamicPublisher CreateDynamicPublisher(std::string topic, std::string proto_type_name) {
    return std::make_shared<DynamicPublisherImpl>(topic, proto_type_name);
  }

  /**
   * CreateDynamicSubscriber create a handle to a subscriber for message types not known at compile time.
   *
   * @param topic the topic name to subscribe to
   * @param callback the function to call for every new inbound message
   *
   * Note that the callback will receive a generic `google::protobuf::Message` and must have a way
   * to determine how to interpret the message
   *
   * @return a subscriber handle
   */
  DynamicSubscriber CreateDynamicSubscriber(std::string topic,
                                            std::function<void(const google::protobuf::Message&)> callback) {
    DynamicSubscriber subscriber = std::make_shared<DynamicSubscriberClass>(topic);
    // TODO(bsirang) consider passing time_ and clock_ to user
    auto callback_wrapper = [callback](const char* topic_name_, const google::protobuf::Message& msg_,
                                       long long time_) { callback(msg_); };
    subscriber->AddReceiveCallback(callback_wrapper);
    return subscriber;
  }

  /**
   * CreateServiceClient create a handle to a remote procedure call service client
   *
   * @tparam RPC_T the datatype of the proto service definition
   *
   * @return a service client handle
   */
  template <typename RPC_T>
  ServiceClient<RPC_T> CreateServiceClient() const {
    return std::make_shared<ServiceClientImpl<RPC_T>>();
  }

  /**
   * CreateServiceServer create a remote procedure call server
   *
   * @tparam RPC_T the datatype of the proto service definition
   * @param rpc an instance of the service handler class, which must be a subclass of the proto service class (see:
   * https://developers.google.com/protocol-buffers/docs/reference/cpp-generated#service)
   *
   * @return a service server handle
   */
  template <typename RPC_T>
  ServiceServer<RPC_T> CreateServiceServer(std::shared_ptr<RPC_T> rpc) const {
    return std::make_shared<ServiceServerClass<RPC_T>>(rpc);
  }

  /**
   * CreateTimer create a new periodic timer
   *
   * @param interval_ms the interval in milliseconds in which to invoke the callback
   * @param callback the function to call every time the timer expires
   * @param initial_delay_ms an extra initial delay in milliseconds before the first timer invocation
   *
   * @return a periodic timer object
   */
  Timer CreateTimer(unsigned interval_ms, TimerImpl::Callback callback, unsigned initial_delay_ms = 0) const;

  /**
   * CreateOneShotTimer create a new one-shot timer.
   *
   * A one-shot timer fires only once at some point in the future as specified by the delay.
   *
   * @param initial_delay_ms the amount of delay in milliseconds before the timer expires
   * @param callback the function to call when the timer expries
   *
   * @return a one-shot timer object
   */
  Timer CreateOneShotTimer(unsigned initial_delay_ms, TimerImpl::Callback callback) const;

  /*
   * Run run the application
   *
   * After the application has performed the required initialization, call this method to co-opt the current thread to
   * run the underlying Trellis facilities. It is recommended to call this method as your main() return statement.
   *
   * @return a return code that may be used to return from main()
   */
  int Run();

  /**
   * RunOnce run a single invocation of the underlying event loops
   *
   * Note: this method is not needed for typical applications
   *
   * @return false if the underlying facilities have stopped
   *
   * @see RunN()
   */
  bool RunOnce() { return RunN(1); }

  /**
   * RunN run N iterations of the underlying event loops
   *
   * @param n the number of times to invoke the underlying event loops
   *
   * Note: this method is not needed for typical applications
   *
   * @return false if the underlying facilities have stopped
   */
  bool RunN(unsigned n);

  /**
   * Stop stop the underlying threads
   *
   * Note: this method is not needed for typical applications
   */
  void Stop();

  /**
   * GetEventLoop retrieve a handle to the underlying asio event loop
   * used under the hood.
   *
   * @param a handle to the underlying asio::io_context instance
   */
  EventLoop GetEventLoop() const { return ev_loop_; }

 private:
  const std::string name_;
  EventLoop ev_loop_;
};

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_NODE_HPP
