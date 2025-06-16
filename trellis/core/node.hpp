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

#ifndef TRELLIS_CORE_NODE_HPP_
#define TRELLIS_CORE_NODE_HPP_

#include <asio.hpp>
#include <functional>
#include <list>
#include <optional>
#include <string>

#include "bind.hpp"
#include "config.hpp"
#include "event_loop.hpp"
#include "health.hpp"
#include "logging.hpp"
#include "publisher.hpp"
#include "subscriber.hpp"
#include "time.hpp"
#include "timer.hpp"
#include "trellis/core/ipc/named_resource_registry.hpp"
#include "trellis/core/ipc/proto/rpc/client.hpp"
#include "trellis/core/ipc/proto/rpc/server.hpp"
#include "trellis/core/timestamped_message.pb.h"

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
   * Function type for signal handlers (SIGINT & SIGTERM)
   */
  using SignalHandler = std::function<void(int)>;

  /**
   * Node Construct an instance
   *
   * @param name the name of the application this instance represents
   */
  Node(std::string name, trellis::core::Config config);

  ~Node();

  // Moving/copying not allowed
  Node(const Node&) = delete;
  Node(Node&&) = delete;
  Node& operator=(const Node&) = delete;
  Node& operator=(Node&&) = delete;

  /**
   * CreatePublisher create a new handle for a publisher
   *
   * @tparam MSG_T the message type that will be published by this handle
   * @param topic the topic name to publish to
   *
   * @return a handle to a publisher instance
   */
  template <typename MSG_T>
  Publisher<MSG_T> CreatePublisher(const std::string& topic) const {
    return std::make_shared<PublisherImpl<MSG_T>>(GetEventLoop(), topic, GetDiscovery(), config_);
  }

  /**
   * CreateZeroCopyPublisher create a new handle for a zero-copy publisher
   *
   * @tparam MSG_T the message type that will be published by this handle
   * @param topic the topic name to publish to
   *
   * @return a handle to a publisher instance
   *
   * Note: A zero-copy publisher should only be used for larger payloads (i.e. in the megabytes)
   */
  template <typename MSG_T>
  Publisher<MSG_T> CreateZeroCopyPublisher(const std::string& topic) const {
    return std::make_shared<PublisherImpl<MSG_T>>(GetEventLoop(), topic, GetDiscovery(), config_);
  }

  /**
   * CreateSubscriber create a new handle for a subscriber
   *
   * @tparam MSG_T the message type that we expect to receive from the publisher
   * @tparam MAX_MSGS the max number of messages that can be allocated and passed out in the callback.
   * @param topic the topic name to subscribe to
   * @param callback the function to call for every new inbound message
   * @param watchdog_timeout_ms optional timeout in milliseconds for a watchdog
   * @param watchdog_callback optional watchdog callback to monitor timeouts
   * @param max_frequency optional maximum frequency to throttle the subscriber callback
   *
   * NOTE: Both watchdog_timeout_ms and watchdog_callback must be specified in
   * order to enable watchdog monitoring.
   *
   * @return a subscriber handle
   */
  template <typename MSG_T>
  Subscriber<MSG_T> CreateSubscriber(std::string_view topic,
                                     typename trellis::core::SubscriberImpl<MSG_T>::Callback callback,
                                     std::optional<unsigned> watchdog_timeout_ms = {},
                                     TimerImpl::Callback watchdog_callback = {},
                                     std::optional<double> max_frequency = {}) {
    auto update_sim_fn = [this](const time::TimePoint& time) { UpdateSimulatedClock(time); };
    const bool do_watchdog = watchdog_timeout_ms.has_value() && watchdog_callback != nullptr;
    Timer watchdog_timer{};

    using RawCallback = typename trellis::core::SubscriberImpl<MSG_T>::RawCallback;
    const auto impl = std::make_shared<SubscriberImpl<MSG_T>>(GetEventLoop(), std::string{topic}, callback,
                                                              RawCallback{}, update_sim_fn, GetDiscovery());
    if (max_frequency.has_value()) {
      impl->SetMaxFrequencyThrottle(max_frequency.value());
    }
    if (do_watchdog) {
      const auto initial_delay_ms = watchdog_timeout_ms.value();
      auto watchdog_wrapper = [watchdog_callback = std::move(watchdog_callback), impl](const time::TimePoint& now) {
        // Desired behavior is to have the watchdog fire only if messages were previously received
        if (impl->DidReceive()) {
          watchdog_callback(now);
        }
      };
      auto timer = CreateOneShotTimer(initial_delay_ms, std::move(watchdog_wrapper));
      impl->SetWatchdogTimer(std::move(timer));
    }
    return impl;
  }

  /**
   * CreateDynamicPublisher create a handle to a publisher for message types not known at compile time.
   *
   * In order to use the dynamic publisher, you must be able to create instances of the abstract
   * google::protobuf::Message type at runtime.
   *
   * @param topic the topic name to publish to
   *
   * @return a publisher handle
   */
  DynamicPublisher CreateDynamicPublisher(const std::string& topic) const {
    return std::make_shared<PublisherImpl<google::protobuf::Message>>(GetEventLoop(), topic, GetDiscovery(), config_);
  }

  /**
   * CreateDynamicSubscriber create a handle to a subscriber for message types not known at compile time.
   *
   * @param topic the topic name to subscribe to
   * @param callback the function to call for every new inbound message
   * @param watchdog_timeout_ms optional timeout in milliseconds for a watchdog
   * @param watchdog_callback optional watchdog callback to monitor timeouts
   * @param max_frequency optional maximum frequency to throttle the subscriber callback
   *
   * Note that the callback will receive a generic `google::protobuf::Message` and must have a way
   * to determine how to interpret the message
   *
   * @return a subscriber handle
   */
  DynamicSubscriber CreateDynamicSubscriber(
      const std::string& topic, typename trellis::core::SubscriberImpl<google::protobuf::Message>::Callback callback,
      std::optional<unsigned> watchdog_timeout_ms = {}, TimerImpl::Callback watchdog_callback = {},
      std::optional<double> max_frequency = {}) {
    return CreateSubscriber<google::protobuf::Message>(topic, std::move(callback), watchdog_timeout_ms,
                                                       std::move(watchdog_callback), max_frequency);
  }

  /**
   * @brief CreateRawSubscriber create a handle to a raw subscriber. A raw subscriber can be used to receive the raw
   * message payload before deserialization.
   *
   * @param topic the topic name to subscribe to
   * @param callback the function to call for every new inbound message
   * @return SubscriberRaw
   */
  SubscriberRaw CreateRawSubscriber(std::string topic, SubscriberRawImpl::RawCallback callback) {
    auto update_sim_fn = [this](const time::TimePoint& time) { UpdateSimulatedClock(time); };
    return std::make_shared<SubscriberImpl<google::protobuf::Message>>(
        GetEventLoop(), std::string{topic}, SubscriberRawImpl::Callback{}, std::move(callback),
        std::move(update_sim_fn), GetDiscovery());
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
    return std::make_shared<ipc::proto::rpc::Client<RPC_T>>(GetEventLoop(), GetDiscovery());
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
    return std::make_shared<ipc::proto::rpc::Server<RPC_T>>(rpc, GetEventLoop(), GetDiscovery());
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
  Timer CreateTimer(unsigned interval_ms, TimerImpl::Callback callback, unsigned initial_delay_ms = 0);

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
  Timer CreateOneShotTimer(unsigned initial_delay_ms, TimerImpl::Callback callback);

  /**
   * RemoveTimer remove a timer pointer from the list of active timers
   *
   * If the caller still holds a reference to the timer, it will not be deleted.
   *
   * @param timer the timer to remove
   */
  void RemoveTimer(const Timer& timer);

  /**
   * UpdateHealth update application health state
   *
   * An application can call this to update health information that is broadcast to the rest of the system
   * @param status The health status
   * @param compare_description A flag signalling that the description should be used in the status comparison; defaults
   * to false health state update
   *
   * @ see health.hpp
   */
  void UpdateHealth(const trellis::core::HealthStatus& status, const bool compare_description = false);

  /**
   * UpdateHealth update application health state
   *
   * An application can call this to update health information that is broadcast to the rest of the system
   * @param state the enumerated health state value
   * @param code an optional application-defined integer representing the condition causing the health state update
   * @param description an optional application-defined, human-readable string represending the condition causing the
   * @param compare_description A flag signalling that the description should be used in the status comparison; defaults
   * to false health state update
   *
   * @ see health.hpp
   */
  void UpdateHealth(trellis::core::HealthState state, Health::Code code = 0, const std::string& description = "",
                    const bool compare_description = false);

  /**
   * GetHealthState get current app health state value
   */
  trellis::core::HealthState GetHealthState() const;

  /**
   * GetLastHealthStatus get the full update from the most recent health update
   */
  const trellis::core::HealthStatus& GetLastHealthStatus() const;

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
   * This method allows the application to execute the event loop for a limited number of events,
   * which is useful for testing or special control flows where the main thread must regain control
   * after a timeout.
   *
   * Note: this method is not needed for typical applications
   *
   * @return false if the underlying facilities have stopped
   */
  bool RunN(unsigned n);

  /**
   * RunFor runs the underlying event loop for a specified duration
   *
   * This method allows the application to execute the event loop for a limited time duration,
   * which is useful for testing or special control flows where the main thread must regain control
   * after a timeout.
   *
   * @tparam Rep An arithmetic type representing the number of ticks.
   * @tparam Period A std::ratio type representing the tick period.
   * @param rel_time The duration to run the event loop for.
   *
   * @return false if the underlying facilities have stopped
   *
   * @see Run(), RunOnce(), RunN()
   */
  template <typename Rep, typename Period>
  bool RunFor(const std::chrono::duration<Rep, Period>& rel_time) {
    try {
      ev_loop_.RunFor(rel_time);
      return ShouldRun();
    } catch (const std::exception& e) {
      Log::Error("Unhandled std::exception: {}", e.what());
      ipc::NamedResourceRegistry::Get().UnlinkAll();
      return 1;
    } catch (...) {
      Log::Error("Unhandled unknown exception occurred.");
      ipc::NamedResourceRegistry::Get().UnlinkAll();
      return 1;
    }
  }

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

  /**
   * GetDiscovery retrieve a handle to the discovery module
   */
  discovery::DiscoveryPtr GetDiscovery() const { return discovery_; }

  /**
   *  AddSignalHandler adds a handler for SIGINT or SIGTERM signals
   *
   * @param handler the function to call when SIGINT or SIGTERM is caught
   */
  void AddSignalHandler(SignalHandler handler);

  /**
   * UpdateSimulatedClock update the simulated clock
   *
   * Updates the simulated clock based on the given time, and immediately runs any timers that are due
   */
  void UpdateSimulatedClock(const time::TimePoint& new_time);

  /**
   * GetConfig retrieve a reference to the loaded configuration object
   *
   * @return the config object
   */
  const trellis::core::Config& GetConfig() { return config_; }

  /**
   * Returns the name of the node
   *
   * @return The name of the node
   */
  inline const std::string& GetName() const { return name_; }

 private:
  // Helper to determine if we should not be running anymore
  bool ShouldRun();

  // The name of the node
  const std::string name_;

  // The active configuration
  trellis::core::Config config_;

  // The event loop handle used for asynchronous operations
  EventLoop ev_loop_;

  // The dynamic discovery layer for discovering other nodes
  discovery::DiscoveryPtr discovery_;

  // Used to manage signal handlers
  asio::signal_set signal_set_;

  // Used to manage application health state
  trellis::core::Health health_;

  // User-specified signal handler
  SignalHandler user_handler_{nullptr};

  // Track the first invocation of the event loop
  bool first_run_{true};

  // A list of the timers that have been created
  std::set<Timer> timers_;
};

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_NODE_HPP_
