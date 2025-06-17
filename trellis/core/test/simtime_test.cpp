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

TEST(TrellisSimulatedClock, UpdateSimulatedClockTicksTimers) {
  trellis::core::time::EnableSimulatedClock();
  trellis::core::time::SetSimulatedTime(trellis::core::time::TimePoint{});  // reset time

  trellis::core::Node node("test_simtime", {});

  const unsigned timer1_interval{1000u};
  const unsigned timer2_interval{333u};
  const unsigned timer3_interval{220u};
  static constexpr unsigned time_jump_ms{10500};

  unsigned timer1_ticks{0};
  unsigned timer2_ticks{0};
  unsigned timer3_ticks{0};
  trellis::core::time::TimePoint last_now;

  auto timer1 =
      node.CreateTimer(timer1_interval, [&timer1_ticks, &last_now](const trellis::core::time::TimePoint& now) {
        ++timer1_ticks;
        ASSERT_EQ(trellis::core::time::TimePointToMilliseconds(now), (timer1_ticks * timer1_interval) + time_jump_ms);
        ASSERT_TRUE(trellis::core::time::TimePointToMilliseconds(now) >
                    trellis::core::time::TimePointToMilliseconds(last_now));
        last_now = now;
      });

  auto timer2 =
      node.CreateTimer(timer2_interval, [&timer2_ticks, &last_now](const trellis::core::time::TimePoint& now) {
        ++timer2_ticks;
        ASSERT_EQ(trellis::core::time::TimePointToMilliseconds(now), (timer2_ticks * timer2_interval) + time_jump_ms);
        ASSERT_TRUE(trellis::core::time::TimePointToMilliseconds(now) >
                    trellis::core::time::TimePointToMilliseconds(last_now));
        last_now = now;
      });

  auto timer3 =
      node.CreateTimer(timer3_interval, [&timer3_ticks, &last_now](const trellis::core::time::TimePoint& now) {
        ++timer3_ticks;
        ASSERT_EQ(trellis::core::time::TimePointToMilliseconds(now), (timer3_ticks * timer3_interval) + time_jump_ms);
        ASSERT_TRUE(trellis::core::time::TimePointToMilliseconds(now) >
                    trellis::core::time::TimePointToMilliseconds(last_now));
        last_now = now;
      });

  // The first time we update the time, we're essentially resetting all the timers
  trellis::core::time::TimePoint time{trellis::core::time::Now() + std::chrono::milliseconds(time_jump_ms)};
  node.UpdateSimulatedClock(time);
  node.RunOnce();  // kick the event loop

  ASSERT_EQ(timer1_ticks, 0);
  ASSERT_EQ(timer2_ticks, 0);
  ASSERT_EQ(timer3_ticks, 0);

  // Now we're moving forward in time, so our timers should fire accordingly
  time += std::chrono::milliseconds(time_jump_ms);
  node.UpdateSimulatedClock(time);
  node.RunOnce();  // kick the event loop

  // We jumped forward in time 10500 milliseconds, so...
  // our 1000ms timer should have fired 10 times
  // our 333ms timer should have fired 31 times
  // our 220ms timer should have fired 47 times
  ASSERT_EQ(timer1_ticks, 10);
  ASSERT_EQ(timer2_ticks, 31);
  ASSERT_EQ(timer3_ticks, 47);
}

TEST(TrellisSimulatedClock, UpdateSimulatedClockTicksOneShotTimersOnce) {
  trellis::core::time::EnableSimulatedClock();
  trellis::core::time::SetSimulatedTime(trellis::core::time::TimePoint{});  // reset time

  trellis::core::Node node("test_simtime", {});

  const unsigned timer1_interval{1000u};
  const unsigned timer2_interval{333u};
  const unsigned timer3_interval{220u};
  static constexpr unsigned time_jump_ms{1000u};  // matches longest interval

  unsigned timer1_ticks{0};
  unsigned timer2_ticks{0};
  unsigned timer3_ticks{0};
  trellis::core::time::TimePoint last_now;

  auto timer1 =
      node.CreateOneShotTimer(timer1_interval, [&timer1_ticks, &last_now](const trellis::core::time::TimePoint& now) {
        ++timer1_ticks;
        ASSERT_GT(now, last_now);
        last_now = now;
      });

  auto timer2 =
      node.CreateOneShotTimer(timer2_interval, [&timer2_ticks, &last_now](const trellis::core::time::TimePoint& now) {
        ++timer2_ticks;
        ASSERT_GT(now, last_now);
        last_now = now;
      });

  auto timer3 =
      node.CreateOneShotTimer(timer3_interval, [&timer3_ticks, &last_now](const trellis::core::time::TimePoint& now) {
        ++timer3_ticks;
        ASSERT_GT(now, last_now);
        last_now = now;
      });

  // The first time we update the time, we're essentially resetting all the timers
  trellis::core::time::TimePoint time{trellis::core::time::Now() + std::chrono::milliseconds(time_jump_ms)};
  node.UpdateSimulatedClock(time);
  node.RunOnce();  // kick the event loop

  ASSERT_EQ(timer1_ticks, 0);
  ASSERT_EQ(timer2_ticks, 0);
  ASSERT_EQ(timer3_ticks, 0);

  // Now we're moving forward in time, so our timers should fire accordingly
  time += std::chrono::milliseconds(time_jump_ms);
  node.UpdateSimulatedClock(time);
  node.RunOnce();  // kick the event loop

  // One shot timers should have fired once
  ASSERT_EQ(timer1_ticks, 1);
  ASSERT_EQ(timer2_ticks, 1);
  ASSERT_EQ(timer3_ticks, 1);

  // Now we're moving forward in time, but our one shot timers are expired
  time += std::chrono::milliseconds(time_jump_ms);
  node.UpdateSimulatedClock(time);
  node.RunOnce();  // kick the event loop

  ASSERT_EQ(timer1_ticks, 1);
  ASSERT_EQ(timer2_ticks, 1);
  ASSERT_EQ(timer3_ticks, 1);

  // Reset the timers
  timer1->Reset();
  timer2->Reset();
  timer3->Reset();

  // Now we're moving forward in time and our timers are reset, so our timers should fire accordingly
  time += std::chrono::milliseconds(time_jump_ms);
  node.UpdateSimulatedClock(time);
  node.RunOnce();  // kick the event loop

  // One shot timers should have fired once more
  ASSERT_EQ(timer1_ticks, 2);
  ASSERT_EQ(timer2_ticks, 2);
  ASSERT_EQ(timer3_ticks, 2);
}
