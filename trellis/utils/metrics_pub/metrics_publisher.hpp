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

#ifndef TRELLIS_UTILS_METRICS_PUB_METRICS_PUBLISHER_HPP
#define TRELLIS_UTILS_METRICS_PUB_METRICS_PUBLISHER_HPP

#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

#include "trellis/core/node.hpp"
#include "trellis/core/publisher.hpp"
#include "trellis/core/time.hpp"
#include "trellis/utils/metrics_pub/generic_metrics.pb.h"

namespace trellis {
namespace utils {
namespace metrics {

/**
 * @brief Publishes application metrics over a trellis topic.
 *
 * MetricsPublisher provides a simple interface for publishing two types of metrics:
 * - **Measurements**: Instantaneous values at a point in time (e.g., temperature, queue depth)
 * - **Counters**: Monotonically increasing values where the delta between publishes is reported
 *   (e.g., bytes transferred, messages processed)
 *
 * Metrics are accumulated via AddMeasurement() and AddCounter() calls, then sent as a batch
 * when Publish() is called. After publishing, the internal state is cleared for the next batch.
 */
class MetricsPublisher {
 public:
  /**
   * @brief Configuration for MetricsPublisher.
   */
  struct Config {
    std::string metrics_topic;  ///< The topic name to publish metrics on.
  };

  /**
   * @brief Constructs a MetricsPublisher.
   * @param node The trellis node used to create the publisher.
   * @param config Configuration specifying the metrics topic.
   */
  MetricsPublisher(trellis::core::Node& node, const Config& config);

  /**
   * @brief Adds an instantaneous measurement to the next publish batch.
   *
   * Measurements represent point-in-time values such as gauges, levels, or any
   * numeric value that can go up or down.
   *
   * @param now The timestamp for this measurement.
   * @param key The name/identifier for this measurement.
   * @param value The numeric value of the measurement.
   */
  void AddMeasurement(const trellis::core::time::TimePoint& now, const std::string& key, double value);

  /**
   * @brief Adds a counter metric to the next publish batch.
   *
   * Counters represent monotonically increasing values. The published metric contains
   * the delta (change) from the previous call and the elapsed time between calls.
   * This is useful for computing rates (e.g., messages/second, bytes/second).
   *
   * @note The first call for a given key establishes the baseline and does not
   *       produce any output. Subsequent calls will publish the delta.
   *
   * @param now The timestamp for this counter reading.
   * @param key The name/identifier for this counter.
   * @param counter The current cumulative counter value.
   */
  void AddCounter(const trellis::core::time::TimePoint& now, const std::string& key, int64_t counter);

  /**
   * @brief Publishes all accumulated metrics and clears the internal state.
   *
   * Sends a MetricsGroup message containing all measurements and counters added
   * since the last Publish() call. If no metrics have been added, this is a no-op.
   * After publishing, the measurements and counters are cleared, but counter
   * baselines are retained for delta calculations.
   *
   * @param now The timestamp to attach to the published message.
   */
  void Publish(const trellis::core::time::TimePoint& now);

 private:
  trellis::core::Node& node_;
  const Config config_;
  const std::string app_name_;
  trellis::core::Publisher<trellis::utils::metrics::MetricsGroup> pub_;
  trellis::utils::metrics::MetricsGroup msg_;
  std::unordered_map<std::string, std::pair<int64_t, unsigned long long>> last_counters_;
};

}  // namespace metrics
}  // namespace utils
}  // namespace trellis
#endif  // TRELLIS_UTILS_METRICS_PUB_METRICS_PUBLISHER_HPP
