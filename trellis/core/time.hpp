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

#ifndef TRELLIS_CORE_TIME_HPP
#define TRELLIS_CORE_TIME_HPP

#include <ecal/ecal_time.h>

#include "trellis/core/timestamped_message.pb.h"

namespace trellis {
namespace core {
namespace time {

using TimePoint = std::chrono::time_point<eCAL::Time::ecal_clock>;

/**
 * now Get the current time
 * @return A time point with millisecond resolution representing the current time
 */
inline TimePoint Now() { return eCAL::Time::ecal_clock::now(); }

/**
 * TimePointToSeconds Convert a timepoint to seconds
 * @param tp the timepoint to convert
 * @return the equivalent value in seconds
 */
inline double TimePointToSeconds(const TimePoint& tp) {
  return (tp.time_since_epoch().count() *
          (static_cast<double>(TimePoint::period::num) / static_cast<double>(TimePoint::period::den)));
}

/**
 * NowInSeconds Get the current time in seconds
 * @return a the time in seconds since Unix epoch as a floating point value
 */
inline double NowInSeconds() { return TimePointToSeconds(Now()); }

/**
 * TimePointToNanoseconds Convert a timepoint to nanoseconds
 * @param tp the timepoint to convert
 * @return the equivalent value in nanoseconds
 */
inline unsigned long long TimePointToNanoseconds(const TimePoint& tp) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count();
}

/**
 * NowInNanoseconds Get the current time in nanoseconds
 * @return a the time in nanoseconds since Unix epoch as an unsigned long long
 */
inline unsigned long long NowInNanoseconds() { return TimePointToNanoseconds(Now()); }

/**
 * TimePointToMilliseconds Convert a timepoint to nanoseconds
 * @param tp the timepoint to convert
 * @return the equivalent value in nanoseconds
 */
inline unsigned long long TimePointToMilliseconds(const TimePoint& tp) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

/**
 * NowInMilliseconds Get the current time in milliseconds
 * @return a the time in milliseconds since Unix epoch as an unsigned long long
 */
inline unsigned long long NowInMilliseconds() { return TimePointToMilliseconds(Now()); }

}  // namespace time
}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_TIME_HPP
