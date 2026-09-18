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
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "trellis/core/discovery/utils.hpp"
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

static constexpr std::string_view test_config_otherport = R"(
        trellis:
          discovery:
            interval: 10
            sample_timeout: 200
            port: 45679
        )";

static constexpr std::string_view test_config_loopback = R"(
        trellis:
          discovery:
            interval: 10
            sample_timeout: 200
            loopback_enabled: true
            port: 45678
        )";

// The heartbeat here outlasts any test, so a callback that fires within a short RunFor was delivered by the
// immediate path.
static constexpr std::string_view test_config_loopback_no_heartbeat = R"(
        trellis:
          discovery:
            interval: 100000
            sample_timeout: 200000
            loopback_enabled: true
            port: 45687
        )";

// Poll tests use their own ports so a slow drain in one of them cannot show up as a stray sample in another.
std::string MakeConfig(unsigned port, unsigned poll_interval_ms) {
  return "trellis:\n"
         "  discovery:\n"
         "    interval: 10\n"
         "    sample_timeout: 200\n"
         "    port: " +
         std::to_string(port) + "\n    poll_interval_ms: " + std::to_string(poll_interval_ms) + "\n";
}

bool HasTopic(const std::vector<Sample>& samples, const std::string& topic) {
  return std::any_of(samples.begin(), samples.end(),
                     [&topic](const Sample& sample) { return sample.topic().tname() == topic; });
}
}  // namespace

TEST(DiscoveryTests, IniitalConditions) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(std::string(test_config))));
  ASSERT_FALSE(discovery.GetConfig().loopback_enabled);
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
  discovery.RegisterPublisher<test::Test>("/dummy/publisher", "memfile", 2u);
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

// A callback added after the publisher registered catches up on it when it is added, not on the next heartbeat.
TEST(DiscoveryTests, LoopbackDiscoversPublisherRegisteredBeforeCallback) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev,
                      trellis::core::Config(YAML::Load(std::string(test_config_loopback_no_heartbeat))));
  discovery.RegisterPublisher<test::Test>("/dummy/publisher", "memfile", 2u);
  unsigned receive_count{0};
  discovery.AsyncReceivePublishers([&](Discovery::EventType event, const Sample& sample) {
    ++receive_count;
    EXPECT_EQ(event, Discovery::EventType::kNewRegistration);
    EXPECT_EQ(sample.topic().tname(), "/dummy/publisher");
  });
  ev.RunFor(std::chrono::milliseconds(10));
  EXPECT_GT(receive_count, 0u);
}

// A publisher registered after the callback reaches it when it registers, not on the next heartbeat.
TEST(DiscoveryTests, LoopbackDiscoversPublisherRegisteredAfterCallback) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev,
                      trellis::core::Config(YAML::Load(std::string(test_config_loopback_no_heartbeat))));
  unsigned receive_count{0};
  discovery.AsyncReceivePublishers([&](Discovery::EventType event, const Sample& sample) {
    ++receive_count;
    EXPECT_EQ(event, Discovery::EventType::kNewRegistration);
    EXPECT_EQ(sample.topic().tname(), "/dummy/publisher");
  });
  discovery.RegisterPublisher<test::Test>("/dummy/publisher", "memfile", 2u);
  ev.RunFor(std::chrono::milliseconds(10));
  EXPECT_GT(receive_count, 0u);
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

  auto handle = discovery.RegisterPublisher<test::Test>("/to_be_removed", "mem", 1u);
  ev.RunFor(std::chrono::milliseconds(50));
  discovery.Unregister(handle);

  ev.RunFor(std::chrono::milliseconds(400));  // wait longer than timeout
  auto samples = discovery.GetPubSubSamples();
  ASSERT_TRUE(samples.empty());
  ASSERT_NE(reg_count, 0);
  ASSERT_NE(unreg_count, 0);
}

TEST(DiscoveryTests, GetSampleIdReturnsStableValue) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(std::string(test_config))));
  const auto handle = discovery.RegisterPublisher<test::Test>("/stable/id", "mem", 1u);

  const std::string id1 = discovery.GetSampleId(handle);
  const std::string id2 = discovery.GetSampleId(handle);

  ASSERT_FALSE(id1.empty());
  ASSERT_EQ(id1, id2);  // ID must be stable
}

TEST(DiscoveryTests, RegisterServiceServer) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(std::string(test_config))));
  discovery.RegisterServiceServer("test_service", 1337, ipc::proto::rpc::MethodsMap{});

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

