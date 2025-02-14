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
using testing::ElementsAre;
using testing::Eq;
using testing::ExplainMatchResult;
using testing::Field;
using testing::FieldsAre;
using testing::FloatEq;
using testing::IsEmpty;
using testing::Matcher;
using testing::Optional;
using testing::Property;
using testing::SizeIs;
using testing::StrEq;
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
  return AllOf(Field("timestamp", &StampedMessage<MSG_T>::timestamp, Eq(time)), MessageIs(message_matcher));
}

template <typename MSG_T>
auto OwningMessageIs(const TimePoint& time, const Matcher<MSG_T>& message_matcher) {
  return AllOf(Field("timestamp", &OwningStampedMessage<MSG_T>::timestamp, Eq(time)),
               Field("message", &OwningStampedMessage<MSG_T>::message, message_matcher));
}

Matcher<test::Test> TestIs(const std::string msg) { return Property("msg", &test::Test::msg, Eq(msg)); }

Matcher<TestTwo> TestTwoIs(const std::string bar) { return Property("bar", &TestTwo::bar, Eq(bar)); }

}  // namespace

static_assert(IsLatestReceiveType<Latest<int>>, "Test IsLatestReceiveType concept.");
static_assert(!IsLatestReceiveType<int>, "Test IsLatestReceiveType concept, not satisfied by arbitrary type.");
static_assert(!IsLatestReceiveType<NLatest<int, 7>>,
              "Test IsLatestReceiveType concept, not satisfied by NLatest type.");
static_assert(!IsLatestReceiveType<AllLatest<int>>,
              "Test IsLatestReceiveType concept, not satisfied by AllLatest type.");
static_assert(!IsLatestReceiveType<Loopback<int>>, "Test IsLatestReceiveType concept, not satisfied by Loopback type.");

static_assert(IsNLatestReceiveType<NLatest<int, 7>>, "Test IsNLatestReceiveType concept.");
static_assert(!IsNLatestReceiveType<int>, "Test IsNLatestReceiveType concept, not satisfied by arbitrary type.");
static_assert(!IsNLatestReceiveType<Latest<int>>, "Test IsNLatestReceiveType concept, not satisfied by Latest type.");
static_assert(!IsNLatestReceiveType<AllLatest<int>>,
              "Test IsNLatestReceiveType concept, not satisfied by AllLatest type.");
static_assert(!IsNLatestReceiveType<Loopback<int>>,
              "Test IsNLatestReceiveType concept, not satisfied by Loopback type.");

static_assert(IsAllLatestReceiveType<AllLatest<int>>, "Test IsAllLatestReceiveType concept.");
static_assert(!IsAllLatestReceiveType<int>, "Test IsAllLatestReceiveType concept, not satisfied by arbitrary type.");
static_assert(!IsAllLatestReceiveType<Latest<int>>,
              "Test IsAllLatestReceiveType concept, not satisfied by Latest type.");
static_assert(!IsAllLatestReceiveType<NLatest<int, 7>>,
              "Test IsAllLatestReceiveType concept, not satisfied by NLatest type.");
static_assert(!IsAllLatestReceiveType<Loopback<int>>,
              "Test IsAllLatestReceiveType concept, not satisfied by Loopback type.");

static_assert(IsLoopbackReceiveType<Loopback<int>>, "Test IsLoopbackReceiveType concept.");
static_assert(!IsLoopbackReceiveType<int>, "Test IsLoopbackReceiveType concept, not satisfied by arbitrary type.");
static_assert(!IsLoopbackReceiveType<Latest<int>>, "Test IsLoopbackReceiveType concept, not satisfied by Latest type.");
static_assert(!IsLoopbackReceiveType<NLatest<int, 7>>,
              "Test IsLoopbackReceiveType concept, not satisfied by NLatest type.");
static_assert(!IsLoopbackReceiveType<AllLatest<int>>,
              "Test IsLoopbackReceiveType concept, not satisfied by AllLatest type.");

