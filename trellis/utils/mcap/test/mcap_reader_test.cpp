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

#include <gtest/gtest.h>

#include <iostream>

#include "mcap/reader.hpp"
#include "trellis/core/test/test.pb.h"
#include "trellis/core/test/test_fixture.hpp"
#include "trellis/utils/mcap/reader.hpp"
#include "trellis/utils/mcap/writer.hpp"

using namespace trellis::core;
using namespace trellis::core::test;

TEST_F(TrellisFixture, McapReaderBasic) {
  static constexpr unsigned kNumMessagesPerChannel = 100U;

  auto pub1 = GetNode().CreatePublisher<test::Test>("mcap_reader_test_topic");
  auto pub2 = GetNode().CreatePublisher<test::TestTwo>("mcap_reader_test_topic2");

  StartRunnerThread();

  const std::string outfile{"/tmp/test_mcap_reader.mcap"};

  // Write
  {
    trellis::utils::mcap::Writer writer(GetNode(), {"mcap_reader_test_topic", "mcap_reader_test_topic2"}, outfile);
    WaitForDiscovery();
    for (unsigned i = 0; i < kNumMessagesPerChannel; ++i) {
      test::Test test_msg;
      test_msg.set_id(i);
      test_msg.set_msg("hello world");
      pub1->Send(test_msg);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      test::TestTwo test2_msg;
      test2_msg.set_foo(i * 3.0);
      test2_msg.set_bar(std::to_string(i * 3));
      pub2->Send(test2_msg);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  // Read
  {
    trellis::utils::mcap::Reader reader(outfile);
    ASSERT_TRUE(reader.HasTopic("mcap_reader_test_topic"));
    ASSERT_TRUE(reader.HasTopic("mcap_reader_test_topic2"));

    // Statistics
    auto stats = *reader.Statistics();
    ASSERT_EQ(stats.messageCount, kNumMessagesPerChannel * 2);
    ASSERT_EQ(stats.schemaCount, 2);
    ASSERT_EQ(stats.channelCount, 2);
    ASSERT_GT(stats.messageEndTime, stats.messageStartTime);

    {  // scan through mcap_reader_test_topic2 channel
      auto view = reader.ReadMessagesFromTopic<test::TestTwo>("mcap_reader_test_topic2");
      unsigned read_count{0};
      trellis::core::time::TimePoint last_timestamp_{};
      for (auto it = view.begin(); it != view.end(); ++it) {
        ASSERT_GT((*it).timestamp, last_timestamp_);
        ASSERT_EQ((*it).msg.foo(), read_count * 3.0);
        ASSERT_EQ((*it).msg.bar(), std::to_string(read_count * 3));
        ++read_count;
        last_timestamp_ = (*it).timestamp;
      }
      ASSERT_EQ(read_count, kNumMessagesPerChannel);
    }

    {  // scan through mcap_reader_test_topic channel
      auto view = reader.ReadMessagesFromTopic<test::Test>("mcap_reader_test_topic");
      unsigned read_count{0};
      trellis::core::time::TimePoint last_timestamp_{};
      for (auto it = view.begin(); it != view.end(); ++it) {
        ASSERT_GT((*it).timestamp, last_timestamp_);
        ASSERT_EQ((*it).msg.id(), read_count);
        ASSERT_EQ((*it).msg.msg(), "hello world");
        ++read_count;
        last_timestamp_ = (*it).timestamp;
      }
      ASSERT_EQ(read_count, kNumMessagesPerChannel);
    }

    {
      // scan through both topics
      unsigned read_count{0};
      unsigned long long last_timestamp_{};
      auto view = reader.ReadMessagesFromTopicSet({"mcap_reader_test_topic2", "mcap_reader_test_topic"});
      for (auto it = view.begin(); it != view.end(); ++it) {
        ASSERT_GT(it->message.publishTime, last_timestamp_);
        last_timestamp_ = it->message.publishTime;
        ++read_count;
      }
      ASSERT_EQ(read_count, stats.messageCount);
    }
  }
}
