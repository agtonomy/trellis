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

#include "trellis/core/message_consumer.hpp"

#include <gtest/gtest.h>

#include "trellis/core/test/test.hpp"
#include "trellis/core/test/test.pb.h"
#include "trellis/core/test/test_fixture.hpp"

using namespace trellis::core;
using namespace trellis::core::test;

TEST_F(TrellisFixture, MultipleMessageTypesWithIndividualCallbacks) {
  static unsigned receive_count_1{0};
  static unsigned receive_count_2{0};
  static constexpr unsigned num_burst_messages = 10U;

  auto pub = GetNode().CreatePublisher<test::Test>("consumer_topic_1");
  auto pub2 = GetNode().CreatePublisher<test::TestTwo>("consumer_topic_2");

  trellis::core::MessageConsumer<num_burst_messages, TypeTuple<test::Test>, TypeTuple<test::TestTwo>> inputs_{
      GetNode(),
      {{"consumer_topic_1", "consumer_topic_2"}},
      {[this](const std::string& topic, const test::Test& msg, const time::TimePoint&, const time::TimePoint&) {
         ASSERT_EQ(topic, "consumer_topic_1");
         ASSERT_EQ(receive_count_1, msg.id());
         ++receive_count_1;
       },
       [this](const std::string& topic, const test::TestTwo& msg, const time::TimePoint&, const time::TimePoint&) {
         ASSERT_EQ(topic, "consumer_topic_2");
         ASSERT_FLOAT_EQ(receive_count_2, msg.foo() / 2.0);
         ++receive_count_2;
       }}};
  StartRunnerThread();
  WaitForDiscovery();

  // Publish messages on both topics
  for (unsigned i = 0; i < num_burst_messages; ++i) {
    test::Test test_msg;
    test::TestTwo test_msg2;
    test_msg.set_id(i);
    test_msg.set_msg("hello world");
    test_msg2.set_foo(i * 2.0);
    pub->Send(test_msg);
    pub2->Send(test_msg2);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  WaitForSendReceive();

  ASSERT_EQ(receive_count_1, num_burst_messages);
  ASSERT_EQ(receive_count_2, num_burst_messages);
}

TEST_F(TrellisFixture, DuplicateMessageTypes) {
  static unsigned receive_count_1{0};
  static unsigned receive_count_2{0};
  static constexpr unsigned num_burst_messages = 10U;

  auto pub = GetNode().CreatePublisher<test::Test>("consumer_topic_1");
  auto pub2 = GetNode().CreatePublisher<test::Test>("consumer_topic_2");

  trellis::core::MessageConsumer<num_burst_messages, TypeTuple<test::Test>, TypeTuple<test::Test>> inputs_{
      GetNode(),
      {{"consumer_topic_1", "consumer_topic_2"}},
      {[this](const std::string& topic, const test::Test& msg, const time::TimePoint&, const time::TimePoint&) {
         ASSERT_EQ(topic, "consumer_topic_1");
         ASSERT_EQ(receive_count_1, msg.id());
         ++receive_count_1;
       },
       {[this](const std::string& topic, const test::Test& msg, const time::TimePoint&, const time::TimePoint&) {
         ASSERT_EQ(topic, "consumer_topic_2");
         ASSERT_EQ(receive_count_2, msg.id());
         ++receive_count_2;
       }}}};
  StartRunnerThread();
  WaitForDiscovery();

  // Publish messages on both topics
  for (unsigned i = 0; i < num_burst_messages; ++i) {
    test::Test test_msg;
    test_msg.set_id(i);
    test_msg.set_msg("hello world");
    pub->Send(test_msg);
    pub2->Send(test_msg);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  WaitForSendReceive();

  ASSERT_EQ(receive_count_1, num_burst_messages);
  ASSERT_EQ(receive_count_2, num_burst_messages);
}

TEST_F(TrellisFixture, MultipleMessageTypesWithIndividualCallbacksAndWatchdogs) {
  static unsigned receive_count_1{0};
  static unsigned receive_count_2{0};
  static unsigned watchdog_count_1{0};
  static unsigned watchdog_count_2{0};
  static constexpr unsigned watchdog1_timeout_ms = 1000U;
  static constexpr unsigned watchdog2_timeout_ms = 1000U;
  static constexpr unsigned num_burst_messages = 10U;

  StartRunnerThread();

  auto pub = GetNode().CreatePublisher<test::Test>("consumer_topic_3");
  auto pub2 = GetNode().CreatePublisher<test::TestTwo>("consumer_topic_4");

  trellis::core::MessageConsumer<num_burst_messages, TypeTuple<test::Test>, TypeTuple<test::TestTwo>> inputs_{
      GetNode(),
      {{"consumer_topic_3", "consumer_topic_4"}},
      {[this](const std::string& topic, const test::Test& msg, const time::TimePoint&, const time::TimePoint&) {
         ASSERT_EQ(topic, "consumer_topic_3");
         ASSERT_EQ(receive_count_1, msg.id());
         ++receive_count_1;
       },
       [this](const std::string& topic, const test::TestTwo& msg, const time::TimePoint&, const time::TimePoint&) {
         ASSERT_EQ(topic, "consumer_topic_4");
         ASSERT_FLOAT_EQ(receive_count_2, msg.foo() / 2.0);
         ++receive_count_2;
       }},
      {{watchdog1_timeout_ms, watchdog2_timeout_ms}},
      {{[](const std::string& topic, const trellis::core::time::TimePoint&) {
          ++watchdog_count_1;
          ASSERT_EQ(topic, "consumer_topic_3");
        },
        [](const std::string& topic, const trellis::core::time::TimePoint&) {
          ++watchdog_count_2;
          ASSERT_EQ(topic, "consumer_topic_4");
        }}}};

  WaitForDiscovery();

  // Publish messages on both topics
  for (unsigned i = 0; i < 5U; ++i) {
    test::Test test_msg;
    test::TestTwo test_msg2;
    test_msg.set_id(i);
    test_msg.set_msg("hello world");
    test_msg2.set_foo(i * 2.0);
    pub->Send(test_msg);
    pub2->Send(test_msg2);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Wait for watchdog
  std::this_thread::sleep_for(std::chrono::milliseconds(std::max(watchdog1_timeout_ms, watchdog2_timeout_ms) * 2));

  ASSERT_EQ(receive_count_1, 5U);
  ASSERT_EQ(receive_count_2, 5U);

  ASSERT_EQ(watchdog_count_1, 1U);
  ASSERT_EQ(watchdog_count_2, 1U);

  // Publish more messages on both topics
  for (unsigned i = 5; i < num_burst_messages; ++i) {
    test::Test test_msg;
    test::TestTwo test_msg2;
    test_msg.set_id(i);
    test_msg.set_msg("hello world");
    test_msg2.set_foo(i * 2.0);
    pub->Send(test_msg);
    pub2->Send(test_msg2);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Wait for watchdog
  std::this_thread::sleep_for(std::chrono::milliseconds(std::max(watchdog1_timeout_ms, watchdog2_timeout_ms) * 2));

  ASSERT_EQ(receive_count_1, num_burst_messages);
  ASSERT_EQ(receive_count_2, num_burst_messages);

  ASSERT_EQ(watchdog_count_1, 2U);
  ASSERT_EQ(watchdog_count_2, 2U);
}

TEST_F(TrellisFixture, RoundTripConversionWithIndividualCallbacks) {
  static unsigned receive_count_1{0};
  static unsigned receive_count_2{0};
  static constexpr unsigned num_burst_messages = 10U;

  auto pub =
      GetNode().CreatePublisher<test::Test, test::arbitrary::Test, std::function<decltype(test::arbitrary::ToProto)>>(
          "consumer_topic_1", test::arbitrary::ToProto);
  auto pub2 = GetNode().CreatePublisher<test::TestTwo>("consumer_topic_2");

  trellis::core::MessageConsumer<
      num_burst_messages,
      TypeTuple<test::Test, test::arbitrary::Test, std::function<decltype(test::arbitrary::FromProto)>>,
      TypeTuple<test::TestTwo>>
      inputs_{
          GetNode(),
          {{"consumer_topic_1", "consumer_topic_2"}},
          {[this](const std::string& topic, const test::arbitrary::Test& msg, const time::TimePoint&,
                  const time::TimePoint&) {
             ASSERT_EQ(topic, "consumer_topic_1");
             ASSERT_EQ(receive_count_1, msg.id);
             ++receive_count_1;
           },
           [this](const std::string& topic, const test::TestTwo& msg, const time::TimePoint&, const time::TimePoint&) {
             ASSERT_EQ(topic, "consumer_topic_2");
             ASSERT_FLOAT_EQ(receive_count_2, msg.foo() / 2.0);
             ++receive_count_2;
           }},
          {test::arbitrary::FromProto, std::identity()},
      };
  StartRunnerThread();
  WaitForDiscovery();

  // Publish messages on both topics
  for (unsigned i = 0; i < num_burst_messages; ++i) {
    pub->Send({.id = i, .msg = "designated initialization rules!"});
    test::TestTwo test_msg2;
    test_msg2.set_foo(i * 2.0);
    pub2->Send(test_msg2);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  WaitForSendReceive();

  ASSERT_EQ(receive_count_1, num_burst_messages);
  ASSERT_EQ(receive_count_2, num_burst_messages);
}

TEST_F(TrellisFixture, ConvertingDuplicateMessageTypes) {
  static unsigned receive_count_1{0};
  static unsigned receive_count_2{0};
  static constexpr unsigned num_burst_messages = 10U;

  using ToT = std::function<decltype(test::arbitrary::ToProto)>;
  using FromT = std::function<decltype(test::arbitrary::FromProto)>;
  auto pub =
      GetNode().CreatePublisher<test::Test, test::arbitrary::Test, ToT>("consumer_topic_1", test::arbitrary::ToProto);
  auto pub2 =
      GetNode().CreatePublisher<test::Test, test::arbitrary::Test, ToT>("consumer_topic_2", test::arbitrary::ToProto);

  trellis::core::MessageConsumer<num_burst_messages, TypeTuple<test::Test, test::arbitrary::Test, FromT>,
                                 TypeTuple<test::Test, test::arbitrary::Test, FromT>>
      inputs_{GetNode(),
              {{"consumer_topic_1", "consumer_topic_2"}},
              {[this](const std::string& topic, const test::arbitrary::Test& msg, const time::TimePoint&,
                      const time::TimePoint&) {
                 ASSERT_EQ(topic, "consumer_topic_1");
                 ASSERT_EQ(receive_count_1, msg.id);
                 ++receive_count_1;
               },
               {[this](const std::string& topic, const test::arbitrary::Test& msg, const time::TimePoint&,
                       const time::TimePoint&) {
                 ASSERT_EQ(topic, "consumer_topic_2");
                 ASSERT_EQ(receive_count_2, msg.id);
                 ++receive_count_2;
               }}},
              {test::arbitrary::FromProto, test::arbitrary::FromProto}};
  StartRunnerThread();
  WaitForDiscovery();

  // Publish messages on both topics
  for (unsigned i = 0; i < num_burst_messages; ++i) {
    const auto test_msg = test::arbitrary::Test{.id = i, .msg = "wow this is nice"};
    pub->Send(test_msg);
    pub2->Send(test_msg);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  WaitForSendReceive();

  ASSERT_EQ(receive_count_1, num_burst_messages);
  ASSERT_EQ(receive_count_2, num_burst_messages);
}
