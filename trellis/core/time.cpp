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

#include "time.hpp"

namespace trellis {
namespace core {
namespace time {

namespace {

bool sim_clock_enabled_{false};
TimePoint simulated_now_{};

}  // namespace

TimePoint Now() { return sim_clock_enabled_ ? simulated_now_ : std::chrono::steady_clock::now(); }

double TimePointToSeconds(const TimePoint& tp) {
  return (tp.time_since_epoch().count() *
          (static_cast<double>(TimePoint::period::num) / static_cast<double>(TimePoint::period::den)));
}

double NowInSeconds() { return TimePointToSeconds(Now()); }

unsigned long long TimePointToNanoseconds(const TimePoint& tp) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count();
}

unsigned long long NowInNanoseconds() { return TimePointToNanoseconds(Now()); }

unsigned long long TimePointToMilliseconds(const TimePoint& tp) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

unsigned long long NowInMilliseconds() { return TimePointToMilliseconds(Now()); }

TimePoint TimePointFromTimestampedMessage(const trellis::core::TimestampedMessage& msg) {
  return TimePointFromTimestamp(msg.timestamp());
}

TimePoint TimePointFromTimestamp(const google::protobuf::Timestamp& timestamp) {
  return TimePoint(std::chrono::seconds(timestamp.seconds()) + std::chrono::nanoseconds(timestamp.nanos()));
}

google::protobuf::Timestamp TimePointToTimestamp(const TimePoint& tp) {
  google::protobuf::Timestamp ts;
  auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch());
  const auto duration_seconds = std::chrono::duration_cast<std::chrono::seconds>(duration_ns);
  duration_ns -= duration_seconds;
  ts.set_seconds(duration_seconds.count());
  ts.set_nanos(duration_ns.count());
  return ts;
}

void EnableSimulatedClock() { sim_clock_enabled_ = true; }

bool IsSimulatedClockEnabled() { return sim_clock_enabled_; }

void SetSimulatedTime(const TimePoint& now) {
  if (!sim_clock_enabled_) {
    throw std::runtime_error("Attempt to set simulated time while sim_clock_enabled_ = false");
  }
  simulated_now_ = now;
}

void IncrementSimulatedTime(const std::chrono::milliseconds& duration) { SetSimulatedTime(simulated_now_ + duration); }

SystemTimePoint TimePointToSystemTime(const TimePoint& time) {
  // Subtract a duration in trellis clock domain (steady clock) from current system clock
  // to get the equivalent time in system clock domain
  return std::chrono::system_clock::now() +
         std::chrono::duration_cast<std::chrono::system_clock::duration>(time - Now());
}

}  // namespace time
}  // namespace core
}  // namespace trellis
