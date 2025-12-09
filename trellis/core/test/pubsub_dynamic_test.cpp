/*
 * Copyright (C) 2025 Agtonomy
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

#include "trellis/core/discovery/utils.hpp"
#include "trellis/core/test/test.hpp"
#include "trellis/core/test/test.pb.h"
#include "trellis/core/test/test_fixture.hpp"

using namespace trellis::core;
using namespace trellis::core::test;

static constexpr std::chrono::milliseconds kProcessEventsWaitTime(500U);

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

TEST_F(TrellisFixture, DynamicPublisherWithSchema) {
  unsigned receive_count{0};
  std::mutex count_mutex;
  std::condition_variable count_cv;
  constexpr unsigned kExpectedMessages = 10U;

  // Create dynamic publisher with schema provided upfront - no need to wait for subscriber
  // Schema is derived from concrete message type (simulates having schema available at runtime)
  auto pub = GetNode().CreateDynamicPublisher(
      "test_dynamic_pub_with_schema_topic",
      DynamicPublisherSchema{.tdesc = discovery::utils::GetProtoMessageDescription(test::Test{}),
                             .tname = test::Test::descriptor()->full_name()});

  // Create subscriber after publisher - unlike DynamicPublisher test, order doesn't matter
  auto sub = GetNode().CreateSubscriber<test::Test>(
      "test_dynamic_pub_with_schema_topic",
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

  // Start event loop so discovery callbacks can fire
  StartRunnerThread();

  // Only need ONE discovery cycle since publisher already has its schema
  // (vs DynamicPublisher test which needs two cycles)
  WaitForDiscovery();
  ASSERT_FALSE(GetNode().GetEventLoop().Stopped());

  // Sanity check initial value
  ASSERT_EQ(receive_count, 0U);

  for (unsigned i = 0; i < kExpectedMessages; ++i) {
    // Create message with concrete type, then treat as generic protobuf message
    test::Test test_msg;
    test_msg.set_id(i);
    test_msg.set_msg("hello from dynamic publisher with schema");

    // Cast to google::protobuf::Message* to use the dynamic publisher API
    const google::protobuf::Message* generic_msg = &test_msg;
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
