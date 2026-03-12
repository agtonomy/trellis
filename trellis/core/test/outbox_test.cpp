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

#include "trellis/core/outbox.hpp"

#include <gmock/gmock.h>

#include "trellis/core/inbox.hpp"
#include "trellis/core/test/test.hpp"
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
auto StampedMessageIs(const Matcher<MSG_T>& message_matcher) {
  return MessageIs(message_matcher);
}

Matcher<test::Test> TestIs(const std::string msg) { return Property("msg", &test::Test::msg, Eq(msg)); }

Matcher<arbitrary::Test> ArbitraryTestIs(const std::string msg) { return Field(&arbitrary::Test::msg, msg); }

Matcher<TestTwo> TestTwoIs(const std::string bar) { return Property("bar", &TestTwo::bar, Eq(bar)); }

}  // namespace

TEST_F(TrellisFixture, Constraints) {
  static_assert(_Sender<ImmediateSender<test::Test>>);
  static_assert(_Sender<AsyncSender<test::Test>>);
}

TEST_F(TrellisFixture, OutboxNoMessages) {
  StartRunnerThread();

  const auto inbox = Inbox<Latest<test::Test>>{GetNode(), {"topic_1"}, {100ms}};
  auto outbox = Outbox<ImmediateSender<test::Test>>{GetNode(), {"topic_1"}};

  WaitForDiscovery();

  outbox.UpdateMsgs(Outbox<ImmediateSender<test::Test>>::Messages{});

  WaitForSendReceive();

  ASSERT_THAT(inbox.GetMessages(kT0), FieldsAre(Eq(std::nullopt))) << "No messages sent!";
}

TEST_F(TrellisFixture, OutboxMessages) {
  StartRunnerThread();

  const auto inbox = Inbox<Latest<test::Test>, Latest<TestTwo>>{GetNode(), {"topic_1", "topic_2"}, {100ms, 100ms}};
  auto outbox = Outbox<ImmediateSender<test::Test>, AsyncSender<TestTwo>>{GetNode(), {"topic_1"}, {"topic_2", 50}};

  WaitForDiscovery();

  outbox.UpdateMsgs({MakeTest("hello"), MakeTestTwo("there")});

  WaitForSendReceive();

  ASSERT_THAT(inbox.GetMessages(kT0),
              FieldsAre(Optional(StampedMessageIs(TestIs("hello"))), Optional(StampedMessageIs(TestTwoIs("there")))))
      << "Messages sent at receive time, so are still valid.";
}

TEST_F(TrellisFixture, OutboxStaleMessages) {
  StartRunnerThread();

  const auto inbox = Inbox<Latest<test::Test>, Latest<TestTwo>>{GetNode(), {"topic_1", "topic_2"}, {100ms, 100ms}};
  auto outbox = Outbox<ImmediateSender<test::Test>, AsyncSender<TestTwo>>{GetNode(), {"topic_1"}, {"topic_2", 500}};

  WaitForDiscovery();

  outbox.UpdateMsgs({MakeTest("hello"), MakeTestTwo("my name is stale")});

  WaitForSendReceive();

  ASSERT_THAT(inbox.GetMessages(kT0), FieldsAre(Optional(StampedMessageIs(TestIs("hello"))), Eq(std::nullopt)))
      << "The async sender message has timed out, so we only see the first message in the inbox!";
}

TEST_F(TrellisFixture, ConvertingOutbox) {
  StartRunnerThread();

  using FromConverterT = std::function<decltype(arbitrary::FromProto)>;
  const auto inbox = Inbox<Latest<test::Test, arbitrary::Test, FromConverterT>>{
      GetNode(), {"topic_1"}, {100ms}, {arbitrary::FromProto}};
  using ToConverterT = std::function<decltype(arbitrary::ToProto)>;
  auto outbox =
      Outbox<ImmediateSender<test::Test, arbitrary::Test, ToConverterT>>{GetNode(), {"topic_1", arbitrary::ToProto}};

  WaitForDiscovery();

  const std::string msg = "wow automatic conversions are convenient";

  outbox.UpdateMsgs({arbitrary::Test{.msg = msg}});

  WaitForSendReceive();

  ASSERT_THAT(inbox.GetMessages(kT0), FieldsAre(Optional(StampedMessageIs(ArbitraryTestIs(msg)))));
}

}  // namespace trellis::core::test
