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

#include <fmt/core.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "trellis/core/discovery/descriptor_store.hpp"
#include "trellis/core/discovery/utils.hpp"
#include "trellis/core/test/test.hpp"
#include "trellis/core/test/test.pb.h"
#include "trellis/core/test/test_fixture.hpp"

using namespace trellis::core;
using namespace trellis::core::test;

static constexpr std::chrono::milliseconds kProcessEventsWaitTime(500U);
static constexpr unsigned kWatchdogTimeoutMs{1000u};
// An upper bound rather than an expected duration: whoever waits on it polls, so it only has to outlast a heavily
// loaded CI machine.
static constexpr std::chrono::milliseconds kDiscoveryConvergeTimeout(5000U);

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

// A subscriber created off the loop thread receives discovery's replay of existing publishers immediately. The
// replay must not reach it before a shared_ptr owns it; otherwise shared_from_this() throws and Node::Run() exits.
TEST_F(TrellisFixture, SubscriberCreatedWhileLoopRunsKeepsLoopAlive) {
  constexpr size_t kSubscriberCount{200};

  auto pub = GetNode().CreatePublisher<test::Test>("test_topic");
  StartRunnerThread();
  WaitForDiscovery();

  std::vector<Subscriber<test::Test>> subs;
  for (size_t i = 0; i < kSubscriberCount; ++i) {
    subs.push_back(GetNode().CreateSubscriber<test::Test>(
        "test_topic", [](const time::TimePoint&, const time::TimePoint&, SubscriberImpl<test::Test>::MsgTypePtr) {}));
  }

  // Handlers run in order. When this one runs, every earlier delivery has been handled.
  std::promise<void> marker_ran;
  asio::post(*GetNode().GetEventLoop(), [&marker_ran]() { marker_ran.set_value(); });
  EXPECT_EQ(marker_ran.get_future().wait_for(std::chrono::seconds{5}), std::future_status::ready)
      << "the event loop stopped while subscribers were being created";
  StopAndJoinRunnerThread();  // The marker references a local.
}

// The same, but for destruction. Each subscriber is destroyed on this thread straight after it is created, while the
// loop may still be delivering discovery to it or running its statistics timer. A strong reference taken in
// ReceivePublisher() threw bad_weak_ptr once the count reached zero, which ended Node::Run(). Destroying a timer here
// while the loop fired it was a use-after-free, which the ASAN build reports.
TEST_F(TrellisFixture, SubscriberDestroyedWhileLoopRunsKeepsLoopAlive) {
  constexpr size_t kSubscriberCount{200};

  auto pub = GetNode().CreatePublisher<test::Test>("test_topic");
  StartRunnerThread();
  WaitForDiscovery();

  for (size_t i = 0; i < kSubscriberCount; ++i) {
    auto sub = GetNode().CreateSubscriber<test::Test>(
        "test_topic", [](const time::TimePoint&, const time::TimePoint&, SubscriberImpl<test::Test>::MsgTypePtr) {});
    sub.reset();
  }

  // Handlers run in order. When this one runs, every earlier delivery has been handled.
  std::promise<void> marker_ran;
  asio::post(*GetNode().GetEventLoop(), [&marker_ran]() { marker_ran.set_value(); });
  EXPECT_EQ(marker_ran.get_future().wait_for(std::chrono::seconds{5}), std::future_status::ready)
      << "the event loop stopped while subscribers were being destroyed";
  StopAndJoinRunnerThread();  // The marker references a local.
}

