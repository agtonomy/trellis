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

#include "trellis/core/test/test.hpp"
#include "trellis/core/test/test.pb.h"
#include "trellis/core/test/test_fixture.hpp"

using namespace trellis::core;
using namespace trellis::core::test;

static constexpr std::chrono::milliseconds kProcessEventsWaitTime(500U);
static constexpr unsigned kWatchdogTimeoutMs{1000u};

TEST_F(TrellisFixture, PubSubBurst) {
  unsigned receive_count{0};
  std::mutex count_mutex;
  std::condition_variable count_cv;
  constexpr unsigned kExpectedMessages = 10U;

  auto pub = GetNode().CreatePublisher<test::Test>("test_topic");
  auto sub = GetNode().CreateSubscriber<test::Test>(
      "test_topic",
      [&receive_count, &count_mutex, &count_cv](const time::TimePoint&, const time::TimePoint&,
                                                trellis::core::SubscriberImpl<test::Test>::MsgTypePtr msg) {
        std::lock_guard<std::mutex> lock(count_mutex);
        ASSERT_EQ(msg->id(), receive_count);
        ++receive_count;

        // Notify waiting thread when we've received all messages
        if (receive_count == kExpectedMessages) {
          count_cv.notify_one();
        }
      });

  StartRunnerThread();
  WaitForDiscovery();
  ASSERT_FALSE(GetNode().GetEventLoop().Stopped());

  // Sanity check initial value
  ASSERT_EQ(receive_count, 0U);

  for (unsigned i = 0; i < kExpectedMessages; ++i) {
    test::Test test_msg;
    test_msg.set_id(i);
    test_msg.set_msg("hello world");
    pub->Send(test_msg);
  }

  // Wait for all messages to be received, with timeout
  {
    std::unique_lock<std::mutex> lock(count_mutex);
    bool received_all = count_cv.wait_for(lock, kProcessEventsWaitTime,
                                          [&receive_count]() { return receive_count == kExpectedMessages; });
    ASSERT_TRUE(received_all) << "Timeout waiting for all messages. Received: " << receive_count << "/"
                              << kExpectedMessages;
  }

  ASSERT_EQ(receive_count, kExpectedMessages);
}

TEST_F(TrellisFixture, LargePublisher) {
  unsigned receive_count{0};
  std::mutex count_mutex;
  std::condition_variable count_cv;
  constexpr unsigned kExpectedMessages = 10U;

  auto pub = GetNode().CreatePublisher<test::Test>("test_topic");
  auto sub = GetNode().CreateSubscriber<test::Test>(
      "test_topic",
      [&receive_count, &count_mutex, &count_cv](const time::TimePoint&, const time::TimePoint&,
                                                trellis::core::SubscriberImpl<test::Test>::MsgTypePtr msg) {
        std::lock_guard<std::mutex> lock(count_mutex);
        ASSERT_EQ(msg->id(), receive_count);
        ++receive_count;

        // Notify waiting thread when we've received all messages
        if (receive_count == kExpectedMessages) {
          count_cv.notify_one();
        }
      });

  StartRunnerThread();
  WaitForDiscovery();
  ASSERT_FALSE(GetNode().GetEventLoop().Stopped());

  // Sanity check initial value
  ASSERT_EQ(receive_count, 0U);

  for (unsigned i = 0; i < kExpectedMessages; ++i) {
    test::Test test_msg;
    test_msg.set_id(i);
    test_msg.set_msg(std::string(226851, 'x'));
    pub->Send(test_msg);
  }

  // Wait for all messages to be received, with timeout
  {
    std::unique_lock<std::mutex> lock(count_mutex);
    bool received_all = count_cv.wait_for(lock, kProcessEventsWaitTime,
                                          [&receive_count]() { return receive_count == kExpectedMessages; });
    ASSERT_TRUE(received_all) << "Timeout waiting for all messages. Received: " << receive_count << "/"
                              << kExpectedMessages;
  }

  ASSERT_EQ(receive_count, kExpectedMessages);
}

