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
  const time::TimePoint start(std::chrono::milliseconds(500));
  const time::TimePoint end(std::chrono::milliseconds(1000));

  const auto start_secs = time::TimePointToSeconds(start);
  const auto end_secs = time::TimePointToSeconds(end);

  const auto dt = end_secs - start_secs;

  ASSERT_TRUE(end_secs > start_secs);
  ASSERT_TRUE(dt == 0.5);
}

TEST(TrellisTimeAPI, Nanoseconds) {
  const time::TimePoint start(std::chrono::milliseconds(500));
  const time::TimePoint end(std::chrono::milliseconds(1000));

  const auto start_nanos = time::TimePointToNanoseconds(start);
  const auto end_nanos = time::TimePointToNanoseconds(end);

  const auto dt = end_nanos - start_nanos;

  ASSERT_TRUE(end_nanos > start_nanos);
  ASSERT_TRUE(dt == 5e8);
}

TEST(TrellisTimeAPI, Milliseconds) {
  const time::TimePoint start(std::chrono::milliseconds(500));
  const time::TimePoint end(std::chrono::milliseconds(1000));

  const auto start_millis = time::TimePointToMilliseconds(start);
  const auto end_millis = time::TimePointToMilliseconds(end);

  const auto dt = end_millis - start_millis;

  ASSERT_TRUE(end_millis > start_millis);
  ASSERT_TRUE(dt == 500);
}

TEST(TrellisTimeAPI, SetSimTimeWhileDisabled) {
  trellis::core::time::TimePoint timepoint{std::chrono::milliseconds(1500)};
  ASSERT_THROW(trellis::core::time::SetSimulatedTime(timepoint), std::runtime_error);
}

// All the tests following this one have simulated clock enabled

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
  const auto now = trellis::core::time::Now();
  ASSERT_TRUE(trellis::core::time::TimePointToSeconds(now) == 0.0);

  trellis::core::time::SetSimulatedTime(now + std::chrono::milliseconds(1500));
  ASSERT_TRUE(trellis::core::time::NowInSeconds() == 1.5);
}

TEST(TrellisTimeAPI, IncrementSimTime) {
  const auto now = trellis::core::time::Now();

  trellis::core::time::IncrementSimulatedTime(std::chrono::milliseconds(1500));
  const auto future = trellis::core::time::Now();

  auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(future - now);
  ASSERT_EQ(delta.count(), 1500);
}

TEST(TrellisTimeAPI, SystemTimeConversion) {
  const time::TimePoint start(std::chrono::milliseconds(500));
  const time::TimePoint end(std::chrono::milliseconds(2500));

  trellis::core::time::SetSimulatedTime(start);
  const auto system_start = time::TimePointToSystemTime(start);
  const auto system_end = time::TimePointToSystemTime(end);

  const auto dt = system_end - system_start;
  const auto dt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(system_end - system_start).count();

  ASSERT_TRUE(system_end > system_start);
  ASSERT_EQ(dt_ms, 2000);

  const auto system_seconds_start_absolute =
      std::chrono::duration_cast<std::chrono::seconds>(system_start.time_since_epoch()).count();
  const auto system_seconds_end_absolute =
      std::chrono::duration_cast<std::chrono::seconds>(system_end.time_since_epoch()).count();

  // Sanity check on the absolute system time by expecting it to be more recent than
  // when this test was written (in seconds since epoch). The caveat us that the system time of the
  // machine running this test needs to be sane. Remove this check if it becomes a problem.
  ASSERT_TRUE(system_seconds_start_absolute > 1651626686);
  // And for good measure make sure they're two seconds off in absolute time too
  ASSERT_EQ(system_seconds_start_absolute + 2, system_seconds_end_absolute);
}
