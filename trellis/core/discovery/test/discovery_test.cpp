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

#include "trellis/core/discovery/discovery.hpp"

#include <gtest/gtest.h>

#include "trellis/core/test/test.pb.h"

namespace trellis::core::discovery {

namespace {
static constexpr std::string_view test_config = R"(
        trellis:
          discovery:
            interval: 10
            sample_timeout: 200
            port: 45678
        )";
}

TEST(DiscoveryTests, IniitalConditions) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(std::string(test_config))));
  ev.RunFor(std::chrono::milliseconds(200));
  {
    auto samples = discovery.GetPubSubSamples();
    ASSERT_TRUE(samples.empty());
  }
  {
    auto samples = discovery.GetServiceSamples();
    ASSERT_TRUE(samples.empty());
  }
  {  // we should see our own process sample
    auto samples = discovery.GetProcessSamples();
    ASSERT_EQ(samples.size(), 1);
    ASSERT_EQ(samples[0].process().uname(), "test_node");
  }
}

TEST(DiscoveryTests, RegisterPublisher) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(std::string(test_config))));
  discovery.RegisterPublisher<test::Test>("/dummy/publisher", std::vector<std::string>{"memfile1", "memfile2"});
  unsigned receive_count{0};
  discovery.AsyncReceivePublishers([&](Discovery::EventType event, const Sample& sample) {
    ++receive_count;
    ASSERT_EQ(event, Discovery::EventType::kNewRegistration);
    ASSERT_EQ(sample.topic().tname(), "/dummy/publisher");
  });
  ev.RunFor(std::chrono::milliseconds(200));
  {
    auto samples = discovery.GetPubSubSamples();
    ASSERT_EQ(samples.size(), 1);
    ASSERT_EQ(samples[0].topic().tname(), "/dummy/publisher");
  }
  {
    auto samples = discovery.GetServiceSamples();
    ASSERT_TRUE(samples.empty());
  }
  {  // we should see our own process sample
    auto samples = discovery.GetProcessSamples();
    ASSERT_EQ(samples.size(), 1);
    ASSERT_EQ(samples[0].process().uname(), "test_node");
  }

  ASSERT_NE(receive_count, 0);  // exact count dependent on timing
}

TEST(DiscoveryTests, RegisterSubscriber) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(std::string(test_config))));
  discovery.RegisterSubscriber<test::Test>("/dummy/subscriber");

  unsigned receive_count{0};
  discovery.AsyncReceiveSubscribers([&](Discovery::EventType event, const Sample& sample) {
    ++receive_count;
    ASSERT_EQ(event, Discovery::EventType::kNewRegistration);
    ASSERT_EQ(sample.topic().tname(), "/dummy/subscriber");
  });

  ev.RunFor(std::chrono::milliseconds(200));

  auto samples = discovery.GetPubSubSamples();
  ASSERT_EQ(samples.size(), 1);
  ASSERT_EQ(samples[0].topic().tname(), "/dummy/subscriber");
  ASSERT_NE(receive_count, 0);
}

TEST(DiscoveryTests, UnregisterPublisher) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(std::string(test_config))));

  unsigned reg_count{0};
  unsigned unreg_count{0};
  discovery.AsyncReceivePublishers([&](Discovery::EventType event, const Sample& sample) {
    if (event == Discovery::EventType::kNewRegistration) {
      ++reg_count;
    } else if (event == Discovery::EventType::kNewUnregistration) {
      ++unreg_count;
    }
    ASSERT_EQ(sample.topic().tname(), "/to_be_removed");
  });

  auto handle = discovery.RegisterPublisher<test::Test>("/to_be_removed", {"mem1"});
  ev.RunFor(std::chrono::milliseconds(50));
  discovery.Unregister(handle);

  ev.RunFor(std::chrono::milliseconds(400));  // wait longer than timeout
  auto samples = discovery.GetPubSubSamples();
  ASSERT_TRUE(samples.empty());
  ASSERT_NE(reg_count, 0);
  ASSERT_NE(unreg_count, 0);
}

TEST(DiscoveryTests, GetPubSubIdReturnsStableValue) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(std::string(test_config))));
  const auto handle = discovery.RegisterPublisher<test::Test>("/stable/id", {"mem1"});

  const std::string id1 = discovery.GetPubSubId(handle);
  const std::string id2 = discovery.GetPubSubId(handle);

  ASSERT_FALSE(id1.empty());
  ASSERT_EQ(id1, id2);  // ID must be stable
}

TEST(DiscoveryTests, RegisterServiceServer) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(std::string(test_config))));
  discovery.RegisterServiceServer("test_service", 1337);

  unsigned receive_count{0};
  discovery.AsyncReceiveServices([&](Discovery::EventType event, const Sample& sample) {
    ++receive_count;
    ASSERT_EQ(event, Discovery::EventType::kNewRegistration);
    ASSERT_EQ(sample.service().sname(), "test_service");
    ASSERT_EQ(sample.service().tcp_port(), 1337);
  });

  ev.RunFor(std::chrono::milliseconds(200));

  auto samples = discovery.GetServiceSamples();
  ASSERT_EQ(samples.size(), 1);
  ASSERT_EQ(samples[0].service().sname(), "test_service");
  ASSERT_EQ(samples[0].service().tcp_port(), 1337);
  ASSERT_NE(receive_count, 0);
}

}  // namespace trellis::core::discovery
