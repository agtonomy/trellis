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

#include "trellis/core/time_types.hpp"
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
 * @brief NanosecondsToTimePoint Create a time point from nanoseconds
 *
 * @param nanoseconds a timestamp in nanoseconds from the time base
 * @return TimePoint a time point representing the given time in nanoseconds
 */
TimePoint NanosecondsToTimePoint(unsigned long long nanoseconds);

/**
 * NowInNanoseconds Get the current time in nanoseconds
 * @return the time in nanoseconds since Unix epoch as an unsigned long long
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
 * @return the time in milliseconds since Unix epoch as an unsigned long long
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
 * DisableSimulatedClock disable the simulated system clock
 * @see EnableSimulatedClock
 */
void DisableSimulatedClock();

/**
 * IsSimulatedClockEnabled returns true if the simulated clock is running
 */
bool IsSimulatedClockEnabled();

/**
 * SetSimulatedTime set the simulated system clock to the given time
 *
 * Sets the clock unconditionally, backwards included, which is what resetting it needs. Code that advances the clock
 * while more than one thread may be advancing it should use AdvanceSimulatedTime instead.
 *
 * @param now the time point to set the simulated clock with
 */
void SetSimulatedTime(const TimePoint& now);

/**
 * AdvanceSimulatedTime move the simulated system clock forward to the given time, or leave it if it is already there
 *
 * The clock is process-global, and a process can run several event loops that each step it from their own thread.
 * This is the one way for them to move it: it is atomic and never moves the clock backwards, so whichever loop is
 * furthest ahead sets the time every loop reads.
 *
 * That changes what a loop that falls behind sees, compared with the same node in a process of its own:
 * - Its clock keeps moving while it is busy. In a process of its own, the clock showed the newest message the node
 *   had received, so it stood still during a long callback. Here it shows the furthest-ahead loop's time, so a
 *   watchdog or a "now minus the message's stamp" check also counts the time other loops moved it on.
 * - Its timers can fire late, all at the same time. A step moves the clock to each due timer's expiry before firing
 *   it, but another loop has often already moved the clock past those expiries. They then fire one after another at
 *   time::Now(), so a kCatchUp timer that fell several slots behind replays them all at the same time, and a
 *   callback that measures the time between its calls with time::Now() sees zero, then the whole gap.
 *
 * @param time the time to advance the simulated clock to
 * @return the simulated time after the call, which is time or a later time another thread had already set
 * @throws std::runtime_error if the simulated clock is not enabled
 */
TimePoint AdvanceSimulatedTime(const TimePoint& time);

/**
 * IsUnsetSimulatedTime returns true if a reading of the simulated clock is from before the clock was ever set
 *
 * The clock starts at 0, so a reading under a millisecond counts as unset. Pass the reading an expiry or decision was
 * computed from rather than calling Now() again: another loop's thread may set the clock in between.
 *
 * @param time a reading of time::Now() under the simulated clock
 */
bool IsUnsetSimulatedTime(const TimePoint& time);

/**
 * IncrementSimulatedTime increment simulated time
 *
 * Like SetSimulatedTime, this does not keep the clock from moving backwards for a negative duration; code that
 * advances the clock while other threads may also be advancing it should use AdvanceSimulatedTime instead.
 *
 * @param duration a time duration in milliseconds
 * @throws std::runtime_error if the simulated clock is not enabled
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
