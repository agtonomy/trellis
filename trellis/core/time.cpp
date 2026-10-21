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

#include <atomic>
#include <mutex>
#include <stdexcept>

namespace trellis {
namespace core {
namespace time {

namespace {

// Atomic because a process can run several event loops, each stepping the one simulated clock from its own thread.
// Readers load both with std::memory_order_relaxed, since Now() is on every hot path. sim_clock_enabled_ is set before
// the loop threads start, and starting a thread orders that write before the thread's reads; toggling it while timers
// exist is unsupported. simulated_now_ is only moved forward, under writer_mutex_, so a relaxed load still never sees
// the clock go back. Other data reaches a loop through the IPC that delivers it, never through Now().
std::atomic<bool> sim_clock_enabled_{false};
std::atomic<TimePoint> simulated_now_{TimePoint{}};
std::atomic<TimePoint::duration> cached_sim_offset_{TimePoint::duration{}};
static_assert(std::atomic<TimePoint>::is_always_lock_free);
static_assert(std::atomic<TimePoint::duration>::is_always_lock_free);

// Serializes the writers, so the offset cached last is always the one computed from the time set last. Without it, two
// threads' writes could interleave and leave the offset of the earlier time next to the later one. Readers don't take
// it: Now() stays a lock-free load.
std::mutex writer_mutex_;

// Call with writer_mutex_ held.
void StoreSimulatedTime(const TimePoint& now) {
  simulated_now_ = now;
  cached_sim_offset_ = std::chrono::system_clock::now().time_since_epoch() - now.time_since_epoch();
}

}  // namespace

TimePoint Now() {
  return sim_clock_enabled_.load(std::memory_order_relaxed) ? simulated_now_.load(std::memory_order_relaxed)
                                                            : std::chrono::steady_clock::now();
}

double TimePointToSeconds(const TimePoint& tp) {
  return (tp.time_since_epoch().count() *
          (static_cast<double>(TimePoint::period::num) / static_cast<double>(TimePoint::period::den)));
}

double NowInSeconds() { return TimePointToSeconds(Now()); }

unsigned long long TimePointToNanoseconds(const TimePoint& tp) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count();
}

TimePoint NanosecondsToTimePoint(unsigned long long nanoseconds) {
  return TimePoint(std::chrono::nanoseconds(nanoseconds));
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
  return TimePoint{std::chrono::seconds{timestamp.seconds()} + std::chrono::nanoseconds{timestamp.nanos()}};
}

SystemTimePoint SystemTimePointFromTimestamp(const google::protobuf::Timestamp& timestamp) {
  return SystemTimePoint{std::chrono::seconds{timestamp.seconds()} + std::chrono::nanoseconds{timestamp.nanos()}};
}

namespace {

template <typename TimePointType>
auto TimePointToTimestampImpl(const TimePointType& tp) {
  auto ret = google::protobuf::Timestamp{};

  const auto duration_ns = std::chrono::round<std::chrono::nanoseconds>(tp.time_since_epoch());
  // Floor since Timestamp should always have positive nanosecond portion.
  const auto duration_seconds = std::chrono::floor<std::chrono::seconds>(tp.time_since_epoch());
  ret.set_seconds(duration_seconds.count());
  ret.set_nanos((duration_ns - duration_seconds).count());
  return ret;
}

}  // namespace

google::protobuf::Timestamp TimePointToTimestamp(const TimePoint& tp) { return TimePointToTimestampImpl(tp); }

google::protobuf::Timestamp SystemTimePointToTimestamp(const SystemTimePoint& tp) {
  return TimePointToTimestampImpl(tp);
}

void EnableSimulatedClock() {
  std::lock_guard guard(writer_mutex_);
  sim_clock_enabled_ = true;
  StoreSimulatedTime(simulated_now_.load());
}

void DisableSimulatedClock() { sim_clock_enabled_ = false; }

bool IsSimulatedClockEnabled() { return sim_clock_enabled_.load(std::memory_order_relaxed); }

void SetSimulatedTime(const TimePoint& now) {
  if (!sim_clock_enabled_.load(std::memory_order_relaxed)) {
    throw std::runtime_error("Attempt to set simulated time while sim_clock_enabled_ = false");
  }
  std::lock_guard guard(writer_mutex_);
  StoreSimulatedTime(now);
}

TimePoint AdvanceSimulatedTime(const TimePoint& time) {
  if (!sim_clock_enabled_.load(std::memory_order_relaxed)) {
    throw std::runtime_error("Attempt to advance simulated time while sim_clock_enabled_ = false");
  }
  std::lock_guard guard(writer_mutex_);
  if (simulated_now_.load() < time) {
    StoreSimulatedTime(time);
  }
  return simulated_now_.load();
}

bool IsUnsetSimulatedTime(const TimePoint& time) { return TimePointToMilliseconds(time) == 0; }

void IncrementSimulatedTime(const std::chrono::milliseconds& duration) {
  if (!sim_clock_enabled_.load(std::memory_order_relaxed)) {
    throw std::runtime_error("Attempt to set simulated time while sim_clock_enabled_ = false");
  }
  // Read and written under the one lock, so a concurrent writer's time is never lost.
  std::lock_guard guard(writer_mutex_);
  StoreSimulatedTime(simulated_now_.load() + duration);
}

TimePoint::duration TimePointToSystemTimeOffset() {
  if (sim_clock_enabled_.load(std::memory_order_relaxed)) {
    return cached_sim_offset_;
  }
  return std::chrono::system_clock::now().time_since_epoch() - Now().time_since_epoch();
}

SystemTimePoint TimePointToSystemTime(const TimePoint& time) {
  // Add a duration in trellis clock domain (steady clock) to current system clock to get the equivalent time in system
  // clock domain.
  return SystemTimePoint{(time + TimePointToSystemTimeOffset()).time_since_epoch()};
}

TimePoint TimePointFromSystemTime(const SystemTimePoint& time) {
  // Add a duration in system clock domain to current trellis clock to get the equivalent time in trellis clock domain
  // (steady clock).
  return TimePoint{(time - TimePointToSystemTimeOffset()).time_since_epoch()};
}

}  // namespace time
}  // namespace core
}  // namespace trellis