TEST_F(TrellisFixture, PublisherMessageSizeIncreases) {
  unsigned receive_count{0};
  std::mutex count_mutex;
  std::condition_variable count_cv;
  constexpr unsigned kExpectedMessages = 10U;

  auto pub = GetNode().CreatePublisher<test::Test>("test_topic");
  auto sub = GetNode().CreateSubscriber<test::Test>(
      "test_topic",
      [&receive_count, &count_mutex, &count_cv](const time::TimePoint&, const time::TimePoint&,
                                                trellis::core::SubscriberImpl<test::Test>::MsgTypePtr msg) {
        std::lock_guard<std::mutex> lock(count_mutex);
        ASSERT_EQ(msg->id(), receive_count);
        ++receive_count;

        // Notify waiting thread when we've received all messages
        if (receive_count == kExpectedMessages) {
          count_cv.notify_one();
        }
      });

  StartRunnerThread();
  WaitForDiscovery();
  ASSERT_FALSE(GetNode().GetEventLoop().Stopped());

  // Sanity check initial value
  ASSERT_EQ(receive_count, 0U);

  for (unsigned i = 0; i < kExpectedMessages; ++i) {
    test::Test test_msg;
    test_msg.set_id(i);
    test_msg.set_msg(std::string(1000 * (i + 1), 'x'));
    pub->Send(test_msg);
  }

  // Wait for all messages to be received, with timeout
  {
    std::unique_lock<std::mutex> lock(count_mutex);
    bool received_all = count_cv.wait_for(lock, kProcessEventsWaitTime,
                                          [&receive_count]() { return receive_count == kExpectedMessages; });
    ASSERT_TRUE(received_all) << "Timeout waiting for all messages. Received: " << receive_count << "/"
                              << kExpectedMessages;
  }

  ASSERT_EQ(receive_count, kExpectedMessages);
}

TEST_F(TrellisFixture, SubscriberWatchdogTimeout) {
  unsigned receive_count{0};
  unsigned watchdog_count{0};

  auto pub = GetNode().CreatePublisher<test::Test>("test_watchdog_topic");
  auto sub = GetNode().CreateSubscriber<test::Test>(
      "test_watchdog_topic",
      [&receive_count](const time::TimePoint&, const time::TimePoint&,
                       trellis::core::SubscriberImpl<test::Test>::MsgTypePtr msg) {
        ASSERT_EQ(msg->id(), receive_count);
        ++receive_count;
      },
      kWatchdogTimeoutMs, [&watchdog_count](const trellis::core::time::TimePoint&) { ++watchdog_count; });

  StartRunnerThread();
  WaitForDiscovery();
  ASSERT_FALSE(GetNode().GetEventLoop().Stopped());

  // Sanity check initial values
  ASSERT_EQ(receive_count, 0U);
  ASSERT_EQ(watchdog_count, 0U);

  // Send 2 messages
  for (unsigned i = 0; i < 2U; ++i) {
    test::Test test_msg;
    test_msg.set_id(i);
    test_msg.set_msg("hello world");
    pub->Send(test_msg);
  }
  // Give the event loop some time before checking the result
  std::this_thread::sleep_for(kProcessEventsWaitTime);

  // Expect two messages received
  ASSERT_EQ(receive_count, 2U);
  ASSERT_EQ(watchdog_count, 0U);

  std::this_thread::sleep_for(std::chrono::milliseconds(kWatchdogTimeoutMs * 2));

  // Expect watchdog fired
  ASSERT_EQ(receive_count, 2U);
  ASSERT_EQ(watchdog_count, 1U);

  {
    test::Test test_msg;
    test_msg.set_id(2);
    test_msg.set_msg("hello world");
    pub->Send(test_msg);
  }
  // Give the event loop some time before checking the result
  std::this_thread::sleep_for(kProcessEventsWaitTime);

  // Expect third message received
  ASSERT_EQ(receive_count, 3U);
  ASSERT_EQ(watchdog_count, 1U);

  std::this_thread::sleep_for(std::chrono::milliseconds(kWatchdogTimeoutMs * 2));

  // Expect another watchdog fire
  ASSERT_EQ(receive_count, 3U);
  ASSERT_EQ(watchdog_count, 2U);
}

