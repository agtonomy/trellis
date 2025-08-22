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

#ifndef TRELLIS_CORE_STATISTICS_FREQUENCY_CALCULATOR_HPP_
#define TRELLIS_CORE_STATISTICS_FREQUENCY_CALCULATOR_HPP_

#include <chrono>
#include <optional>

#include "trellis/core/time.hpp"

namespace trellis::core::statistics {

/**
 * @brief Utility class for calculating message frequency statistics.
 *
 * This class provides common functionality for tracking message counts
 * and calculating frequency over time, used by both publishers and subscribers.
 */
class FrequencyCalculator {
 public:
  /**
   * @brief Construct a frequency calculator.
   *
   * @param update_interval_ms Interval in milliseconds for frequency calculations.
   */
  explicit FrequencyCalculator(unsigned update_interval_ms) : update_interval_ms_(update_interval_ms) {}

  /**
   * @brief Increment the message count.
   *
   * Call this every time a message is sent or received.
   */
  void IncrementCount() { ++total_count_; }

  /**
   * @brief Get the current total message count.
   *
   * @return Total number of messages processed.
   */
  unsigned GetTotalCount() const { return total_count_; }

  /**
   * @brief Get the current measured frequency.
   *
   * @return Frequency in Hz (messages per second).
   */
  double GetFrequencyHz() const { return measured_frequency_hz_; }

  /**
   * @brief Update frequency calculation if enough time has passed.
   *
   * This should be called periodically (either on every message or via timer).
   *
   * @param now Current timestamp.
   * @return true if frequency was recalculated, false otherwise.
   */
  bool UpdateFrequency(const trellis::core::time::TimePoint& now) {
    if (!last_measurement_time_.has_value()) {
      // First measurement - just record the baseline
      last_measurement_time_ = now;
      last_measurement_count_ = total_count_;
      return false;
    }

    const auto time_delta = now - last_measurement_time_.value();
    if (time_delta >= std::chrono::milliseconds(update_interval_ms_)) {
      // Update frequency calculation
      last_measurement_time_ = now;
      const unsigned count_delta = total_count_ - last_measurement_count_;
      const auto duration_s = std::chrono::duration_cast<std::chrono::duration<double>>(time_delta).count();
      measured_frequency_hz_ = static_cast<double>(count_delta) / duration_s;
      last_measurement_count_ = total_count_;
      return true;
    }

    return false;
  }

 private:
  const unsigned update_interval_ms_;  ///< Interval for frequency calculations in milliseconds

  unsigned total_count_{0};                                                ///< Total messages processed
  double measured_frequency_hz_{0.0};                                      ///< Calculated frequency in Hz
  std::optional<trellis::core::time::TimePoint> last_measurement_time_{};  ///< Last measurement timestamp
  unsigned last_measurement_count_{0};                                     ///< Count at last measurement
};

}  // namespace trellis::core::statistics

#endif  // TRELLIS_CORE_STATISTICS_FREQUENCY_CALCULATOR_HPP_
