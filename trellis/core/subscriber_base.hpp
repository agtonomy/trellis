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
 *
 */

#ifndef TRELLIS_CORE_SUBSCRIBER_BASE_HPP_
#define TRELLIS_CORE_SUBSCRIBER_BASE_HPP_

#include <string>

#include "trellis/core/statistics/latency_calculator.hpp"

namespace trellis::core {

/**
 * @brief Abstract base class for type-erased subscriber access.
 *
 * This interface allows Node to track heterogeneous SubscriberImpl instantiations
 * and collect latency statistics without knowing the concrete template parameters.
 */
class SubscriberBase {
 public:
  virtual ~SubscriberBase() = default;

  /**
   * @brief Get the topic name this subscriber is subscribed to.
   * @return The topic name.
   */
  virtual const std::string& GetTopic() const = 0;

  /**
   * @brief Get the latency stats since last call and reset the stats.
   * @return Latency stats with min, mean, max latency in microseconds and sample count.
   */
  virtual statistics::LatencyCalculator::Stats GetLatestLatencyStats() = 0;

 protected:
  SubscriberBase() = default;
  SubscriberBase(const SubscriberBase&) = delete;
  SubscriberBase& operator=(const SubscriberBase&) = delete;
  SubscriberBase(SubscriberBase&&) = delete;
  SubscriberBase& operator=(SubscriberBase&&) = delete;
};

}  // namespace trellis::core

#endif  // TRELLIS_CORE_SUBSCRIBER_BASE_HPP_
