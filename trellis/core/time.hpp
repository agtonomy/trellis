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

#ifndef TRELLIS_CORE_TIME_HPP_
#define TRELLIS_CORE_TIME_HPP_

#include "time_types.hpp"
#include "trellis/core/timestamped_message.pb.h"

namespace trellis {
namespace core {
namespace time {

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
 * TimePointFromTimestampedMessage create a time point from a TimestampedMessage
 *
 */
TimePoint TimePointFromTimestampedMessage(const trellis::core::TimestampedMessage& msg);

/**
 * TimePointFromTimestamp create a time point from a Timestamp
 *
 */
TimePoint TimePointFromTimestamp(const google::protobuf::Timestamp& timestamp);

/**
 * @brief Create a time point from a Timestamp
 */
SystemTimePoint SystemTimePointFromTimestamp(const google::protobuf::Timestamp& timestamp);

/**
 * TimePointToTimestamp create a google::protobuf::Timestamp from a time point
 */
google::protobuf::Timestamp TimePointToTimestamp(const TimePoint& tp);

/**
 * @brief Create a google::protobuf::Timestamp from a system time point
 */
google::protobuf::Timestamp SystemTimePointToTimestamp(const SystemTimePoint& tp);

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
void SetSimulatedTime(const TimePoint& now);

/**
 * IncrementSimulatedTime increment simulated time
 *
 * @param duration a time duration in milliseconds
 */
void IncrementSimulatedTime(const std::chrono::milliseconds& duration);

/**
 * TimePointFromTimestampedMessage create a time point from a TimestampedMessage
 *
 */
TimePoint TimePointFromTimestampedMessage(const trellis::core::TimestampedMessage& msg);

/**
 * @brief TimePointToSystemTimeOffset is the duration offset of system time minus trellis time (steady clock).
 *
 * @return TimePoint::duration The duration offset.
 */
TimePoint::duration TimePointToSystemTimeOffset();

/**
 * @brief TimePointToSystemTime get the system time associated with a given TimePoint.
 *
 * @param time The input TimePoint.
 * @return SystemTimePoint The corresponding SystemTimePoint.
 */
SystemTimePoint TimePointToSystemTime(const TimePoint& time);

/**
 * @brief TimePointToSystemTime get the TimePoint associated with a given SystemTimePoint.
 *
 * @param time The input SystemTimePoint.
 * @return TimePoint The corresponding TimePoint.
 */
TimePoint TimePointFromSystemTime(const SystemTimePoint& time);

}  // namespace time
}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_TIME_HPP_
