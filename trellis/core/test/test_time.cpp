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

#include <gtest/gtest.h>

#include <thread>

#include "trellis/core/time.hpp"

using namespace trellis::core;

TEST(TrellisTimeAPI, SecondsConversion) {
  time::TimePoint start(std::chrono::milliseconds(500));
  time::TimePoint end(std::chrono::milliseconds(1000));

  const double start_secs = time::TimePointToSeconds(start);
  const double end_secs = time::TimePointToSeconds(end);

  const double dt = end_secs - start_secs;

  ASSERT_TRUE(end_secs > start_secs);
  ASSERT_TRUE(dt == 0.5);
}

TEST(TrellisTimeAPI, Nanoseconds) {
  time::TimePoint start(std::chrono::milliseconds(500));
  time::TimePoint end(std::chrono::milliseconds(1000));

  const double start_nanos = time::TimePointToNanoseconds(start);
  const double end_nanos = time::TimePointToNanoseconds(end);

  const double dt = end_nanos - start_nanos;

  ASSERT_TRUE(end_nanos > start_nanos);
  ASSERT_TRUE(dt == 5e8);
}

TEST(TrellisTimeAPI, Milliseconds) {
  time::TimePoint start(std::chrono::milliseconds(500));
  time::TimePoint end(std::chrono::milliseconds(1000));

  const double start_millis = time::TimePointToMilliseconds(start);
  const double end_millis = time::TimePointToMilliseconds(end);

  const double dt = end_millis - start_millis;

  ASSERT_TRUE(end_millis > start_millis);
  ASSERT_TRUE(dt == 500);
}

TEST(TrellisTimeAPI, SetSimTimeWhileDisabled) {
  trellis::core::time::TimePoint timepoint{std::chrono::milliseconds(1500)};
  ASSERT_THROW(trellis::core::time::SetSimulatedTime(timepoint), std::runtime_error);
}

TEST(TrellisTimeAPI, EnableSimTime) {
  ASSERT_FALSE(trellis::core::time::IsSimulatedClockEnabled());
  auto now = trellis::core::time::Now();
  // Real time is going to be some non-zero time
  ASSERT_TRUE(trellis::core::time::TimePointToSeconds(now) > 0.0);

  trellis::core::time::EnableSimulatedClock();

  ASSERT_TRUE(trellis::core::time::IsSimulatedClockEnabled());
  now = trellis::core::time::Now();
  // Simulated time is going to start at exactly zero
  ASSERT_TRUE(trellis::core::time::TimePointToSeconds(now) == 0.0);
}

TEST(TrellisTimeAPI, SetSimTime) {
  auto now = trellis::core::time::Now();
  ASSERT_TRUE(trellis::core::time::TimePointToSeconds(now) == 0.0);

  trellis::core::time::SetSimulatedTime(now + std::chrono::milliseconds(1500));
  ASSERT_TRUE(trellis::core::time::NowInSeconds() == 1.5);
}

TEST(TrellisTimeAPI, IncrementSimTime) {
  auto now = trellis::core::time::Now();

  trellis::core::time::IncrementSimulatedTime(std::chrono::milliseconds(1500));
  auto future = trellis::core::time::Now();

  auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(future - now);
  ASSERT_EQ(delta.count(), 1500);
}