TEST_F(TrellisFixture, SubscriberThrottle) {
  unsigned receive_count{0};
  unsigned sent_count{0};

  auto pub = GetNode().CreatePublisher<test::Test>("test_throttle_topic");
  auto sub = GetNode().CreateSubscriber<test::Test>(
      "test_throttle_topic",
      [&receive_count](const time::TimePoint&, const time::TimePoint&,
                       trellis::core::SubscriberImpl<test::Test>::MsgTypePtr msg) {
        ASSERT_TRUE(msg->id() >= receive_count);
        ++receive_count;
      },
      {}, {}, 100.0);

  StartRunnerThread();
  WaitForDiscovery();
  ASSERT_FALSE(GetNode().GetEventLoop().Stopped());

  for (unsigned i = 0; i < 20U; ++i) {
    test::Test test_msg;
    test_msg.set_id(i);
    test_msg.set_msg("hello world");
    pub->Send(test_msg);
    ++sent_count;
  }

  // Give the event loop some time before checking the result
  std::this_thread::sleep_for(kProcessEventsWaitTime);
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

  auto pub = GetNode().CreatePublisher<test::Test>("test_send_timestamp_topic");

  test::Test test_msg;
  test_msg.set_msg("hello world");
  auto send_time = pub->Send(test_msg);

  ASSERT_EQ(send_time, time);
}

TEST_F(TrellisFixture, RawSubscriberBasicTest) {
  unsigned receive_count{0};

  auto pub = GetNode().CreatePublisher<test::Test>("test_raw_sub_topic");
  auto sub = GetNode().CreateRawSubscriber(
      "test_raw_sub_topic",
      [&receive_count](const time::TimePoint& now, const time::TimePoint& msgtime, const uint8_t* data, size_t len) {
        test::Test proto;
        if (proto.ParseFromArray(data, len)) {
          ASSERT_EQ(proto.id(), receive_count);
          ++receive_count;
        }
      });

  StartRunnerThread();
  WaitForDiscovery();
  ASSERT_FALSE(GetNode().GetEventLoop().Stopped());

  // Sanity check initial value
  ASSERT_EQ(receive_count, 0U);

  for (unsigned i = 0; i < 10U; ++i) {
    test::Test test_msg;
    test_msg.set_id(i);
    test_msg.set_msg("hello world");
    pub->Send(test_msg);
  }
  // Give the event loop some time before checking the result
  std::this_thread::sleep_for(kProcessEventsWaitTime);
  ASSERT_EQ(receive_count, 10U);
}

TEST_F(TrellisFixture, SubscriberRapidRecycle) {
  constexpr size_t kRecycleCount{10};
  constexpr size_t kMessagesPerCycle{10};
  unsigned receive_count{0};

  auto pub = GetNode().CreatePublisher<test::Test>("test_topic");
  StartRunnerThread();

  for (size_t i = 0; i < kRecycleCount; ++i) {
    auto sub = GetNode().CreateSubscriber<test::Test>(
        "test_topic", [&receive_count](const time::TimePoint&, const time::TimePoint&,
                                       trellis::core::SubscriberImpl<test::Test>::MsgTypePtr msg) {
          ASSERT_EQ(msg->id(), receive_count);
          ++receive_count;
        });
    WaitForDiscovery();
    for (unsigned j = 0; j < kMessagesPerCycle; ++j) {
      test::Test test_msg;
      test_msg.set_id(i * kMessagesPerCycle + j);
      test_msg.set_msg("hello world");
      pub->Send(test_msg);
    }
    std::this_thread::sleep_for(kProcessEventsWaitTime);
  }

  ASSERT_EQ(receive_count, kMessagesPerCycle * kRecycleCount);
}