static_assert(IsReceiveType<Latest<int>>, "Test IsReceiveType concept, satisfied by Latest.");
static_assert(IsReceiveType<NLatest<int, 7>>, "Test IsReceiveType concept, satisfied by NLatest.");
static_assert(IsReceiveType<AllLatest<int>>, "Test IsReceiveType concept, satisfied by AllLatest.");
static_assert(IsReceiveType<Loopback<int>>, "Test IsReceiveType concept, satisfied by Loopback.");

TEST_F(TrellisFixture, InboxNoMessages) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic_1");
  auto pub2 = node_.CreatePublisher<TestTwo>("topic_2");

  const auto inbox = Inbox<Latest<test::Test>, Latest<TestTwo>>{node_, {"topic_1", "topic_2"}, {100ms, 100ms}};

  WaitForDiscovery();

  WaitForSendReceive();

  ASSERT_THAT(inbox.GetMessages(kT0), FieldsAre(Eq(std::nullopt), Eq(std::nullopt))) << "No messages sent!";
}

TEST_F(TrellisFixture, InboxMessagesReceived) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic_1");
  auto pub2 = node_.CreatePublisher<TestTwo>("topic_2");

  const auto inbox = Inbox<Latest<test::Test>, Latest<TestTwo>>{node_, {"topic_1", "topic_2"}, {100ms, 100ms}};

  WaitForDiscovery();

  pub->Send(MakeTest("hello"), kT0);
  pub2->Send(MakeTestTwo("there"), kT0);

  WaitForSendReceive();

  ASSERT_THAT(inbox.GetMessages(kT0), FieldsAre(Optional(StampedMessageIs(kT0, TestIs("hello"))),
                                                Optional(StampedMessageIs(kT0, TestTwoIs("there")))))
      << "Messages sent at receive time, so are still valid.";
}

TEST_F(TrellisFixture, InboxMessageTimeout) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic_1");
  auto pub2 = node_.CreatePublisher<TestTwo>("topic_2");

  const auto inbox = Inbox<Latest<test::Test>, Latest<TestTwo>>{node_, {"topic_1", "topic_2"}, {100ms, 100ms}};

  WaitForDiscovery();

  pub->Send(MakeTest("hello"), kT0);
  pub2->Send(MakeTestTwo("there"), kT0 + 1ms);

  WaitForSendReceive();

  ASSERT_THAT(inbox.GetMessages(kT0 + 101ms),
              FieldsAre(Eq(std::nullopt), Optional(StampedMessageIs(kT0 + 1ms, TestTwoIs("there")))))
      << "First message has timed out by 1ms.";
}

TEST_F(TrellisFixture, InboxMissedMessages) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic_1");
  auto pub2 = node_.CreatePublisher<TestTwo>("topic_2");

  const auto inbox = Inbox<Latest<test::Test>, Latest<TestTwo>>{node_, {"topic_1", "topic_2"}, {100ms, 100ms}};

  WaitForDiscovery();

  pub->Send(MakeTest("hello"), kT0);
  pub2->Send(MakeTestTwo("there"), kT0);

  WaitForSendReceive();

  pub->Send(MakeTest("good"), kT0 + 1ms);
  pub2->Send(MakeTestTwo("bye"), kT0 + 1ms);

  WaitForSendReceive();

  ASSERT_THAT(inbox.GetMessages(kT0 + 2ms), FieldsAre(Optional(StampedMessageIs(kT0 + 1ms, TestIs("good"))),
                                                      Optional(StampedMessageIs(kT0 + 1ms, TestTwoIs("bye")))))
      << "Only the latest messages are reported.";
}

TEST_F(TrellisFixture, InboxMultipleTopicsSameType) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic_1");
  auto pub2 = node_.CreatePublisher<test::Test>("topic_2");

  const auto inbox = Inbox<Latest<test::Test>, Latest<test::Test>>{node_, {"topic_1", "topic_2"}, {100ms, 100ms}};

  WaitForDiscovery();

  pub->Send(MakeTest("hello"), kT0);
  pub2->Send(MakeTest("there"), kT0);

  WaitForSendReceive();

  ASSERT_THAT(inbox.GetMessages(kT0), FieldsAre(Optional(StampedMessageIs(kT0, TestIs("hello"))),
                                                Optional(StampedMessageIs(kT0, TestIs("there")))))
      << "One inbox output per topic.";
}