// The in-process bus posts each delivery to the subscriber's loop at send time, so deliveries may already be queued
// when Stop() is called. None of them may reach the callback, or re-arm the watchdog that Stop() disarmed.
TEST_F(TrellisFixture, SubscriberStopDropsQueuedDeliveries) {
  constexpr unsigned kWatchdogTimeoutMs{50};

  unsigned receive_count{0};
  unsigned watchdog_count{0};
  auto pub = GetNode().CreatePublisher<test::Test>("test_topic");
  auto sub = GetNode().CreateSubscriber<test::Test>(
      "test_topic",
      [&receive_count](const time::TimePoint&, const time::TimePoint&, SubscriberImpl<test::Test>::MsgTypePtr) {
        ++receive_count;
      },
      kWatchdogTimeoutMs, [&watchdog_count](const time::TimePoint&) { ++watchdog_count; });
  GetNode().RunUntilIdle();
  ASSERT_TRUE(sub->IsInProcess());

  test::Test msg;
  msg.set_id(1);
  pub->Send(msg);
  GetNode().RunUntilIdle();
  ASSERT_EQ(receive_count, 1u);

  pub->Send(msg);  // Queued on the loop, not delivered yet.
  sub->Stop();
  GetNode().RunUntilIdle();
  EXPECT_EQ(receive_count, 1u) << "a delivery queued before Stop() reached the callback";

  GetNode().RunFor(std::chrono::milliseconds{kWatchdogTimeoutMs * 4});
  EXPECT_EQ(watchdog_count, 0u) << "the watchdog fired after Stop()";
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

  // No explicit ConverterT/converter passed. CreatePublisher finds ToProto via ADL on arbitrary::Test, and
  // CreateSubscriber finds FromProto via ADL on test::Test.
  auto pub = GetNode().CreatePublisher<test::Test, test::arbitrary::Test>("test_topic");
  auto sub = GetNode().CreateSubscriber<test::Test, test::arbitrary::Test>(
      "test_topic", [&receive_count, &count_mutex, &count_cv](const time::TimePoint&, const time::TimePoint&,
                                                              std::unique_ptr<test::arbitrary::Test> msg) {
        std::lock_guard<std::mutex> lock(count_mutex);
        ASSERT_EQ(msg->id, receive_count);
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

TEST_F(TrellisFixture, EarlyStopSubscriber) {
  unsigned receive_count{0};
  std::mutex count_mutex;
  std::condition_variable count_cv;

  constexpr unsigned kPublishCount = 20U;
  constexpr unsigned kExpectedReceivedCount = 7U;

  auto pub = GetNode().CreatePublisher<test::Test>("early_stop_test_topic");
  auto sub = GetNode().CreateSubscriber<test::Test>(
      "early_stop_test_topic",
      [&receive_count, &count_mutex, &count_cv](const time::TimePoint&, const time::TimePoint&,
                                                trellis::core::SubscriberImpl<test::Test>::MsgTypePtr msg) {
        std::lock_guard<std::mutex> lock(count_mutex);
        ASSERT_EQ(msg->id(), receive_count);
        ++receive_count;
        std::cout << receive_count << '\t';

        if (receive_count == kPublishCount) {
          ASSERT_TRUE(false) << "The subscriber received all published messages which means it was not stopped early";
          count_cv.notify_one();
        }
      });

  StartRunnerThread();
  WaitForDiscovery();
  ASSERT_FALSE(GetNode().GetEventLoop().Stopped());

  // Sanity check initial value
  ASSERT_EQ(receive_count, 0U);

  for (unsigned i = 0; i < kPublishCount; ++i) {
    test::Test test_msg;
    test_msg.set_id(i);
    test_msg.set_msg("hello world");
    pub->Send(test_msg);
    if (i + 1 == kExpectedReceivedCount) {  // +1 because of 0 index
      sleep(1);
      sub->Stop();
    }
  }

  auto spurious_wakeup_predicate = [&receive_count]() { return receive_count == kExpectedReceivedCount; };
  // Wait for all messages to be received, with timeout
  {
    std::unique_lock<std::mutex> lock(count_mutex);
    bool received_all = count_cv.wait_for(lock, kProcessEventsWaitTime, spurious_wakeup_predicate);
    ASSERT_TRUE(received_all);  // Should time out because some messages from the publisher arrive after the sub stops
  }

  ASSERT_EQ(receive_count, kExpectedReceivedCount);
}

TEST(PublisherBufferSize, SendThrowsWhenMessageExceedsMaxBufferSize) {
  constexpr size_t kInitialBufferSize = 1024;
  constexpr size_t kMaxBufferSize = 4096;
  trellis::core::Node node("SendThrowsWhenMessageExceedsMaxBufferSize",
                           trellis::core::Config(YAML::Load(fmt::format(R"(
    trellis:
      publisher:
        attributes:
          initial_buffer_size: {}
          max_buffer_size: {}
      discovery:
        interval: 100
        sample_timeout: 200
        loopback_enabled: true
    )",
                                                                        kInitialBufferSize, kMaxBufferSize))));

  auto pub = node.CreatePublisher<test::Test>("test_max_buffer_topic");

  test::Test small_msg;
  small_msg.set_id(0);
  small_msg.set_msg("fits");
  EXPECT_NO_THROW(pub->Send(small_msg));

  test::Test big_msg;
  big_msg.set_id(1);
  big_msg.set_msg(std::string(kMaxBufferSize * 2, 'x'));
  EXPECT_THROW(pub->Send(big_msg), std::runtime_error);

  // The rejected message must not have disturbed the publisher: a message that fits still sends afterwards
  EXPECT_NO_THROW(pub->Send(small_msg));
}

TEST(PublisherBufferSize, CreateThrowsWhenInitialBufferSizeExceedsMax) {
  trellis::core::Node node("CreateThrowsWhenInitialBufferSizeExceedsMax", trellis::core::Config(YAML::Load(R"(
    trellis:
      publisher:
        attributes:
          initial_buffer_size: 4096
          max_buffer_size: 1024
      discovery:
        interval: 100
        sample_timeout: 200
        loopback_enabled: true
    )")));

  EXPECT_THROW(node.CreatePublisher<test::Test>("test_initial_exceeds_max_topic"), std::invalid_argument);
}

TEST_F(TrellisFixture, SendBytesGrowsBuffer) {
  // Comfortably past the 10 KiB default initial_buffer_size so the slot has to grow
  const std::string payload(64 * 1024, 'z');
  size_t received_len{0};

  auto pub = GetNode().CreatePublisher<test::Test>("test_sb_topic");
  auto sub = GetNode().CreateRawSubscriber(
      "test_sb_topic", [&received_len](const time::TimePoint&, const time::TimePoint&, const uint8_t*, size_t len) {
        received_len = len;
      });

  StartRunnerThread();
  WaitForDiscovery();
  ASSERT_FALSE(GetNode().GetEventLoop().Stopped());

  pub->SendBytes(payload.data(), payload.size());

  std::this_thread::sleep_for(kProcessEventsWaitTime);
  ASSERT_EQ(received_len, payload.size());
}

TEST_F(TrellisFixture, SendAlternatesLargeAndSmallMessages) {
  // The buffer only ever grows, so a small message following a large one must still round trip at its own length
  const std::vector<size_t> payload_sizes{16, 64 * 1024, 16, 128 * 1024, 32};
  std::vector<size_t> received_sizes;
  std::mutex received_mutex;
  std::condition_variable received_cv;

  auto pub = GetNode().CreatePublisher<test::Test>("test_alternating_topic");
  auto sub = GetNode().CreateSubscriber<test::Test>(
      "test_alternating_topic",
      [&](const time::TimePoint&, const time::TimePoint&, trellis::core::SubscriberImpl<test::Test>::MsgTypePtr msg) {
        std::lock_guard<std::mutex> lock(received_mutex);
        received_sizes.push_back(msg->msg().size());
        if (received_sizes.size() == payload_sizes.size()) {
          received_cv.notify_one();
        }
      });

  StartRunnerThread();
  WaitForDiscovery();
  ASSERT_FALSE(GetNode().GetEventLoop().Stopped());

  for (size_t size : payload_sizes) {
    test::Test test_msg;
    test_msg.set_id(0);
    test_msg.set_msg(std::string(size, 'x'));
    pub->Send(test_msg);
  }

  {
    std::unique_lock<std::mutex> lock(received_mutex);
    const bool received_all = received_cv.wait_for(lock, kProcessEventsWaitTime, [&received_sizes, &payload_sizes]() {
      return received_sizes.size() == payload_sizes.size();
    });
    ASSERT_TRUE(received_all) << "Received " << received_sizes.size() << "/" << payload_sizes.size();
  }

  EXPECT_EQ(received_sizes, payload_sizes);
}

// Publisher and subscriber share a process in these tests, so the subscriber selects the in-process bus from the
// publisher's advertisement. That choice is independent of how discovery is configured -- see
// InProcessTransportIsSelectedWithoutLoopback.

TEST_F(TrellisFixture, InProcessBusCarriesSendTimeAndPayload) {
  std::string received_msg;
  trellis::core::time::TimePoint received_send_time{};
  std::mutex m;
  std::condition_variable cv;
  bool got{false};

  const Publisher<test::Test> pub = GetNode().CreatePublisher<test::Test>("test_inproc_bus_topic");
  const Subscriber<test::Test> sub = GetNode().CreateSubscriber<test::Test>(
      "test_inproc_bus_topic", [&](const time::TimePoint&, const time::TimePoint& send_time,
                                   trellis::core::SubscriberImpl<test::Test>::MsgTypePtr msg) {
        std::lock_guard<std::mutex> lock(m);
        received_msg = msg->msg();
        received_send_time = send_time;
        got = true;
        cv.notify_one();
      });

  StartRunnerThread();
  WaitForDiscovery();
  ASSERT_FALSE(GetNode().GetEventLoop().Stopped());

  test::Test msg;
  msg.set_msg("through the bus");
  const time::TimePoint sent = pub->Send(msg);

  std::unique_lock<std::mutex> lock(m);
  ASSERT_TRUE(cv.wait_for(lock, kProcessEventsWaitTime, [&got]() { return got; }));
  EXPECT_EQ(received_msg, "through the bus");
  // The synthesized header must carry the publisher's send time so a follower steps its sim clock identically.
  EXPECT_EQ(received_send_time, sent);
}

TEST_F(TrellisFixture, InProcessBusDeliversFromMultiplePublishersOnOneTopic) {
  std::vector<std::string> received;
  std::mutex m;
  std::condition_variable cv;

  const Subscriber<test::Test> sub = GetNode().CreateSubscriber<test::Test>(
      "test_inproc_multi_topic",
      [&](const time::TimePoint&, const time::TimePoint&, trellis::core::SubscriberImpl<test::Test>::MsgTypePtr msg) {
        std::lock_guard<std::mutex> lock(m);
        received.push_back(msg->msg());
        cv.notify_one();
      });
  const Publisher<test::Test> pub_a = GetNode().CreatePublisher<test::Test>("test_inproc_multi_topic");
  const Publisher<test::Test> pub_b = GetNode().CreatePublisher<test::Test>("test_inproc_multi_topic");

  StartRunnerThread();
  WaitForDiscovery();
  ASSERT_FALSE(GetNode().GetEventLoop().Stopped());

  test::Test a;
  a.set_msg("from A");
  test::Test b;
  b.set_msg("from B");
  pub_a->Send(a);
  pub_b->Send(b);

  std::unique_lock<std::mutex> lock(m);
  // Each publisher has a distinct writer id, so both messages are tracked independently and neither is dropped as a
  // stale sequence on the single per-topic route.
  ASSERT_TRUE(cv.wait_for(lock, kProcessEventsWaitTime, [&received]() { return received.size() == 2; }));
  EXPECT_NE(std::find(received.begin(), received.end(), "from A"), received.end());
  EXPECT_NE(std::find(received.begin(), received.end(), "from B"), received.end());
}

TEST_F(TrellisFixture, InProcessBusFansOutFromMultiplePublishersToMultipleSubscribers) {
  std::vector<std::string> received_a;
  std::vector<std::string> received_b;
  std::mutex m;
  std::condition_variable cv;

  const auto make_sub = [&](std::vector<std::string>& sink) {
    return GetNode().CreateSubscriber<test::Test>(
        "test_inproc_many_to_many_topic",
        [&](const time::TimePoint&, const time::TimePoint&, trellis::core::SubscriberImpl<test::Test>::MsgTypePtr msg) {
          std::lock_guard<std::mutex> lock(m);
          sink.push_back(msg->msg());
          cv.notify_one();
        });
  };
  const Subscriber<test::Test> sub_a = make_sub(received_a);
  const Subscriber<test::Test> sub_b = make_sub(received_b);
  const Publisher<test::Test> pub_a = GetNode().CreatePublisher<test::Test>("test_inproc_many_to_many_topic");
  const Publisher<test::Test> pub_b = GetNode().CreatePublisher<test::Test>("test_inproc_many_to_many_topic");

  StartRunnerThread();
  WaitForDiscovery();
  ASSERT_TRUE(sub_a->IsInProcess());
  ASSERT_TRUE(sub_b->IsInProcess());

  test::Test a;
  a.set_msg("from A");
  test::Test b;
  b.set_msg("from B");
  pub_a->Send(a);
  pub_b->Send(b);

  // Routes are keyed by topic rather than by publisher, so each subscriber holds one route and still sees both
  // publishers. Distinct per-publisher writer ids keep the two sequence streams independent, so neither message is
  // charged as a drop against the other.
  std::unique_lock<std::mutex> lock(m);
  ASSERT_TRUE(cv.wait_for(lock, kProcessEventsWaitTime,
                          [&received_a, &received_b]() { return received_a.size() == 2 && received_b.size() == 2; }));
  for (const std::vector<std::string>* sink : {&received_a, &received_b}) {
    EXPECT_NE(std::find(sink->begin(), sink->end(), "from A"), sink->end());
    EXPECT_NE(std::find(sink->begin(), sink->end(), "from B"), sink->end());
  }
}

TEST_F(TrellisFixture, InProcessTransportIsSelectedForSameProcessPublisher) {
  const Publisher<test::Test> pub = GetNode().CreatePublisher<test::Test>("test_inproc_selection_topic");
  const Subscriber<test::Test> sub = GetNode().CreateSubscriber<test::Test>(
      "test_inproc_selection_topic",
      [](const time::TimePoint&, const time::TimePoint&, trellis::core::SubscriberImpl<test::Test>::MsgTypePtr) {});

  StartRunnerThread();
  WaitForDiscovery();

  EXPECT_TRUE(sub->IsInProcess());
}

namespace {

/// @brief Runs a node on its own thread, stopping and joining it on every exit path.
///
/// A failed ASSERT_* returns from the test body immediately. Without this, that early return would destroy a
/// still-joinable std::thread, and std::thread's destructor calls std::terminate() — taking down every remaining test
/// in the binary rather than failing just the one.
class ScopedNodeRunner {
 public:
  explicit ScopedNodeRunner(trellis::core::Node& node) : node_{node}, thread_{[&node]() { node.Run(); }} {}

  ~ScopedNodeRunner() {
    node_.Stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  ScopedNodeRunner(const ScopedNodeRunner&) = delete;
  ScopedNodeRunner& operator=(const ScopedNodeRunner&) = delete;

 private:
  trellis::core::Node& node_;
  std::thread thread_;
};

/// @brief A discovery config on a port drawn from the IANA dynamic range, which nothing well-known claims.
///
/// Discovery sockets set SO_REUSEPORT and broadcast, so on a fixed port two concurrent runs of this binary would see
/// each other's samples. The peer in the other process advertises a different pid, so the subscriber below would build
/// an ShmReader alongside its bus route and could receive the message twice.
std::string RandomPortDiscoveryConfig() {
  std::random_device rd;
  const uint16_t port = std::uniform_int_distribution<uint16_t>{49152U, 65535U}(rd);
  return fmt::format(R"(
    trellis:
      discovery:
        interval: 10
        sample_timeout: 500
        port: {}
    )",
                     port);
}

}  // namespace

// Discovery is left on its default UDP path here, so the only thing steering the subscriber onto the bus is the
// publisher's advertised transport layer plus a matching pid. This is what keeps the transport decision independent
// of trellis.discovery.loopback_enabled.
TEST(InProcessTransport, InProcessTransportIsSelectedWithoutLoopback) {
  trellis::core::Node node("InProcessTransportIsSelectedWithoutLoopback",
                           trellis::core::Config(YAML::Load(RandomPortDiscoveryConfig())));

  std::string received_msg;
  std::mutex m;
  std::condition_variable cv;
  bool got{false};

  const Publisher<test::Test> pub = node.CreatePublisher<test::Test>("test_no_loopback_topic");
  const Subscriber<test::Test> sub = node.CreateSubscriber<test::Test>(
      "test_no_loopback_topic",
      [&](const time::TimePoint&, const time::TimePoint&, trellis::core::SubscriberImpl<test::Test>::MsgTypePtr msg) {
        std::lock_guard<std::mutex> lock(m);
        received_msg = msg->msg();
        got = true;
        cv.notify_one();
      });

  // Declared after the publisher and subscriber so it is destroyed before them, stopping the loop while they are still
  // alive to serve any handler already in flight.
  const ScopedNodeRunner runner{node};

  // Polled rather than slept, so the test proceeds the moment discovery converges.
  const auto deadline = std::chrono::steady_clock::now() + kDiscoveryConvergeTimeout;
  while (!sub->IsInProcess() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ASSERT_TRUE(sub->IsInProcess());

  test::Test msg;
  msg.set_msg("no loopback");
  pub->Send(msg);

  {
    std::unique_lock<std::mutex> lock(m);
    EXPECT_TRUE(cv.wait_for(lock, kProcessEventsWaitTime, [&got]() { return got; }));
  }
  EXPECT_EQ(received_msg, "no loopback");
}

// A publisher can hold a subscriber in this process and one outside it at the same time. That is the only path where
// a single send both publishes to the bus and fills a shared memory slot, from the same serialized bytes, so it is
// worth covering on its own rather than inferring it from the two single-transport cases.
TEST(InProcessTransport, SendReachesAnInProcessSubscriberWhileARemoteOneIsRegistered) {
  // Short by necessity: the node name goes into a Unix socket path that AddReader builds, capped at 108 bytes.
  trellis::core::Node node("mixed_transport", trellis::core::Config(YAML::Load(RandomPortDiscoveryConfig())));

  std::string received_msg;
  std::mutex m;
  std::condition_variable cv;
  unsigned receive_count{0};

  const Publisher<test::Test> pub = node.CreatePublisher<test::Test>("test_mixed_transport_topic");
  const Subscriber<test::Test> sub = node.CreateSubscriber<test::Test>(
      "test_mixed_transport_topic",
      [&](const time::TimePoint&, const time::TimePoint&, trellis::core::SubscriberImpl<test::Test>::MsgTypePtr msg) {
        std::lock_guard<std::mutex> lock(m);
        received_msg = msg->msg();
        ++receive_count;
        cv.notify_one();
      });

  // Declared after the publisher and subscriber so it is destroyed before them, stopping the loop while they are
  // still alive to serve any handler already in flight.
  const ScopedNodeRunner runner{node};

  // A subscriber sample reporting a pid that is not ours, which is what makes the publisher treat it as remote and
  // keep a shared memory reader alongside its bus route. Nothing ever reads that slot, and nothing needs to:
  // ShmWriter::AddReader only opens a notification socket, and the writer never waits on one.
  trellis::core::discovery::DescriptorStore store{""};
  trellis::core::discovery::Sample remote_subscriber = trellis::core::discovery::utils::CreateProtoPubSubSample(
      store, "test_mixed_transport_topic", /* message_desc = */ "", "trellis.core.test.Test",
      /* publisher = */ false, /* memory_file_prefix = */ "", /* buffer_count = */ 0u);
  ASSERT_EQ(remote_subscriber.topic().pid(), ::getpid());
  remote_subscriber.mutable_topic()->set_pid(::getpid() + 1);
  node.GetDiscovery()->Register(std::move(remote_subscriber));

  // Polled rather than slept, so the test proceeds the moment both transports are in play.
  const auto deadline = std::chrono::steady_clock::now() + kDiscoveryConvergeTimeout;
  while ((!sub->IsInProcess() || !pub->HasRemoteSubscribers()) && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ASSERT_TRUE(sub->IsInProcess()) << "the same-process subscriber did not take the bus";
  ASSERT_TRUE(pub->HasRemoteSubscribers()) << "the publisher never saw the subscriber from another process";

  test::Test msg;
  msg.set_msg("mixed transport");
  pub->Send(msg);

  {
    std::unique_lock<std::mutex> lock(m);
    ASSERT_TRUE(cv.wait_for(lock, kProcessEventsWaitTime, [&receive_count]() { return receive_count > 0; }));
    // Exactly once, not merely at least once: a second copy would mean this subscriber was served by both
    // transports, which is what registering a shared memory reader for it would cause. Wait for one and require
    // the wait to time out.
    EXPECT_FALSE(cv.wait_for(lock, kProcessEventsWaitTime, [&receive_count]() { return receive_count > 1; }))
        << "delivered " << receive_count << " times";
  }
  EXPECT_EQ(received_msg, "mixed transport");
}
