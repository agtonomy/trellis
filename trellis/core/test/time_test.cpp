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

#include "trellis/core/time.hpp"

#include <gmock/gmock.h>

#include <thread>

namespace google::protobuf {
bool operator==(const Timestamp& a, const Timestamp& b) { return a.seconds() == b.seconds() && a.nanos() == b.nanos(); }
}  // namespace google::protobuf

namespace trellis::core::time {

using std::chrono_literals::operator""ns;
using std::chrono_literals::operator""s;
using testing::Eq;

TEST(TrellisTimeAPI, SecondsConversion) {
  const TimePoint start(std::chrono::milliseconds(500));
  const TimePoint end(std::chrono::milliseconds(1000));

  const auto start_secs = TimePointToSeconds(start);
  const auto end_secs = TimePointToSeconds(end);

  const auto dt = end_secs - start_secs;

  ASSERT_TRUE(end_secs > start_secs);
  ASSERT_TRUE(dt == 0.5);
}

TEST(TrellisTimeAPI, Nanoseconds) {
  const TimePoint start(std::chrono::milliseconds(500));
  const TimePoint end(std::chrono::milliseconds(1000));

  const auto start_nanos = TimePointToNanoseconds(start);
  const auto end_nanos = TimePointToNanoseconds(end);

  const auto dt = end_nanos - start_nanos;

  ASSERT_TRUE(end_nanos > start_nanos);
  ASSERT_TRUE(dt == 5e8);
}

TEST(TrellisTimeAPI, Milliseconds) {
  const TimePoint start(std::chrono::milliseconds(500));
  const TimePoint end(std::chrono::milliseconds(1000));

  const auto start_millis = TimePointToMilliseconds(start);
  const auto end_millis = TimePointToMilliseconds(end);

  const auto dt = end_millis - start_millis;

  ASSERT_TRUE(end_millis > start_millis);
  ASSERT_TRUE(dt == 500);
}

TEST(TrellisTimeAPI, SetSimTimeWhileDisabled) {
  TimePoint timepoint{std::chrono::milliseconds(1500)};
  ASSERT_THROW(SetSimulatedTime(timepoint), std::runtime_error);
}

// All the tests following this one have simulated clock enabled

TEST(TrellisTimeAPI, EnableSimTime) {
  ASSERT_FALSE(IsSimulatedClockEnabled());
  auto now = Now();
  // Real time is going to be some non-zero time
  ASSERT_TRUE(TimePointToSeconds(now) > 0.0);

  EnableSimulatedClock();

  ASSERT_TRUE(IsSimulatedClockEnabled());
  now = Now();
  // Simulated time is going to start at exactly zero
  ASSERT_TRUE(TimePointToSeconds(now) == 0.0);
}

TEST(TrellisTimeAPI, SetSimTime) {
  const auto now = Now();
  ASSERT_TRUE(TimePointToSeconds(now) == 0.0);

  SetSimulatedTime(now + std::chrono::milliseconds(1500));
  ASSERT_TRUE(NowInSeconds() == 1.5);
}

TEST(TrellisTimeAPI, IncrementSimTime) {
  const auto now = Now();

  IncrementSimulatedTime(std::chrono::milliseconds(1500));
  const auto future = Now();

  auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(future - now);
  ASSERT_EQ(delta.count(), 1500);
}

TEST(TrellisTimeAPI, SystemTimeConversion) {
  const TimePoint start(std::chrono::milliseconds(500));
  const TimePoint end(std::chrono::milliseconds(2500));

  SetSimulatedTime(start);
  const auto system_start = TimePointToSystemTime(start);
  const auto system_end = TimePointToSystemTime(end);

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

namespace {

auto MakeTimestamp(const int seconds, const int nanos) {
  auto ret = google::protobuf::Timestamp{};
  ret.set_seconds(seconds);
  ret.set_nanos(nanos);
  return ret;
}

auto MakeTimestampedMessage(const int seconds, const int nanos) {
  auto ret = TimestampedMessage{};
  *ret.mutable_timestamp() = MakeTimestamp(seconds, nanos);
  return ret;
}

}  // namespace

TEST(TrellisTimeAPI, TimePointFromTimestampedMessage) {
  const auto msg = MakeTimestampedMessage(11, 72);
  ASSERT_THAT(TimePointFromTimestampedMessage(msg), Eq(TimePoint{11s + 72ns}));
}

TEST(TrellisTimeAPI, TimePointFromTimestamp) {
  ASSERT_THAT(TimePointFromTimestamp(MakeTimestamp(12, 18)), Eq(TimePoint{12s + 18ns}));
  ASSERT_THAT(TimePointFromTimestamp(MakeTimestamp(-12, 18)), Eq(TimePoint{-12s + 18ns}));
}

TEST(TrellisTimeAPI, SystemTimePointFromTimestamp) {
  ASSERT_THAT(SystemTimePointFromTimestamp(MakeTimestamp(12, 18)), Eq(SystemTimePoint{12s + 18ns}));
  ASSERT_THAT(SystemTimePointFromTimestamp(MakeTimestamp(-12, 18)), Eq(SystemTimePoint{-12s + 18ns}));
}

TEST(TrellisTimeAPI, TimePointToTimestamp) {
  ASSERT_THAT(TimePointToTimestamp(TimePoint{12s + 18ns}), Eq(MakeTimestamp(12, 18)));
  ASSERT_THAT(TimePointToTimestamp(TimePoint{-12s + 18ns}), Eq(MakeTimestamp(-12, 18)))
      << "Protobuf timestamps should always have positive nanos.";
  ASSERT_THAT(TimePointToTimestamp(SystemTimePoint{12s + 18ns}), Eq(MakeTimestamp(12, 18)));
  ASSERT_THAT(TimePointToTimestamp(SystemTimePoint{-12s + 18ns}), Eq(MakeTimestamp(-12, 18)))
      << "Protobuf timestamps should always have positive nanos.";
}

}  // namespace trellis::core::time