TEST_F(TrellisFixture, PublisherRapidRecycle) {
  constexpr size_t kRecycleCount{10};
  constexpr size_t kMessagesPerCycle{10};
  unsigned receive_count{0};

  auto sub = GetNode().CreateSubscriber<test::Test>(
      "test_topic", [&receive_count](const time::TimePoint&, const time::TimePoint&,
                                     trellis::core::SubscriberImpl<test::Test>::MsgTypePtr msg) {
        ASSERT_EQ(msg->id(), receive_count);
        ++receive_count;
      });
  StartRunnerThread();

  for (size_t i = 0; i < kRecycleCount; ++i) {
    auto pub = GetNode().CreatePublisher<test::Test>("test_topic");
    WaitForDiscovery();
    for (unsigned j = 0; j < kMessagesPerCycle; ++j) {
      test::Test test_msg;
      test_msg.set_id(i * kMessagesPerCycle + j);
      test_msg.set_msg("hello world");
      pub->Send(test_msg);
    }
    std::this_thread::sleep_for(kProcessEventsWaitTime);
  }

  ASSERT_EQ(receive_count, kMessagesPerCycle * kRecycleCount);
}

TEST_F(TrellisFixture, ConvertingPubSub) {
  unsigned receive_count{0};
  std::mutex count_mutex;
  std::condition_variable count_cv;
  constexpr unsigned kExpectedMessages = 10U;

  auto pub = GetNode().CreatePublisher<test::Test, test::arbitrary::Test, std::function<decltype(arbitrary::ToProto)>>(
      "test_topic", arbitrary::ToProto);
  auto sub =
      GetNode().CreateSubscriber<test::Test, test::arbitrary::Test, std::function<decltype(arbitrary::FromProto)>>(
          "test_topic",
          [&receive_count, &count_mutex, &count_cv](const time::TimePoint&, const time::TimePoint&,
                                                    std::unique_ptr<test::arbitrary::Test> msg) {
            std::lock_guard<std::mutex> lock(count_mutex);
            ASSERT_EQ(msg->id, receive_count);
            ++receive_count;

            // Notify waiting thread when we've received all messages
            if (receive_count == kExpectedMessages) {
              count_cv.notify_one();
            }
          },
          {}, {}, {}, arbitrary::FromProto);

  StartRunnerThread();
  WaitForDiscovery();
  ASSERT_FALSE(GetNode().GetEventLoop().Stopped());

  // Sanity check initial value
  ASSERT_EQ(receive_count, 0U);

  for (unsigned i = 0; i < kExpectedMessages; ++i) {
    pub->Send({.id = i, .msg = std::string(226851, 'x')});
  }

  // Wait for all messages to be received, with timeout
  {
    std::unique_lock<std::mutex> lock(count_mutex);
    bool received_all = count_cv.wait_for(lock, kProcessEventsWaitTime,
                                          [&receive_count]() { return receive_count == kExpectedMessages; });
    ASSERT_TRUE(received_all) << "Timeout waiting for all messages. Received: " << receive_count << "/"
                              << kExpectedMessages;
  }

  ASSERT_EQ(receive_count, kExpectedMessages);
}

TEST_F(TrellisFixture, DynamicSubscriber) {
  unsigned receive_count{0};
  std::mutex count_mutex;
  std::condition_variable count_cv;
  constexpr unsigned kExpectedMessages = 10U;

  // Publisher with compile-time known message type
  auto pub = GetNode().CreatePublisher<test::Test>("test_dynamic_topic");

  // Dynamic subscriber that doesn't know the message type at compile time
  auto sub = GetNode().CreateDynamicSubscriber(
      "test_dynamic_topic", [&receive_count, &count_mutex, &count_cv](
                                const time::TimePoint&, const time::TimePoint&,
                                trellis::core::SubscriberImpl<google::protobuf::Message>::MsgTypePtr msg) {
        std::lock_guard<std::mutex> lock(count_mutex);

        // Use the protobuf reflection API to access fields dynamically
        ASSERT_NE(msg, nullptr);
        const google::protobuf::Descriptor* descriptor = msg->GetDescriptor();
        ASSERT_NE(descriptor, nullptr);

        // Verify this is the expected message type
        ASSERT_EQ(descriptor->full_name(), "trellis.core.test.Test");

        // Access the 'id' field using reflection
        const google::protobuf::Reflection* reflection = msg->GetReflection();
        const google::protobuf::FieldDescriptor* id_field = descriptor->FindFieldByName("id");
        ASSERT_NE(id_field, nullptr);

        unsigned id = reflection->GetUInt32(*msg, id_field);
        ASSERT_EQ(id, receive_count);
        ++receive_count;

        // Notify waiting thread when we've received all messages
        if (receive_count == kExpectedMessages) {
          count_cv.notify_one();
        }
      });

  StartRunnerThread();
  WaitForDiscovery();
  ASSERT_FALSE(GetNode().GetEventLoop().Stopped());

  // Sanity check initial value
  ASSERT_EQ(receive_count, 0U);

  for (unsigned i = 0; i < kExpectedMessages; ++i) {
    test::Test test_msg;
    test_msg.set_id(i);
    test_msg.set_msg("hello dynamic world");
    pub->Send(test_msg);
  }

  // Wait for all messages to be received, with timeout
  {
    std::unique_lock<std::mutex> lock(count_mutex);
    bool received_all = count_cv.wait_for(lock, kProcessEventsWaitTime,
                                          [&receive_count]() { return receive_count == kExpectedMessages; });
    ASSERT_TRUE(received_all) << "Timeout waiting for all messages. Received: " << receive_count << "/"
                              << kExpectedMessages;
  }

  ASSERT_EQ(receive_count, kExpectedMessages);
}

