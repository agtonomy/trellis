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

#include "trellis/core/node.hpp"

using namespace trellis::core;

TEST(TrellisSimulatedClock, UpdateSimulatedClockTicksTimers) {
  time::EnableSimulatedClock();

  trellis::core::Node node("test_simtime");

  const unsigned timer1_interval{1000u};
  const unsigned timer2_interval{333u};
  const unsigned timer3_interval{220u};

  unsigned timer1_ticks{0};
  unsigned timer2_ticks{0};
  unsigned timer3_ticks{0};
  time::TimePoint last_now;

  node.CreateTimer(timer1_interval, [&timer1_ticks, &last_now]() {
    ++timer1_ticks;
    auto now = time::Now();
    ASSERT_EQ(time::TimePointToMilliseconds(now), timer1_ticks * timer1_interval);
    std::cout << "Timer 1 now = " << time::TimePointToMilliseconds(now) << std::endl;
    ASSERT_TRUE(time::TimePointToMilliseconds(now) > time::TimePointToMilliseconds(last_now));
    last_now = now;
  });

  node.CreateTimer(timer2_interval, [&timer2_ticks, &last_now]() {
    ++timer2_ticks;
    auto now = time::Now();
    ASSERT_EQ(time::TimePointToMilliseconds(now), timer2_ticks * timer2_interval);
    std::cout << "Timer 2 now = " << time::TimePointToMilliseconds(now) << std::endl;
    ASSERT_TRUE(time::TimePointToMilliseconds(now) > time::TimePointToMilliseconds(last_now));
    last_now = now;
  });

  node.CreateTimer(timer3_interval, [&timer3_ticks, &last_now]() {
    ++timer3_ticks;
    auto now = time::Now();
    ASSERT_EQ(time::TimePointToMilliseconds(now), timer3_ticks * timer3_interval);
    std::cout << "Timer 3 now = " << time::TimePointToMilliseconds(now) << std::endl;
    ASSERT_TRUE(time::TimePointToMilliseconds(now) > time::TimePointToMilliseconds(last_now));
    last_now = now;
  });

  time::TimePoint time{time::Now() + std::chrono::milliseconds(10500)};
  node.UpdateSimulatedClock(time);

  // We jumped forward in time 10500 milliseconds, so...
  // our 1000ms timer should have fired 10 times
  // our 333ms timer should have fired 31 times
  // our 220ms timer should have fired 47 times
  ASSERT_EQ(timer1_ticks, 10);
  ASSERT_EQ(timer2_ticks, 31);
  ASSERT_EQ(timer3_ticks, 47);
}