TEST(DiscoveryTests, RegisterPublisherWithLargeTopicName) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(std::string(test_config))));

  // Create a very large topic name to force multi-packet transmission
  // UDP buffer is 65535 bytes, so create topic name > 80KB to ensure multi-packet
  const std::string large_topic_name = "/very/large/topic/name/" + std::string(80000, 'x');

  discovery.RegisterPublisher<test::Test>(large_topic_name, "memfile", 2u);

  unsigned receive_count{0};
  std::string received_topic_name;
  discovery.AsyncReceivePublishers([&](Discovery::EventType event, const Sample& sample) {
    ++receive_count;
    ASSERT_EQ(event, Discovery::EventType::kNewRegistration);
    received_topic_name = sample.topic().tname();
  });

  ev.RunFor(std::chrono::milliseconds(200));

  {
    auto samples = discovery.GetPubSubSamples();
    ASSERT_EQ(samples.size(), 1);
    ASSERT_EQ(samples[0].topic().tname(), large_topic_name);
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

  ASSERT_NE(receive_count, 0);                       // exact count dependent on timing
  ASSERT_EQ(received_topic_name, large_topic_name);  // verify large topic name was received correctly
}

TEST(DiscoveryTests, MultipleNodes) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery1("test_node1", ev, trellis::core::Config(YAML::Load(std::string(test_config))));
  Discovery discovery2("test_node2", ev, trellis::core::Config(YAML::Load(std::string(test_config))));
  ev.RunFor(std::chrono::milliseconds(200));
  {  // we should see both process samples
    auto samples = discovery1.GetProcessSamples();
    ASSERT_EQ(samples.size(), 2);
  }
  {
    auto samples = discovery2.GetProcessSamples();
    ASSERT_EQ(samples.size(), 2);
  }
}

// test that we only see our own node if discovery is running on a separate port
TEST(DiscoveryTests, MultipleNodesSeparatePorts) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery1("test_node1", ev, trellis::core::Config(YAML::Load(std::string(test_config))));
  Discovery discovery2("test_node2", ev, trellis::core::Config(YAML::Load(std::string(test_config_otherport))));
  ev.RunFor(std::chrono::milliseconds(200));
  {
    auto samples = discovery1.GetProcessSamples();
    ASSERT_EQ(samples.size(), 1);
    ASSERT_EQ(samples[0].process().uname(), "test_node1");
  }
  {
    auto samples = discovery2.GetProcessSamples();
    ASSERT_EQ(samples.size(), 1);
    ASSERT_EQ(samples[0].process().uname(), "test_node2");
  }
}

// A peer must be able to resolve a publisher's schema purely from the broadcast `desc_hash`, with no
// inline descriptor bytes on the wire.
TEST(DiscoveryTests, ResolveDescriptorViaHashAcrossPeers) {
  // Isolate the descriptor store under a per-process dir so the test never touches the production
  // default (/tmp/trellis/descriptors) or races a live node sharing it.
  const auto descriptor_dir =
      std::filesystem::temp_directory_path() / ("trellis_discovery_test_descriptors_" + std::to_string(::getpid()));
  std::error_code ec;
  std::filesystem::remove_all(descriptor_dir, ec);
  const std::string config_yaml =
      "trellis:\n"
      "  discovery:\n"
      "    interval: 10\n"
      "    sample_timeout: 200\n"
      "    port: 45678\n"
      "    descriptor_dir: " +
      descriptor_dir.string() + "\n";

  auto ev = trellis::core::EventLoop();
  Discovery publisher("pub_node", ev, trellis::core::Config(YAML::Load(config_yaml)));
  Discovery subscriber("sub_node", ev, trellis::core::Config(YAML::Load(config_yaml)));

  publisher.RegisterPublisher<test::Test>("/ref/topic", "memfile", 2u);
  ev.RunFor(std::chrono::milliseconds(200));

  const auto samples = subscriber.GetPubSamples();
  const auto it = std::find_if(samples.begin(), samples.end(),
                               [](const auto& sample) { return sample.topic().tname() == "/ref/topic"; });
  ASSERT_NE(it, samples.end());

  // The descriptor travels as a reference only; the inline byte field is never populated.
  EXPECT_TRUE(it->topic().tdatatype().desc().empty());
  EXPECT_FALSE(it->topic().tdatatype().desc_hash().empty());

  const std::string resolved = subscriber.ResolveTopicDescriptor(*it);
  EXPECT_FALSE(resolved.empty());
  EXPECT_EQ(resolved, utils::GetProtoMessageDescription(test::Test{}.GetDescriptor()));

  std::filesystem::remove_all(descriptor_dir, ec);
}

// test that loopback works
TEST(DiscoveryTests, Loopback) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(std::string(test_config_loopback))));
  ASSERT_TRUE(discovery.GetConfig().loopback_enabled);
  ev.RunFor(std::chrono::milliseconds(200));
  {
    auto samples = discovery.GetProcessSamples();
    ASSERT_EQ(samples.size(), 1);
    ASSERT_EQ(samples[0].process().uname(), "test_node");
  }
}

