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

#include "metrics_publisher.hpp"

namespace trellis {
namespace utils {
namespace metrics {

MetricsPublisher::MetricsPublisher(trellis::core::Node& node, const Config& config)
    : node_{node},
      config_{config},
      app_name_{node.GetName()},
      pub_{node.CreatePublisher<trellis::utils::metrics::MetricsGroup>(config_.metrics_topic)} {
  msg_.set_source(app_name_);
}

void MetricsPublisher::AddMeasurement(const trellis::core::time::TimePoint& now, const std::string& key, double value) {
  trellis::utils::metrics::Measurement metric;
  metric.set_name(key);
  metric.set_value(value);
  msg_.mutable_measurements()->Add(std::move(metric));
}

void MetricsPublisher::AddCounter(const trellis::core::time::TimePoint& now, const std::string& key, int64_t counter) {
  const auto now_ms = trellis::core::time::TimePointToMilliseconds(now);
  const auto last = last_counters_.find(key);

  if (last != last_counters_.end()) {
    // Calculate delta from previous value
    const auto [last_counter, last_time_ms] = last->second;
    const auto elapsed_ms = now_ms - last_time_ms;

    trellis::utils::metrics::Counter metric;
    metric.set_name(key);
    metric.set_delta(counter - last_counter);
    metric.set_elapsed_ms(elapsed_ms);
    msg_.mutable_counters()->Add(std::move(metric));
  }

  // Save counter value for next delta calculation
  last_counters_[key] = {counter, now_ms};
}

void MetricsPublisher::Publish(const trellis::core::time::TimePoint& now) {
  // no metrics to publish
  if (msg_.measurements().size() == 0 && msg_.counters().size() == 0) {
    return;
  }

  *msg_.mutable_time() = trellis::core::time::TimePointToTimestamp(now);
  pub_->Send(msg_);

  // Clear the timestamp and metrics, but keep the source name
  msg_.clear_time();
  msg_.clear_measurements();
  msg_.clear_counters();
}
}  // namespace metrics
}  // namespace utils
}  // namespace trellis
