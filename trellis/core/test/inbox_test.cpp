/*
 * Copyright (C) 2023 Agtonomy
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

#include "trellis/core/inbox.hpp"

#include <gmock/gmock.h>

#include "trellis/core/test/test.pb.h"
#include "trellis/core/test/test_fixture.hpp"

namespace trellis::core::test {

namespace {

using std::chrono_literals::operator""ms;
using testing::AllOf;
using testing::Eq;
using testing::ExplainMatchResult;
using testing::Field;
using testing::FieldsAre;
using testing::FloatEq;
using testing::Matcher;
using testing::Optional;
using testing::Property;
using time::TimePoint;

constexpr auto kT0 = TimePoint{};

auto MakeTest(std::string msg = {}) {
  auto ret = test::Test{};
  ret.set_msg(std::move(msg));
  return ret;
}

auto MakeTestTwo(std::string bar = {}) {
  auto ret = TestTwo{};
  ret.set_bar(std::move(bar));
  return ret;
}

// Message field is a reference, so we cannot use testing::Field for it.
MATCHER_P(MessageIs, m, "") { return ExplainMatchResult(m, arg.message, result_listener); }

template <typename MSG_T>
auto StampedMessageIs(const TimePoint& time, const Matcher<MSG_T>& message_matcher) {
  return Optional(AllOf(Field("timestamp", &StampedMessage<MSG_T>::timestamp, Eq(time)), MessageIs(message_matcher)));
}

Matcher<test::Test> TestIs(const std::string msg) { return Property("msg", &test::Test::msg, Eq(msg)); }

Matcher<TestTwo> TestTwoIs(const std::string bar) { return Property("bar", &TestTwo::bar, Eq(bar)); }

}  // namespace

TEST_F(TrellisFixture, InboxNoMessages) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic_1");
  auto pub2 = node_.CreatePublisher<TestTwo>("topic_2");

  const auto inbox = Inbox<test::Test, TestTwo>{node_, {"topic_1", "topic_2"}, {100ms, 100ms}};

  WaitForDiscovery();

  WaitForSendReceive();

  ASSERT_THAT(inbox.GetLatestMessages(kT0), FieldsAre(Eq(std::nullopt), Eq(std::nullopt))) << "No messages sent!";
}

TEST_F(TrellisFixture, InboxMessagesReceived) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic_1");
  auto pub2 = node_.CreatePublisher<TestTwo>("topic_2");

  const auto inbox = Inbox<test::Test, TestTwo>{node_, {"topic_1", "topic_2"}, {100ms, 100ms}};

  WaitForDiscovery();

  pub->Send(MakeTest("hello"), kT0);
  pub2->Send(MakeTestTwo("there"), kT0);

  WaitForSendReceive();

  ASSERT_THAT(inbox.GetLatestMessages(kT0),
              FieldsAre(StampedMessageIs(kT0, TestIs("hello")), StampedMessageIs(kT0, TestTwoIs("there"))))
      << "Messages sent at receive time, so are still valid.";
}

TEST_F(TrellisFixture, InboxMessageTimeout) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic_1");
  auto pub2 = node_.CreatePublisher<TestTwo>("topic_2");

  const auto inbox = Inbox<test::Test, TestTwo>{node_, {"topic_1", "topic_2"}, {100ms, 100ms}};

  WaitForDiscovery();

  pub->Send(MakeTest("hello"), kT0);
  pub2->Send(MakeTestTwo("there"), kT0 + 1ms);

  WaitForSendReceive();

  ASSERT_THAT(inbox.GetLatestMessages(kT0 + 101ms),
              FieldsAre(Eq(std::nullopt), StampedMessageIs(kT0 + 1ms, TestTwoIs("there"))))
      << "First message has timed out by 1ms.";
}

TEST_F(TrellisFixture, InboxMissedMessages) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic_1");
  auto pub2 = node_.CreatePublisher<TestTwo>("topic_2");

  const auto inbox = Inbox<test::Test, TestTwo>{node_, {"topic_1", "topic_2"}, {100ms, 100ms}};

  WaitForDiscovery();

  pub->Send(MakeTest("hello"), kT0);
  pub2->Send(MakeTestTwo("there"), kT0);

  pub->Send(MakeTest("good"), kT0 + 1ms);
  pub2->Send(MakeTestTwo("bye"), kT0 + 1ms);

  WaitForSendReceive();

  ASSERT_THAT(inbox.GetLatestMessages(kT0 + 2ms),
              FieldsAre(StampedMessageIs(kT0 + 1ms, TestIs("good")), StampedMessageIs(kT0 + 1ms, TestTwoIs("bye"))))
      << "Only the latest messages are reported.";
}

TEST_F(TrellisFixture, InboxMultipleTopicsSameType) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic_1");
  auto pub2 = node_.CreatePublisher<test::Test>("topic_2");

  const auto inbox = Inbox<test::Test, test::Test>{node_, {"topic_1", "topic_2"}, {100ms, 100ms}};

  WaitForDiscovery();

  pub->Send(MakeTest("hello"), kT0);
  pub2->Send(MakeTest("there"), kT0);

  WaitForSendReceive();

  ASSERT_THAT(inbox.GetLatestMessages(kT0),
              FieldsAre(StampedMessageIs(kT0, TestIs("hello")), StampedMessageIs(kT0, TestIs("there"))))
      << "One inbox output per topic.";
}

TEST_F(TrellisFixture, InboxMovable) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic_1");

  auto inbox = Inbox<test::Test>{node_, {"topic_1"}, {100ms}};
  const auto inbox2 = std::move(inbox);

  WaitForDiscovery();

  pub->Send(MakeTest("hello"), kT0);

  WaitForSendReceive();

  ASSERT_THAT(inbox2.GetLatestMessages(kT0), FieldsAre(StampedMessageIs(kT0, TestIs("hello"))))
      << "An inbox can be safely moved!";
}

}  // namespace trellis::core::test
