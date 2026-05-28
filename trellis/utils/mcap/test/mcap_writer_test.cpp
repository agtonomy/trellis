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
#include "trellis/utils/mcap/writer.hpp"

using namespace trellis::core;
using namespace trellis::core::test;

TEST_F(TrellisFixture, McapWriterBasic) {
  static constexpr unsigned kNumMessagesPerChannel = 100U;

  auto pub1 = GetNode().CreatePublisher<test::Test>("mcap_writer_test_topic");
  auto pub2 = GetNode().CreatePublisher<test::TestTwo>("mcap_writer_test_topic2");

  StartRunnerThread();

  const std::string outfile{"/tmp/test_mcap_writer.mcap"};

  // Write
  {
    trellis::utils::mcap::Writer writer(GetNode(), {"mcap_writer_test_topic", "mcap_writer_test_topic2"}, outfile);
    WaitForDiscovery();
    for (unsigned i = 0; i < kNumMessagesPerChannel; ++i) {
      test::Test test_msg;
      test_msg.set_id(i);
      test_msg.set_msg("hello world");
      pub1->Send(test_msg);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));

      test::TestTwo test2_msg;
      test2_msg.set_foo(i * 3.0);
      test2_msg.set_bar(std::to_string(i * 3));
      pub2->Send(test2_msg);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  ::mcap::McapReader reader;

  auto res = reader.open(outfile);
  ASSERT_TRUE(res.ok());

  res = reader.readSummary(::mcap::ReadSummaryMethod::NoFallbackScan);
  ASSERT_TRUE(res.ok());

  auto channels = reader.channels();
  ASSERT_EQ(channels.size(), 2);

  auto messageView = reader.readMessages();
  unsigned chan1_count{0};
  unsigned chan2_count{0};
  for (auto it = messageView.begin(); it != messageView.end(); it++) {
    ASSERT_EQ(it->schema->encoding, "protobuf");
    if (it->schema->name == "trellis.core.test.Test") {
      test::Test msg;
      msg.ParseFromArray(static_cast<const void*>(it->message.data), it->message.dataSize);
      ASSERT_EQ(msg.id(), chan1_count);
      ++chan1_count;
    } else if (it->schema->name == "trellis.core.test.TestTwo") {
      test::TestTwo msg;
      msg.ParseFromArray(static_cast<const void*>(it->message.data), it->message.dataSize);
      ASSERT_EQ(msg.foo(), chan2_count * 3.0);
      ++chan2_count;
    } else {
      ASSERT_TRUE(false);
    }
  }

  ASSERT_EQ(chan1_count, kNumMessagesPerChannel);
  ASSERT_EQ(chan2_count, kNumMessagesPerChannel);
}

TEST_F(TrellisFixture, McapWriterStatistics) {
  static constexpr unsigned kNumMessagesPerChannel = 50U;

  auto pub1 = GetNode().CreatePublisher<test::Test>("mcap_writer_stats_topic1");
  auto pub2 = GetNode().CreatePublisher<test::TestTwo>("mcap_writer_stats_topic2");

  StartRunnerThread();

  const std::string outfile{"/tmp/test_mcap_writer_stats.mcap"};

  const ::mcap::Timestamp start_time{trellis::core::time::NowInNanoseconds()};

  ::mcap::Timestamp on_close_start_time;
  ::mcap::Timestamp on_close_end_time;
  uint64_t on_close_message_count;
  bool was_called{false};
  std::vector<std::string> out_topics;

  auto on_destruction_callback = [&on_close_message_count, &on_close_start_time, &on_close_end_time, &was_called,
                                  &out_topics](const ::mcap::Statistics& stats,
                                               const std::vector<std::string>& topics) {
    on_close_message_count = stats.messageCount;
    on_close_start_time = stats.messageStartTime;
    on_close_end_time = stats.messageEndTime;
    was_called = true;
    out_topics = topics;
  };

  const std::vector<std::string> in_topics{"mcap_writer_stats_topic1", "mcap_writer_stats_topic2"};
  {
    trellis::utils::mcap::Writer writer(GetNode(), in_topics, outfile, ::mcap::McapWriterOptions("protobuf"),
                                        std::chrono::milliseconds{0}, on_destruction_callback);
    WaitForDiscovery();

    for (unsigned i = 0; i < kNumMessagesPerChannel; ++i) {
      test::Test test_msg;
      test_msg.set_id(i);
      test_msg.set_msg("hello world");
      pub1->Send(test_msg);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));

      test::TestTwo test2_msg;
      test2_msg.set_foo(i * 3.0);
      test2_msg.set_bar(std::to_string(i * 3));
      pub2->Send(test2_msg);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Let the subscriber callbacks drain so the stats reflect all sent messages.
    WaitForSendReceive();
  }

  ASSERT_TRUE(was_called);

  const ::mcap::Timestamp end_time{trellis::core::time::NowInNanoseconds()};

  EXPECT_EQ(on_close_message_count, 2 * kNumMessagesPerChannel);

  EXPECT_GT(on_close_start_time, start_time);
  EXPECT_LT(on_close_end_time, end_time);
  EXPECT_LE(on_close_start_time, on_close_end_time);
  ASSERT_EQ(in_topics.size(), out_topics.size());
  // the following works because there are only two entries in the vectors
  EXPECT_EQ(in_topics.front(), out_topics.front());
  EXPECT_EQ(in_topics.back(), out_topics.back());
}