TEST_F(TrellisFixture, InboxMovable) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic");

  auto inbox = Inbox<Latest<test::Test>>{node_, {"topic"}, {100ms}};
  const auto inbox2 = std::move(inbox);

  WaitForDiscovery();

  pub->Send(MakeTest("hello"), kT0);

  WaitForSendReceive();

  ASSERT_THAT(inbox2.GetMessages(kT0), FieldsAre(Optional(StampedMessageIs(kT0, TestIs("hello")))))
      << "An inbox can be safely moved!";
}

TEST_F(TrellisFixture, InboxNLatestEmpty) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic");

  const auto inbox = Inbox<NLatest<test::Test, 3>>{node_, {"topic"}, {100ms}};

  WaitForDiscovery();

  WaitForSendReceive();

  ASSERT_THAT(inbox.GetMessages(kT0), FieldsAre(IsEmpty())) << "No messages in N latest.";
}

TEST_F(TrellisFixture, InboxNLatestNotFull) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic");

  const auto inbox = Inbox<NLatest<test::Test, 3>>{node_, {"topic"}, {100ms}};

  WaitForDiscovery();

  pub->Send(MakeTest("hello"), kT0);

  WaitForSendReceive();

  ASSERT_THAT(inbox.GetMessages(kT0), FieldsAre(ElementsAre(StampedMessageIs(kT0, TestIs("hello")))))
      << "Only 1 message has been sent, so we only get one back.";
}

TEST_F(TrellisFixture, InboxNLatestFull) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic");

  const auto inbox = Inbox<NLatest<test::Test, 3>>{node_, {"topic"}, {100ms}};

  WaitForDiscovery();

  pub->Send(MakeTest("hello"), kT0);
  WaitForSendReceive();
  pub->Send(MakeTest("hello1"), kT0 + 1ms);
  WaitForSendReceive();
  pub->Send(MakeTest("hello2"), kT0 + 2ms);
  WaitForSendReceive();

  ASSERT_THAT(inbox.GetMessages(kT0 + 3ms), FieldsAre(ElementsAre(StampedMessageIs(kT0, TestIs("hello")),
                                                                  StampedMessageIs(kT0 + 1ms, TestIs("hello1")),
                                                                  StampedMessageIs(kT0 + 2ms, TestIs("hello2")))))
      << "Multiple messages received, multiple messages returned.";
}

TEST_F(TrellisFixture, InboxNLatestOverflow) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic");

  const auto inbox = Inbox<NLatest<test::Test, 3>>{node_, {"topic"}, {100ms}};

  WaitForDiscovery();

  pub->Send(MakeTest("hello"), kT0);
  WaitForSendReceive();
  pub->Send(MakeTest("hello1"), kT0 + 1ms);
  WaitForSendReceive();
  pub->Send(MakeTest("hello2"), kT0 + 2ms);
  WaitForSendReceive();
  pub->Send(MakeTest("hello3"), kT0 + 3ms);
  WaitForSendReceive();

  ASSERT_THAT(inbox.GetMessages(kT0 + 4ms), FieldsAre(ElementsAre(StampedMessageIs(kT0 + 1ms, TestIs("hello1")),
                                                                  StampedMessageIs(kT0 + 2ms, TestIs("hello2")),
                                                                  StampedMessageIs(kT0 + 3ms, TestIs("hello3")))))
      << "Messages more than 3 old are dropped.";
}

