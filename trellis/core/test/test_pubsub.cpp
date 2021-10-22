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
  auto sub = node_.CreateSubscriber<test::Test>("test_topic", [](const test::Test& msg) {
    ASSERT_EQ(msg.id(), receive_count);
    ++receive_count;
  });

  WaitForDiscovery();

  for (unsigned i = 0; i < 10U; ++i) {
    test::Test test_msg;
    test_msg.set_id(i);
    test_msg.set_msg("hello world");
    pub->Send(test_msg);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  ASSERT_EQ(receive_count, 10U);
}