TEST(ProcessIdentityTests, SharesThisProcessAcceptsOurOwnSample) {
  DescriptorStore store{""};
  const Sample sample = utils::CreateProtoPubSubSample(store, "/dummy/topic", "", "test.Test", /* publisher = */ true,
                                                       "memfile", /* buffer_count = */ 2u);
  EXPECT_TRUE(utils::SharesThisProcess(sample));
}

TEST(ProcessIdentityTests, SharesThisProcessAcceptsSubscriberSamples) {
  // Either end may need to compare the other's address space against its own, so a sample built with
  // publisher = false carries the layer too.
  DescriptorStore store{""};
  const Sample sample = utils::CreateProtoPubSubSample(store, "/dummy/topic", "", "test.Test", /* publisher = */ false,
                                                       "", /* buffer_count = */ 0u);
  EXPECT_TRUE(utils::SharesThisProcess(sample));
}

TEST(ProcessIdentityTests, SharesThisProcessRejectsADifferentPid) {
  // Every other process on this host reports a different pid. Matching on the layer alone would put all of them on the
  // in-process bus.
  DescriptorStore store{""};
  Sample sample = utils::CreateProtoPubSubSample(store, "/dummy/topic", "", "test.Test", /* publisher = */ true,
                                                 "memfile", /* buffer_count = */ 2u);
  ASSERT_EQ(sample.topic().pid(), ::getpid());
  sample.mutable_topic()->set_pid(::getpid() + 1);
  EXPECT_FALSE(utils::SharesThisProcess(sample));
}

TEST(ProcessIdentityTests, SharesThisProcessRejectsADifferentHostname) {
  // Discovery reaches other hosts, where a pid equal to ours says nothing about address spaces.
  DescriptorStore store{""};
  Sample sample = utils::CreateProtoPubSubSample(store, "/dummy/topic", "", "test.Test", /* publisher = */ true,
                                                 "memfile", /* buffer_count = */ 2u);
  ASSERT_EQ(sample.topic().hname(), utils::GetHostname());
  sample.mutable_topic()->set_hname(sample.topic().hname() + "_elsewhere");
  EXPECT_FALSE(utils::SharesThisProcess(sample));
}

TEST(ProcessIdentityTests, SharesThisProcessRejectsASampleWithoutTheInProcessLayer) {
  // A peer that cannot use the layer never advertises it. Treating a missing layer as a match would make every such
  // peer look local, including one that only ever offered shared memory.
  DescriptorStore store{""};
  Sample sample = utils::CreateProtoPubSubSample(store, "/dummy/topic", "", "test.Test", /* publisher = */ true,
                                                 "memfile", /* buffer_count = */ 2u);
  sample.mutable_topic()->clear_tlayer();
  EXPECT_FALSE(utils::SharesThisProcess(sample));
}

TEST(DiscoveryTests, PollIntervalDefaultsToAsyncReceive) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(std::string(test_config))));
  EXPECT_EQ(discovery.GetConfig().poll_interval_ms, 0u);
}

TEST(DiscoveryTests, PollIntervalReadFromConfig) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("poll_node", ev, trellis::core::Config(YAML::Load(MakeConfig(45680, 20))));
  EXPECT_EQ(discovery.GetConfig().poll_interval_ms, 20u);
}

// A poll interval that lets a live peer's sample go stale between two drains would purge that peer, so it is rejected
// rather than clamped. With interval 10 and timeout 200 the guard admits poll intervals up to 180.
TEST(DiscoveryTests, PollIntervalTooLargeForSampleTimeoutThrows) {
  auto ev = trellis::core::EventLoop();
  EXPECT_THROW(Discovery("poll_node", ev, trellis::core::Config(YAML::Load(MakeConfig(45681, 181)))),
               std::invalid_argument);
  EXPECT_NO_THROW(Discovery("poll_node", ev, trellis::core::Config(YAML::Load(MakeConfig(45681, 180)))));
}

// Nothing can arrive between two drains unless the async receive is still armed, so a long poll interval must hold
// back the first sample until the timer fires.
TEST(DiscoveryTests, PollingDefersReceiveUntilDrain) {
  auto ev = trellis::core::EventLoop();
  Discovery sender("sender_node", ev, trellis::core::Config(YAML::Load(MakeConfig(45686, 0))));
  Discovery polling("poll_node", ev, trellis::core::Config(YAML::Load(MakeConfig(45686, 150))));

  sender.RegisterPublisher<test::Test>("/deferred/publisher", "memfile", 2u);

  ev.RunFor(std::chrono::milliseconds(60));
  EXPECT_FALSE(HasTopic(polling.GetPubSamples(), "/deferred/publisher"));

  ev.RunFor(std::chrono::milliseconds(150));
  EXPECT_TRUE(HasTopic(polling.GetPubSamples(), "/deferred/publisher"));
}

