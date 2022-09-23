/*
 * Copyright (C) 2022 Agtonomy
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

#ifndef TRELLIS_CORE_HEALTH_MONITOR_HPP
#define TRELLIS_CORE_HEALTH_MONITOR_HPP

#include "trellis/core/config.hpp"
#include "trellis/core/health_history.pb.h"
#include "trellis/core/subscriber.hpp"

namespace trellis {
namespace core {

/**
 * HealthMonitor class to monitor node health
 *
 * This class will aggregate health information being published on the health topic and provide callbacks and APIs to
 * access the information. This could be used by a node that is interested in reacting to the health state of
 * other parts of the system.
 *
 */
class HealthMonitor {
 public:
  enum class Event {
    kUnknown = 0,
    kNodeHealthUpdateLost,
    kNodeTransitionToHealthy,
    kNodeTransitionToUnhealthy,
  };
  using HealthSubscriber = trellis::core::Subscriber<trellis::core::HealthHistory>;
  using SubscriberCreateFunction = std::function<HealthSubscriber(
      const std::string& topic, trellis::core::SubscriberImpl<trellis::core::HealthHistory>::Callback)>;
  using TimerCreateFunction = std::function<trellis::core::Timer(unsigned, trellis::core::TimerImpl::Callback)>;
  using HealthHistory = google::protobuf::RepeatedPtrField<trellis::core::HealthStatus>;
  using HealthEventCallback = std::function<void(const std::string&, Event)>;

  /**
   * HealthMonitor constructor
   *
   * @param loop trellis event loop to use for dispatching the events, this should come from node.GetEventLoop()
   * @param config trellis config object to retrieve topic and reporting interval (see health.hpp)
   * @param timer_create_fn a function that will create a watchdog timer for health alert timeouts
   * @param subscriber_create_fn a function that will create and return a trellis subscriber object for receiving all
   * health updates
   * @param health_event_callback the user callback that will be invoked upon health related events
   */
  HealthMonitor(const trellis::core::EventLoop& loop, const trellis::core::Config& config,
                const TimerCreateFunction& timer_create_fn, const SubscriberCreateFunction& subscriber_create_fn,
                const HealthEventCallback& health_event_callback);

  /**
   * GetLastHealthUpdate get current health status for the node given by name
   *
   * @param name the node name
   */
  const trellis::core::HealthStatus& GetLastHealthUpdate(const std::string& name) const;

  /**
   * GetHealthHistory get the health history for the node given by name
   *
   * @param name the node name
   */
  const HealthHistory& GetHealthHistory(const std::string& name) const;

  /**
   * IsAllHealthy determine whether all known nodes are healthy
   *
   * @return true if all the nodes that we have health info from are reporting healthy
   */
  bool IsAllHealthy() const;

  /**
   * NewUpdate update the cache with a new health update
   */
  void NewUpdate(const trellis::core::HealthHistory& status);

  /**
   * GetNodeNames get a set of node names that we've heard from thus far
   */
  std::set<std::string> GetNodeNames() const;

  /**
   * HasHealthInfo check if there exists health info for a given node name
   *
   * @param name the node name
   * @return true if the data exists in the cache for the given node
   */
  bool HasHealthInfo(const std::string& name) const;

  /**
   * WatchdogExpired called when there was no health update from a node beyond the watchdog time period
   */
  void WatchdogExpired(const std::string& name);

 private:
  void CheckNameExists(const std::string& name) const;
  static unsigned GetReportingIntervalTimeoutFromConfig(const trellis::core::Config& config);
  void InsertIntoCache(const trellis::core::HealthHistory& status);

  const trellis::core::HealthStatus& GetMostRecentNodeHealthFromCache(const std::string& name) const;
  static const trellis::core::HealthStatus& GetMostRecentNodeHealthFromMessage(
      const trellis::core::HealthHistory& status);

  /**
   * Data structure associated with each node whose health data is received
   */
  struct Data {
    trellis::core::Timer watchdog_;
    HealthHistory history_;
  };

  using HealthMap = std::unordered_map<std::string, Data>;

  const trellis::core::EventLoop ev_loop_;     /// trellis event loop to dispatch events on
  const unsigned reporting_watchdog_ms_;       /// amount of time to wait for a health update before determining an
                                               /// node has gone away
  const TimerCreateFunction timer_create_fn_;  /// a function that is responsible for creating and returning a one-shot
                                               /// timer object to be used for watchdogs
  const HealthSubscriber subscriber_;          /// a trellis subscriber object for health updates
  const HealthEventCallback health_event_cb_;  /// a user callback for health events
  HealthMap health_data_;  /// our internal health cache, which is a map of node names to Data structures
};

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_HEALTH_MONITOR_HPP
