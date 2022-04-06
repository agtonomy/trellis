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

#include "trellis/core/message_consumer.hpp"
#include "trellis/core/node.hpp"
#include "trellis/core/test/test.pb.h"
#include "trellis/core/test/test_fixture.hpp"

using namespace trellis::core;
using namespace trellis::core::test;

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

TEST_F(TrellisFixture, TestSimulatedTimeWithSubscribersAndWatchdogsAndTimers) {
  static constexpr unsigned send_count{5u};
  static constexpr unsigned timer1_interval{10u};
  static unsigned receive_count_1{0};
  static unsigned receive_count_2{0};
  static unsigned watchdog_count_1{0};
  static unsigned watchdog_count_2{0};
  static unsigned timer_ticks{0};

  time::EnableSimulatedClock();
  StartRunnerThread();

  // Create our two publishers
  auto pub = node_.CreatePublisher<test::Test>("simtime_topic_1");
  auto pub2 = node_.CreatePublisher<test::TestTwo>("simtime_topic_2");

  // Create our message consumer to consume from the two publishers
  trellis::core::MessageConsumer<send_count, test::Test, test::TestTwo> inputs_{
      node_,
      {{"simtime_topic_1", "simtime_topic_2"}},
      {[this](const std::string& topic, const test::Test& msg, const time::TimePoint& msgtime) {
         ASSERT_EQ(topic, "simtime_topic_1");
         ++receive_count_1;
       },
       [this](const std::string& topic, const test::TestTwo& msg, const time::TimePoint& msgtime) {
         ASSERT_EQ(topic, "simtime_topic_2");
         ++receive_count_2;
       }},
      {{50U, 100U}},
      {{[](const std::string& topic) {
          ++watchdog_count_1;
          ASSERT_EQ(topic, "simtime_topic_1");
        },
        [](const std::string& topic) {
          ++watchdog_count_2;
          ASSERT_EQ(topic, "simtime_topic_2");
        }}}};

  WaitForDiscovery();

  // Reset sim time
  time::SetSimulatedTime(time::TimePoint{std::chrono::milliseconds(0)});

  node_.CreateTimer(timer1_interval, []() {
    ++timer_ticks;
  });

  // Publish messages on both topics
  time::TimePoint now{time::Now()};
  for (unsigned i = 0; i < send_count; ++i) {
    test::Test test_msg;
    test::TestTwo test_msg2;
    test_msg.set_id(i);
    test_msg.set_msg("hello world");
    test_msg2.set_foo(i * 2.0);

    now += std::chrono::milliseconds(20);
    pub->Send(test_msg, now);
    now += std::chrono::milliseconds(20);
    pub2->Send(test_msg2, now);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  ASSERT_EQ(watchdog_count_1, 0U);
  ASSERT_EQ(watchdog_count_2, 0U);

  const unsigned expected_timer_ticks = (2 * 20 * send_count) / timer1_interval;
  ASSERT_EQ(timer_ticks, expected_timer_ticks);

  // Advance the time past the watchdog
  node_.UpdateSimulatedClock(time::Now() + std::chrono::milliseconds(200));

  ASSERT_EQ(receive_count_1, send_count);
  ASSERT_EQ(receive_count_2, send_count);

  ASSERT_EQ(watchdog_count_1, 1U);
  ASSERT_EQ(watchdog_count_2, 1U);

  now = time::Now();
  for (unsigned i = 0; i < send_count; ++i) {
    test::Test test_msg;
    test::TestTwo test_msg2;
    test_msg.set_id(i);
    test_msg.set_msg("hello world");
    test_msg2.set_foo(i * 2.0);

    now += std::chrono::milliseconds(20);
    pub->Send(test_msg, now);
    now += std::chrono::milliseconds(20);
    pub2->Send(test_msg2, now);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  ASSERT_EQ(receive_count_1, 2 * send_count);
  ASSERT_EQ(receive_count_2, 2 * send_count);

  // Advance past the watchdog
  node_.UpdateSimulatedClock(time::Now() + std::chrono::milliseconds(200));

  ASSERT_EQ(watchdog_count_1, 2U);
  ASSERT_EQ(watchdog_count_2, 2U);
}