TEST(DiscoveryTests, PollingNodeAndAsyncNodeDiscoverEachOther) {
  auto ev = trellis::core::EventLoop();
  Discovery polling("poll_node", ev, trellis::core::Config(YAML::Load(MakeConfig(45682, 20))));
  Discovery async("async_node", ev, trellis::core::Config(YAML::Load(MakeConfig(45682, 0))));

  polling.RegisterPublisher<test::Test>("/poll/publisher", "memfile", 2u);
  async.RegisterSubscriber<test::Test>("/async/subscriber");

  unsigned polling_sub_count{0};
  unsigned async_pub_count{0};
  polling.AsyncReceiveSubscribers([&](Discovery::EventType event, const Sample& sample) {
    ASSERT_EQ(event, Discovery::EventType::kNewRegistration);
    if (sample.topic().tname() == "/async/subscriber") ++polling_sub_count;
  });
  async.AsyncReceivePublishers([&](Discovery::EventType event, const Sample& sample) {
    ASSERT_EQ(event, Discovery::EventType::kNewRegistration);
    if (sample.topic().tname() == "/poll/publisher") ++async_pub_count;
  });

  ev.RunFor(std::chrono::milliseconds(200));

  EXPECT_TRUE(HasTopic(polling.GetPubSubSamples(), "/async/subscriber"));
  EXPECT_TRUE(HasTopic(async.GetPubSubSamples(), "/poll/publisher"));
  EXPECT_NE(polling_sub_count, 0);
  EXPECT_NE(async_pub_count, 0);
}

TEST(DiscoveryTests, TwoPollingNodesDiscoverEachOther) {
  auto ev = trellis::core::EventLoop();
  Discovery polling1("poll_node1", ev, trellis::core::Config(YAML::Load(MakeConfig(45683, 20))));
  Discovery polling2("poll_node2", ev, trellis::core::Config(YAML::Load(MakeConfig(45683, 20))));

  polling1.RegisterPublisher<test::Test>("/poll1/publisher", "memfile", 2u);
  polling2.RegisterSubscriber<test::Test>("/poll2/subscriber");

  unsigned sub_count{0};
  unsigned pub_count{0};
  polling1.AsyncReceiveSubscribers([&](Discovery::EventType, const Sample& sample) {
    if (sample.topic().tname() == "/poll2/subscriber") ++sub_count;
  });
  polling2.AsyncReceivePublishers([&](Discovery::EventType, const Sample& sample) {
    if (sample.topic().tname() == "/poll1/publisher") ++pub_count;
  });

  ev.RunFor(std::chrono::milliseconds(200));

  EXPECT_TRUE(HasTopic(polling1.GetPubSubSamples(), "/poll2/subscriber"));
  EXPECT_TRUE(HasTopic(polling2.GetPubSubSamples(), "/poll1/publisher"));
  EXPECT_NE(sub_count, 0);
  EXPECT_NE(pub_count, 0);
}

// Every broadcast interval puts a whole burst of datagrams in the socket, so one drain has to take all of them.
TEST(DiscoveryTests, PollingReceivesManySamplesPerTick) {
  static constexpr unsigned kPublisherCount = 50;
  auto ev = trellis::core::EventLoop();
  Discovery sender("sender_node", ev, trellis::core::Config(YAML::Load(MakeConfig(45684, 0))));
  Discovery polling("poll_node", ev, trellis::core::Config(YAML::Load(MakeConfig(45684, 20))));

  for (unsigned i = 0; i < kPublisherCount; ++i) {
    sender.RegisterPublisher<test::Test>("/many/publisher/" + std::to_string(i), "memfile", 2u);
  }

  ev.RunFor(std::chrono::milliseconds(300));

  const auto samples = polling.GetPubSamples();
  EXPECT_EQ(samples.size(), kPublisherCount);
  for (unsigned i = 0; i < kPublisherCount; ++i) {
    EXPECT_TRUE(HasTopic(samples, "/many/publisher/" + std::to_string(i))) << "missing publisher " << i;
  }
}

TEST(DiscoveryTests, UnregisterPublisherWhilePolling) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("poll_node", ev, trellis::core::Config(YAML::Load(MakeConfig(45685, 20))));

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

  auto handle = discovery.RegisterPublisher<test::Test>("/to_be_removed", "mem", 1u);
  ev.RunFor(std::chrono::milliseconds(50));
  discovery.Unregister(handle);

  ev.RunFor(std::chrono::milliseconds(400));  // wait longer than timeout
  EXPECT_TRUE(discovery.GetPubSubSamples().empty());
  EXPECT_NE(reg_count, 0);
  EXPECT_NE(unreg_count, 0);
}

}  // namespace trellis::core::discovery