TEST_F(TrellisFixture, DynamicPublisher) {
  unsigned receive_count{0};
  std::mutex count_mutex;
  std::condition_variable count_cv;
  constexpr unsigned kExpectedMessages = 10U;

  // Create subscriber FIRST - the dynamic publisher will learn the message type from it
  auto sub = GetNode().CreateSubscriber<test::Test>(
      "test_dynamic_pub_topic",
      [&receive_count, &count_mutex, &count_cv](const time::TimePoint&, const time::TimePoint&,
                                                trellis::core::SubscriberImpl<test::Test>::MsgTypePtr msg) {
        std::lock_guard<std::mutex> lock(count_mutex);
        ASSERT_EQ(msg->id(), receive_count);
        ++receive_count;

        // Notify waiting thread when we've received all messages
        if (receive_count == kExpectedMessages) {
          count_cv.notify_one();
        }
      });

  // Create dynamic publisher - it will receive subscriber metadata via discovery callbacks
  auto pub = GetNode().CreateDynamicPublisher("test_dynamic_pub_topic");

  // Start event loop so discovery callbacks can fire
  StartRunnerThread();

  // Wait for discovery - this allows the dynamic publisher to receive the subscriber's
  // metadata through its ReceiveSubscriber callback
  WaitForDiscovery();
  ASSERT_FALSE(GetNode().GetEventLoop().Stopped());

  // The dynamic publisher/subscriber handshake requires two discovery cycles:
  // 1. Publisher receives subscriber info -> registers itself with discovery
  // 2. Subscriber receives publisher info -> creates shared memory reader
  // Wait for two more discovery cycles plus processing time to ensure everything is connected
  WaitForDiscovery();
  WaitForDiscovery();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Sanity check initial value
  ASSERT_EQ(receive_count, 0U);

  for (unsigned i = 0; i < kExpectedMessages; ++i) {
    // Create message with concrete type, then treat as generic protobuf message
    // This simulates the real-world scenario where you have a dynamically created
    // message that you want to publish without compile-time type knowledge
    test::Test test_msg;
    test_msg.set_id(i);
    test_msg.set_msg("hello from dynamic publisher");

    // Cast to google::protobuf::Message* to use the dynamic publisher API
    google::protobuf::Message* generic_msg = &test_msg;
    pub->Send(*generic_msg);
  }

  // Wait for all messages to be received, with timeout
  {
    std::unique_lock<std::mutex> lock(count_mutex);
    bool received_all = count_cv.wait_for(lock, kProcessEventsWaitTime,
                                          [&receive_count]() { return receive_count == kExpectedMessages; });
    ASSERT_TRUE(received_all) << "Timeout waiting for all messages. Received: " << receive_count << "/"
                              << kExpectedMessages;
  }

  ASSERT_EQ(receive_count, kExpectedMessages);
}