TEST_F(TrellisFixture, InboxNLatestTimeout) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic");

  const auto inbox = Inbox<NLatest<test::Test, 3>>{node_, {"topic"}, {100ms}};

  WaitForDiscovery();

  pub->Send(MakeTest("hello"), kT0);
  WaitForSendReceive();
  pub->Send(MakeTest("hello1"), kT0 + 1ms);
  WaitForSendReceive();
  pub->Send(MakeTest("hello2"), kT0 + 2ms);
  WaitForSendReceive();

  ASSERT_THAT(inbox.GetMessages(kT0 + 101ms), FieldsAre(ElementsAre(StampedMessageIs(kT0 + 1ms, TestIs("hello1")),
                                                                    StampedMessageIs(kT0 + 2ms, TestIs("hello2")))))
      << "The oldest message has timed out.";
}

TEST_F(TrellisFixture, InboxAllLatestEmpty) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic");

  const auto inbox = Inbox<AllLatest<test::Test>>{node_, {"topic"}, {100ms}};

  WaitForDiscovery();

  WaitForSendReceive();

  ASSERT_THAT(inbox.GetMessages(kT0), FieldsAre(IsEmpty())) << "No messages in all latest.";
}

TEST_F(TrellisFixture, InboxAllLatestOne) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic");

  const auto inbox = Inbox<AllLatest<test::Test>>{node_, {"topic"}, {100ms}};

  WaitForDiscovery();

  pub->Send(MakeTest("hello"), kT0);

  WaitForSendReceive();

  ASSERT_THAT(inbox.GetMessages(kT0), FieldsAre(ElementsAre(StampedMessageIs(kT0, TestIs("hello")))))
      << "Only 1 message has been sent, so we only get one back.";
}

TEST_F(TrellisFixture, InboxAllLatestMany) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic");

  const auto inbox = Inbox<AllLatest<test::Test>>{node_, {"topic"}, {100ms}};

  WaitForDiscovery();

  pub->Send(MakeTest("hello"), kT0);
  WaitForSendReceive();
  pub->Send(MakeTest("hello1"), kT0 + 1ms);
  WaitForSendReceive();
  pub->Send(MakeTest("hello2"), kT0 + 2ms);
  WaitForSendReceive();

  ASSERT_THAT(inbox.GetMessages(kT0 + 3ms), FieldsAre(ElementsAre(StampedMessageIs(kT0, TestIs("hello")),
                                                                  StampedMessageIs(kT0 + 1ms, TestIs("hello1")),
                                                                  StampedMessageIs(kT0 + 2ms, TestIs("hello2")))))
      << "Multiple messages received, multiple messages returned.";
}

TEST_F(TrellisFixture, InboxAllLatestTimeout) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic");

  const auto inbox = Inbox<AllLatest<test::Test>>{node_, {"topic"}, {100ms}};

  WaitForDiscovery();

  pub->Send(MakeTest("hello"), kT0);
  WaitForSendReceive();
  pub->Send(MakeTest("hello1"), kT0 + 1ms);
  WaitForSendReceive();
  pub->Send(MakeTest("hello2"), kT0 + 2ms);
  WaitForSendReceive();

  ASSERT_THAT(inbox.GetMessages(kT0 + 101ms), FieldsAre(ElementsAre(StampedMessageIs(kT0 + 1ms, TestIs("hello1")),
                                                                    StampedMessageIs(kT0 + 2ms, TestIs("hello2")))))
      << "The oldest message has timed out.";
}

TEST_F(TrellisFixture, InboxSimpleLoopback) {
  StartRunnerThread();

  auto recv_cnt = int{};
  const auto sub = node_.CreateSubscriber<test::Test>("topic", [&recv_cnt](auto, auto, auto) { ++recv_cnt; });

  auto inbox = Inbox<Loopback<test::Test>>{node_, {"topic"}, {100ms}};

  WaitForDiscovery();

  inbox.Send(MakeTest("hello"), kT0);
  WaitForSendReceive();

  ASSERT_THAT(recv_cnt, Eq(1)) << "Message was sent.";
  ASSERT_THAT(inbox.GetMessages(kT0 + 100ms), FieldsAre(Optional(StampedMessageIs(kT0, TestIs("hello")))))
      << "Message stored.";
}

