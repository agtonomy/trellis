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

#ifndef TRELLIS_CORE_HEALTH_HPP
#define TRELLIS_CORE_HEALTH_HPP

#include <deque>
#include <functional>
#include <string>

#include "publisher.hpp"
#include "trellis/core/config.hpp"
#include "trellis/core/health_history.pb.h"
#include "trellis/core/timer.hpp"

namespace trellis {
namespace core {

/**
 * Health class to facilitate the reporting of generic health state from applications.
 *
 * Applications will interface with this class via the trellis::core::Node APIs. This class manages a historical list of
 * health state reported by applications and reports them via a publisher.
 */
class Health {
 public:
  using TimerCreateFunction = std::function<trellis::core::Timer(unsigned, trellis::core::TimerImpl::Callback)>;
  using HealthPublisher = trellis::core::Publisher<trellis::core::HealthHistory>;
  using PublisherCreateFunction = std::function<HealthPublisher(const std::string&)>;
  using HealthHistory = std::deque<trellis::core::HealthStatus>;
  using Code = uint32_t;

  /**
   * Health construct the health object
   *
   * @param name the name of the application reporting health
   * @param config trellis config object to retrieve reporting_topic, publisher_interval_ms, and history_size
   * @param publisher_create_fn a function that will create and return a trellis publisher object for health
   * @param timer_create_fn a function that will create a periodic timer for reporting
   *
   * The config object is expected to have the following structure:
   * trellis:
   *   health:
   *     reporting_topic: "/trellis/app/health"
   *     interval_ms: 200
   *     history_size: 10
   */
  Health(const std::string& name, const trellis::core::Config& config,
         const PublisherCreateFunction& publisher_create_fn, const TimerCreateFunction& timer_create_fn);

  /**
   * Update submit a health update
   *
   * @param state the discrete health enumeration
   * @param code an application-defined numeric code associated with the update
   * @param description an application-defined string associated with the update
   * @param compare_description A flag signalling that the description should be used in the status comparison; defaults
   * to false
   */
  void Update(const trellis::core::HealthState& state, const Code& code, const std::string& description,
              const bool compare_description = false);

  /**
   * GetHealthState retrieve the health enumeration for the most recent update
   */
  trellis::core::HealthState GetHealthState() const;

  /**
   * GetLastHealthStatus retrieve the most recent update
   */
  const trellis::core::HealthStatus& GetLastHealthStatus() const;

  /**
   * GetHealthHistory retrieve the list of historical health updates
   */
  const HealthHistory& GetHealthHistory() const;

  static std::string GetTopicFromConfig(const trellis::core::Config& config);
  static unsigned GetReportingIntervalFromConfig(const trellis::core::Config& config);
  static unsigned GetHistorySizeFromConfig(const trellis::core::Config& config);

 private:
  void UpdateTimer();

  static trellis::core::HealthHistory CreateHealthHistoryMessage(const std::string& name, const HealthHistory& history);
  static trellis::core::HealthStatus CreateHealthUpdateMessage(const trellis::core::HealthState& state,
                                                               const Code& code, const std::string& description);

  void LogUpdate(const trellis::core::HealthStatus& update) const;

  const std::string name_;
  const std::string reporting_topic_;
  const size_t reporting_interval_ms_;
  const size_t history_size_;
  const PublisherCreateFunction publisher_create_fn_;
  const TimerCreateFunction timer_create_fn_;
  HealthPublisher publisher_{nullptr};
  trellis::core::Timer update_timer_{nullptr};
  HealthHistory health_history_{};
};

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_HEALTH_HPP
