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

#ifndef TRELLIS_CORE_STATISTICS_LATENCY_CALCULATOR_HPP_
#define TRELLIS_CORE_STATISTICS_LATENCY_CALCULATOR_HPP_

#include <chrono>
#include <cstdint>
#include <limits>

#include "trellis/core/time.hpp"

namespace trellis::core::statistics {

/**
 * @brief Utility class for calculating message latency statistics.
 *
 * Tracks min, mean, and max latency over a measurement window.
 * Statistics are reset after each call to GetAndReset().
 */
class LatencyCalculator {
 public:
  struct Stats {
    int64_t min_us;   ///< Minimum latency in microseconds
    int64_t mean_us;  ///< Mean latency in microseconds
    int64_t max_us;   ///< Maximum latency in microseconds
    uint64_t count;   ///< Size of sample the stats are computed over
  };

  /**
   * @brief Record a latency sample.
   *
   * @param receive_time Time the message was received.
   * @param send_time Time the message was sent.
   */
  void RecordLatency(const trellis::core::time::TimePoint& receive_time,
                     const trellis::core::time::TimePoint& send_time) {
    const auto latency_us = std::chrono::duration_cast<std::chrono::microseconds>(receive_time - send_time).count();
    min_us_ = std::min(min_us_, latency_us);
    max_us_ = std::max(max_us_, latency_us);
    sum_us_ += latency_us;
    ++count_;
  }

  /**
   * @brief Get the current statistics and reset for the next window.
   *
   * @return Stats structure with min/mean/max latency in microseconds.
   */
  Stats GetAndReset() {
    Stats stats{};
    stats.count = count_;
    if (count_ > 0) {
      stats.min_us = min_us_;
      stats.mean_us = sum_us_ / static_cast<int64_t>(count_);
      stats.max_us = max_us_;
    }
    Reset();
    return stats;
  }

  /**
   * @brief Check if any samples have been recorded.
   *
   * @return True if at least one latency sample has been recorded.
   */
  bool HasSamples() const { return count_ > 0; }

 private:
  void Reset() {
    min_us_ = std::numeric_limits<int64_t>::max();
    max_us_ = std::numeric_limits<int64_t>::min();
    sum_us_ = 0;
    count_ = 0;
  }

  int64_t min_us_{std::numeric_limits<int64_t>::max()};
  int64_t max_us_{std::numeric_limits<int64_t>::min()};
  int64_t sum_us_{0};
  unsigned count_{0};
};

}  // namespace trellis::core::statistics

#endif  // TRELLIS_CORE_STATISTICS_LATENCY_CALCULATOR_HPP_