TEST_F(TrellisFixture, InboxSimpleLoopbackTimeout) {
  StartRunnerThread();

  auto recv_cnt = int{};
  const auto sub = node_.CreateSubscriber<test::Test>("topic", [&recv_cnt](auto, auto, auto) { ++recv_cnt; });

  auto inbox = Inbox<Loopback<test::Test>>{node_, {"topic"}, {100ms}};

  WaitForDiscovery();

  inbox.Send(MakeTest("hello"), kT0);
  WaitForSendReceive();

  ASSERT_THAT(recv_cnt, Eq(1)) << "Message was sent.";
  ASSERT_THAT(inbox.GetMessages(kT0 + 101ms), FieldsAre(Eq(std::nullopt))) << "Message timed out.";
}

namespace {

struct Serializer {
  test::Test operator()(const std::string& in) {
    auto out = test::Test{};
    out.set_msg(in);
    return out;
  }
};

}  // namespace

TEST_F(TrellisFixture, InboxSerializingLoopback) {
  StartRunnerThread();

  auto latest = std::string{};
  const auto sub =
      node_.CreateSubscriber<test::Test>("topic", [&latest](auto, auto, auto msg) { latest = msg->msg(); });

  auto inbox = Inbox<Loopback<std::string, test::Test, Serializer>>{node_, {"topic"}, {100ms}};

  WaitForDiscovery();

  inbox.Send(std::string{"hello"}, kT0);
  WaitForSendReceive();

  ASSERT_THAT(latest, StrEq("hello")) << "Message was sent.";
  ASSERT_THAT(inbox.GetMessages(kT0 + 100ms), FieldsAre(Optional(StampedMessageIs<std::string>(kT0, StrEq("hello")))))
      << "Message stored.";
}

TEST_F(TrellisFixture, GetMessagesCopy) {
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("topic_1");
  auto pub2 = node_.CreatePublisher<TestTwo>("topic_2");

  auto inbox = Inbox<Latest<test::Test>, NLatest<TestTwo, 5>, AllLatest<test::Test>,
                     Loopback<std::string, test::Test, Serializer>>{
      node_, {"topic_1", "topic_2", "topic_1", "loopback_topic"}, {100ms, 100ms, 100ms, 100ms}};

  WaitForDiscovery();

  pub->Send(MakeTest("hello"), kT0);
  pub2->Send(MakeTestTwo("there"), kT0 + 1ms);
  inbox.Send(std::string{"howdy"}, kT0 + 2ms);

  WaitForSendReceive();

  ASSERT_THAT(inbox.GetMessagesCopy(kT0), FieldsAre(Optional(OwningMessageIs(kT0, TestIs("hello"))),
                                                    ElementsAre(OwningMessageIs(kT0 + 1ms, TestTwoIs("there"))),
                                                    ElementsAre(OwningMessageIs(kT0, TestIs("hello"))),
                                                    Optional(OwningMessageIs<std::string>(kT0 + 2ms, StrEq("howdy")))));

  auto promise = std::promise<decltype(inbox)::OwningMessages>{};
  auto future = promise.get_future();
  asio::post(*node_.GetEventLoop(), [&inbox, promise = std::move(promise)]() mutable {
    return promise.set_value(inbox.GetMessagesCopy(kT0));
  });
  const auto result = future.get();
  ASSERT_THAT(result, FieldsAre(Optional(OwningMessageIs(kT0, TestIs("hello"))),
                                ElementsAre(OwningMessageIs(kT0 + 1ms, TestTwoIs("there"))),
                                ElementsAre(OwningMessageIs(kT0, TestIs("hello"))),
                                Optional(OwningMessageIs<std::string>(kT0 + 2ms, StrEq("howdy")))))
      << "Demonstrate that GetMessagesCopy can be called from the event loop.";

  ASSERT_THAT(inbox.GetMessagesCopy(kT0 + 110ms), FieldsAre(Eq(std::nullopt), IsEmpty(), IsEmpty(), Eq(std::nullopt)))
      << "Messages have timed out.";
}

}  // namespace trellis::core::test
