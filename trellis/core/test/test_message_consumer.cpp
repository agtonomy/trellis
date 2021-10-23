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

#include "trellis/core/message_consumer.hpp"
#include "trellis/core/test/test.pb.h"
#include "trellis/core/test/test_fixture.hpp"

using namespace trellis::core;
using namespace trellis::core::test;

TEST_F(TrellisFixture, MultipleMessageTypesWithIndividualCallbacks) {
  static unsigned receive_count_1{0};
  static unsigned receive_count_2{0};
  // Simply start the runner thread and then test that it will gracefully
  // stop without hanging
  StartRunnerThread();

  auto pub = node_.CreatePublisher<test::Test>("consumer_topic_1");
  auto pub2 = node_.CreatePublisher<test::TestTwo>("consumer_topic_2");

  trellis::core::MessageConsumer<1, test::Test, test::TestTwo> inputs_{
      node_,
      {{"consumer_topic_1", "consumer_topic_2"}},
      {[this](const test::Test& msg, const time::TimePoint&) {
         ASSERT_EQ(receive_count_1, msg.id());
         ++receive_count_1;
       },
       [this](const test::TestTwo& msg, const time::TimePoint&) {
         ASSERT_FLOAT_EQ(receive_count_2, msg.foo() / 2.0);
         ++receive_count_2;
       }}};

  WaitForDiscovery();

  // Publish messages on both topics
  for (unsigned i = 0; i < 10U; ++i) {
    test::Test test_msg;
    test::TestTwo test_msg2;
    test_msg.set_id(i);
    test_msg.set_msg("hello world");
    test_msg2.set_foo(i * 2.0);
    pub->Send(test_msg);
    pub2->Send(test_msg2);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  ASSERT_EQ(receive_count_1, 10U);
  ASSERT_EQ(receive_count_2, 10U);
}
