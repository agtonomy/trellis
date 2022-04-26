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

#include "trellis/core/timestamped_message.pb.h"

namespace trellis {
namespace core {
namespace time {

using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

/**
 * now Get the current time
 * @return A time point with millisecond resolution representing the current time
 */
TimePoint Now();

/**
 * TimePointToSeconds Convert a timepoint to seconds
 * @param tp the timepoint to convert
 * @return the equivalent value in seconds
 */
double TimePointToSeconds(const TimePoint& tp);

/**
 * NowInSeconds Get the current time in seconds
 * @return a the time in seconds since Unix epoch as a floating point value
 */
double NowInSeconds();

/**
 * TimePointToNanoseconds Convert a timepoint to nanoseconds
 * @param tp the timepoint to convert
 * @return the equivalent value in nanoseconds
 */
unsigned long long TimePointToNanoseconds(const TimePoint& tp);

/**
 * NowInNanoseconds Get the current time in nanoseconds
 * @return a the time in nanoseconds since Unix epoch as an unsigned long long
 */
unsigned long long NowInNanoseconds();

/**
 * TimePointToMilliseconds Convert a timepoint to nanoseconds
 * @param tp the timepoint to convert
 * @return the equivalent value in nanoseconds
 */
unsigned long long TimePointToMilliseconds(const TimePoint& tp);

/**
 * NowInMilliseconds Get the current time in milliseconds
 * @return a the time in milliseconds since Unix epoch as an unsigned long long
 */
unsigned long long NowInMilliseconds();

/**
 * TimePointFromFromTimestampedMessage create a time point from a TimestampedMessage
 *
 */
TimePoint TimePointFromFromTimestampedMessage(const trellis::core::TimestampedMessage& msg);

/**
 * TimePointToTimestamp create a google::protobuf::Timestamp from a time point
 */
google::protobuf::Timestamp TimePointToTimestamp(const trellis::core::time::TimePoint& tp);

/**
 * EnableSimulatedClock enable the simulated system clock
 *
 * Subsequent calls to Now() will return a simulated time
 */
void EnableSimulatedClock();

/**
 * IsSimulatedClockEnabled returns true if the simulated clock is running
 */
bool IsSimulatedClockEnabled();

/**
 * SetSimulatedTime set the simulated system clock to the given time
 *
 * @param now the time point to set the simulated clock with
 */
void SetSimulatedTime(const trellis::core::time::TimePoint& now);

/**
 * IncrementSimulatedTime increment simulated time
 *
 * @param duration a time duration in milliseconds
 */
void IncrementSimulatedTime(const std::chrono::milliseconds& duration);

/**
 * TimePointFromFromTimestampedMessage create a time point from a TimestampedMessage
 *
 */
TimePoint TimePointFromFromTimestampedMessage(const trellis::core::TimestampedMessage& msg);

/**
 * TimePointToTimestamp create a google::protobuf::Timestamp from a time point
 */
google::protobuf::Timestamp TimePointToTimestamp(const trellis::core::time::TimePoint& tp);

}  // namespace time
}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_TIME_HPP
