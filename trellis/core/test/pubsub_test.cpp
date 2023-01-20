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

#include <iostream>

#include "trellis/core/test/test.pb.h"
#include "trellis/core/test/test_fixture.hpp"

using namespace trellis::core;
using namespace trellis::core::test;

TEST_F(TrellisFixture, BasicPubSub) {
  static unsigned receive_count{0};

  auto pub = node_.CreatePublisher<test::Test>("test_topic");
  auto sub = node_.CreateSubscriber<test::Test>(
      "test_topic",
      [](const time::TimePoint&, const time::TimePoint&, trellis::core::SubscriberImpl<test::Test>::PointerType msg) {
        ASSERT_EQ(msg->id(), receive_count);
        ++receive_count;
      });

  StartRunnerThread();
  WaitForDiscovery();
  ASSERT_FALSE(node_.GetEventLoop().Stopped());

  // Sanity check initial value
  ASSERT_EQ(receive_count, 0U);

  for (unsigned i = 0; i < 10U; ++i) {
    test::Test test_msg;
    test_msg.set_id(i);
    test_msg.set_msg("hello world");
    pub->Send(test_msg);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  ASSERT_EQ(receive_count, 10U);
}

TEST_F(TrellisFixture, SubscriberWatchdogTimeout) {
  static unsigned receive_count{0};
  static unsigned watchdog_count{0};

  auto pub = node_.CreatePublisher<test::Test>("test_watchdog_topic");
  auto sub = node_.CreateSubscriber<test::Test>(
      "test_watchdog_topic",
      [](const time::TimePoint&, const time::TimePoint&, trellis::core::SubscriberImpl<test::Test>::PointerType msg) {
        ASSERT_EQ(msg->id(), receive_count);
        ++receive_count;
      },
      50, [](const trellis::core::time::TimePoint&) { ++watchdog_count; });

  StartRunnerThread();
  WaitForDiscovery();
  ASSERT_FALSE(node_.GetEventLoop().Stopped());

  // Sanity check initial values
  ASSERT_EQ(receive_count, 0U);
  ASSERT_EQ(watchdog_count, 0U);

  // Send 2 messages
  for (unsigned i = 0; i < 2U; ++i) {
    test::Test test_msg;
    test_msg.set_id(i);
    test_msg.set_msg("hello world");
    pub->Send(test_msg);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Expect two messages received
  ASSERT_EQ(receive_count, 2U);
  ASSERT_EQ(watchdog_count, 0U);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Expect watchdog fired
  ASSERT_EQ(receive_count, 2U);
  ASSERT_EQ(watchdog_count, 1U);

  {
    test::Test test_msg;
    test_msg.set_id(2);
    test_msg.set_msg("hello world");
    pub->Send(test_msg);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Expect third message received
  ASSERT_EQ(receive_count, 3U);
  ASSERT_EQ(watchdog_count, 1U);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Expect another watchdog fire
  ASSERT_EQ(receive_count, 3U);
  ASSERT_EQ(watchdog_count, 2U);
}

TEST_F(TrellisFixture, SubscriberThrottle) {
  static unsigned receive_count{0};
  static unsigned sent_count{0};

  auto pub = node_.CreatePublisher<test::Test>("test_throttle_topic");
  auto sub = node_.CreateSubscriber<test::Test>(
      "test_throttle_topic",
      [](const time::TimePoint&, const time::TimePoint&, trellis::core::SubscriberImpl<test::Test>::PointerType msg) {
        ASSERT_TRUE(msg->id() >= receive_count);
        ++receive_count;
      },
      {}, {}, 100.0);

  StartRunnerThread();
  WaitForDiscovery();
  ASSERT_FALSE(node_.GetEventLoop().Stopped());

  for (unsigned i = 0; i < 20U; ++i) {
    test::Test test_msg;
    test_msg.set_id(i);
    test_msg.set_msg("hello world");
    pub->Send(test_msg);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ++sent_count;
  }

  ASSERT_TRUE(sent_count > 0);

  // We should have received some
  ASSERT_TRUE(receive_count > 0 && receive_count <= 3);
  ASSERT_TRUE(receive_count <= 3);

  // ...and it should be less than we sent
  ASSERT_TRUE(sent_count > receive_count);
}

TEST_F(TrellisFixture, SendReturnsTimestamp) {
  trellis::core::time::EnableSimulatedClock();
  const trellis::core::time::TimePoint time{trellis::core::time::TimePoint(std::chrono::milliseconds(1337))};
  trellis::core::time::SetSimulatedTime(time);

  auto pub = node_.CreatePublisher<test::Test>("test_send_timestamp_topic");

  test::Test test_msg;
  test_msg.set_msg("hello world");
  auto send_time = pub->Send(test_msg);

  ASSERT_EQ(send_time, time);
}
