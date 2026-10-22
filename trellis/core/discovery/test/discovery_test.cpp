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
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "trellis/core/discovery/utils.hpp"
#include "trellis/core/test/test.pb.h"

namespace trellis::core::discovery {

namespace {
// Concurrent runs of this test share the host network, so each run picks unused ports rather than fixed ones. Tests
// asking for the same slot share a port; different slots never do.
uint16_t Port(unsigned slot) {
  static std::map<unsigned, uint16_t> ports;
  if (const auto it = ports.find(slot); it != ports.end()) {
    return it->second;
  }
  while (true) {
    asio::io_context io;
    const asio::ip::udp::socket probe{io, asio::ip::udp::endpoint{asio::ip::udp::v4(), 0}};
    const uint16_t port = probe.local_endpoint().port();
    if (std::none_of(ports.begin(), ports.end(), [port](const auto& entry) { return entry.second == port; })) {
      return ports.emplace(slot, port).first->second;
    }
  }
}

std::string TestConfig(unsigned slot = 0) {
  return "trellis:\n"
         "  discovery:\n"
         "    interval: 10\n"
         "    sample_timeout: 200\n"
         "    port: " +
         std::to_string(Port(slot)) + "\n";
}

std::string LoopbackConfig() { return TestConfig() + "    loopback_enabled: true\n"; }

// The heartbeat here outlasts any test, so a callback that fires within a short RunFor was delivered by the
// immediate path.
std::string LoopbackNoHeartbeatConfig() {
  return "trellis:\n"
         "  discovery:\n"
         "    interval: 100000\n"
         "    sample_timeout: 200000\n"
         "    loopback_enabled: true\n"
         "    port: " +
         std::to_string(Port(9)) + "\n";
}

// Poll tests use their own ports so a slow drain in one of them cannot show up as a stray sample in another.
std::string MakeConfig(unsigned slot, unsigned poll_interval_ms) {
  return "trellis:\n"
         "  discovery:\n"
         "    interval: 10\n"
         "    sample_timeout: 200\n"
         "    port: " +
         std::to_string(Port(slot)) + "\n    poll_interval_ms: " + std::to_string(poll_interval_ms) + "\n";
}

bool HasTopic(const std::vector<Sample>& samples, const std::string& topic) {
  return std::any_of(samples.begin(), samples.end(),
                     [&topic](const Sample& sample) { return sample.topic().tname() == topic; });
}

bool HasProcess(const std::vector<Sample>& samples, const std::string& uname) {
  return std::any_of(samples.begin(), samples.end(),
                     [&uname](const Sample& sample) { return sample.process().uname() == uname; });
}

Sample FakeProcessSample(const std::string& id, const std::string& uname) {
  Sample sample;
  sample.set_id(id);
  sample.set_type(discovery::process_registration);
  sample.mutable_process()->set_uname(uname);
  return sample;
}

void AppendLittleEndian(std::string& out, uint32_t value, size_t bytes) {
  for (size_t i = 0; i < bytes; ++i) {
    out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
  }
}

// One discovery datagram, built byte by byte as on the wire: the 24-byte header, the id length, the NUL-terminated
// id, and the payload. `len` is the header's len field, which a well-formed datagram sets to its size minus 24.
std::string Datagram(const std::string& id, const std::string& payload, int32_t len, uint32_t packet_index = 0,
                     uint32_t total_packets = 1, uint32_t total_sample_size = 0) {
  std::string out = "TRLS";
  AppendLittleEndian(out, 1, 4);
  AppendLittleEndian(out, static_cast<uint32_t>(len), 4);
  AppendLittleEndian(out, packet_index, 4);
  AppendLittleEndian(out, total_packets, 4);
  AppendLittleEndian(out, total_sample_size, 4);
  AppendLittleEndian(out, id.size() + 1, 2);
  out += id;
  out.push_back('\0');
  out += payload;
  return out;
}

// The len a well-formed datagram carrying `id` and `payload` declares: the id length field, the id with its NUL, and
// the payload.
int32_t WellFormedLen(const std::string& id, const std::string& payload) {
  return static_cast<int32_t>(sizeof(uint16_t) + id.size() + 1 + payload.size());
}

std::string WellFormedDatagram(const Sample& sample) {
  const std::string payload = sample.SerializeAsString();
  return Datagram(sample.id(), payload, WellFormedLen(sample.id(), payload), 0, 1, payload.size());
}

void SendToDiscoveryPort(uint16_t port, const std::string& datagram) {
  asio::io_context io;
  asio::ip::udp::socket socket{io, asio::ip::udp::v4()};
  socket.set_option(asio::socket_base::broadcast(true));
  socket.send_to(asio::buffer(datagram), asio::ip::udp::endpoint{asio::ip::make_address("127.255.255.255"), port});
}
}  // namespace

TEST(DiscoveryTests, IniitalConditions) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(TestConfig())));
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
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(TestConfig())));
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
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(LoopbackNoHeartbeatConfig())));
  discovery.RegisterPublisher<test::Test>("/dummy/publisher", "memfile", 2u);
  // Deliver the registration while no callback exists, so only the replay on adding one can report it.
  ev.RunFor(std::chrono::milliseconds(10));
  unsigned receive_count{0};
  discovery.AsyncReceivePublishers([&](Discovery::EventType event, const Sample& sample) {
    ++receive_count;
    EXPECT_EQ(event, Discovery::EventType::kNewRegistration);
    EXPECT_EQ(sample.topic().tname(), "/dummy/publisher");
  });
  ev.RunFor(std::chrono::milliseconds(10));
  EXPECT_EQ(receive_count, 1u);
}

// Registered before the callback was added, but its delivery had not run yet. That delivery and the replay both
// reported it until the replay read only what had been received.
TEST(DiscoveryTests, LoopbackReportsAQueuedSampleOnce) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(LoopbackNoHeartbeatConfig())));
  discovery.RegisterPublisher<test::Test>("/dummy/publisher", "memfile", 2u);
  unsigned receive_count{0};
  discovery.AsyncReceivePublishers([&receive_count](Discovery::EventType, const Sample&) { ++receive_count; });
  ev.RunFor(std::chrono::milliseconds(10));
  EXPECT_EQ(receive_count, 1u);
}

// A publisher registered after the callback reaches it when it registers, not on the next heartbeat.
TEST(DiscoveryTests, LoopbackDiscoversPublisherRegisteredAfterCallback) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(LoopbackNoHeartbeatConfig())));
  unsigned receive_count{0};
  discovery.AsyncReceivePublishers([&](Discovery::EventType event, const Sample& sample) {
    ++receive_count;
    EXPECT_EQ(event, Discovery::EventType::kNewRegistration);
    EXPECT_EQ(sample.topic().tname(), "/dummy/publisher");
  });
  discovery.RegisterPublisher<test::Test>("/dummy/publisher", "memfile", 2u);
  ev.RunFor(std::chrono::milliseconds(10));
  EXPECT_EQ(receive_count, 1u);
}

// Unregistered before its loopback delivery ran: the delivery used to register it anyway, and it stayed until its
// sample timed out, since Unregister sends no unregistration. The one still registered shows the delivery ran.
TEST(DiscoveryTests, LoopbackDropsADeliveryForASampleUnregisteredBeforeItRan) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(LoopbackNoHeartbeatConfig())));
  // Padded to span several packets; the callback records each topic without its padding, so a failure stays legible.
  const std::string padding(80000, 'x');
  std::vector<std::string> received;
  discovery.AsyncReceivePublishers([&received, &padding](Discovery::EventType, const Sample& sample) {
    received.push_back(sample.topic().tname().substr(0, sample.topic().tname().size() - padding.size()));
  });
  const auto handle = discovery.RegisterPublisher<test::Test>("/dummy/unregistered/" + padding, "memfile", 2u);
  discovery.RegisterPublisher<test::Test>("/dummy/kept/" + padding, "memfile", 2u);
  discovery.Unregister(handle);
  ev.RunFor(std::chrono::milliseconds(10));
  EXPECT_EQ(received, std::vector<std::string>{"/dummy/kept/"});
  EXPECT_FALSE(HasTopic(discovery.GetPubSamples(), "/dummy/unregistered/" + padding));
  EXPECT_TRUE(HasTopic(discovery.GetPubSamples(), "/dummy/kept/" + padding));
}

// Adding a callback replays what has been received to that callback alone. It used to rebroadcast every sample to
// every callback, so the first of M callbacks heard each of N publishers M times, and the total grew as N * M^2.
TEST(DiscoveryTests, LoopbackReplaysReceivedSamplesOnlyToTheNewCallback) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(LoopbackNoHeartbeatConfig())));
  constexpr unsigned kPublishers = 5;
  constexpr unsigned kCallbacks = 4;
  for (unsigned i = 0; i < kPublishers; ++i) {
    discovery.RegisterPublisher<test::Test>("/dummy/replayed_" + std::to_string(i), "memfile", 2u);
  }
  discovery.RegisterSubscriber<test::Test>("/dummy/not_a_publisher");
  ev.RunFor(std::chrono::milliseconds(10));  // delivered while no callback exists

  std::vector<std::string> publishers;
  for (unsigned i = 0; i < kPublishers; ++i) {
    publishers.push_back("/dummy/replayed_" + std::to_string(i));
  }
  std::vector<std::vector<std::string>> received(kCallbacks);
  for (unsigned i = 0; i < kCallbacks; ++i) {
    discovery.AsyncReceivePublishers([&received, i](Discovery::EventType event, const Sample& sample) {
      EXPECT_EQ(event, Discovery::EventType::kNewRegistration);
      received[i].push_back(sample.topic().tname());
    });
    ev.RunFor(std::chrono::milliseconds(10));
  }
  for (auto& topics : received) {
    std::ranges::sort(topics);
    EXPECT_EQ(topics, publishers);
  }
}

// Unregistered before its delivery ran, a sample never reaches the caches the replay reads, so a callback added
// meanwhile does not hear of it. The one still registered shows the replay ran.
TEST(DiscoveryTests, LoopbackReplaySkipsASampleUnregisteredBeforeItsDeliveryRan) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(LoopbackNoHeartbeatConfig())));
  discovery.RegisterPublisher<test::Test>("/dummy/kept", "memfile", 2u);
  ev.RunFor(std::chrono::milliseconds(10));  // delivered while no callback exists
  const auto handle = discovery.RegisterPublisher<test::Test>("/dummy/unregistered", "memfile", 2u);
  std::vector<std::string> received;
  discovery.AsyncReceivePublishers(
      [&received](Discovery::EventType, const Sample& sample) { received.push_back(sample.topic().tname()); });
  discovery.Unregister(handle);
  ev.RunFor(std::chrono::milliseconds(10));
  EXPECT_EQ(received, std::vector<std::string>{"/dummy/kept"});
}

// Unregistered after its delivery ran, a sample stays known until it times out, since Unregister sends no
// unregistration. A callback added meanwhile is told what the callbacks before it were told.
TEST(DiscoveryTests, LoopbackReplayTellsANewCallbackWhatTheExistingOnesHeard) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(LoopbackNoHeartbeatConfig())));
  std::vector<std::string> existing;
  discovery.AsyncReceivePublishers(
      [&existing](Discovery::EventType, const Sample& sample) { existing.push_back(sample.topic().tname()); });
  const auto handle = discovery.RegisterPublisher<test::Test>("/dummy/unregistered", "memfile", 2u);
  ev.RunFor(std::chrono::milliseconds(10));
  discovery.Unregister(handle);
  std::vector<std::string> added;
  discovery.AsyncReceivePublishers(
      [&added](Discovery::EventType, const Sample& sample) { added.push_back(sample.topic().tname()); });
  ev.RunFor(std::chrono::milliseconds(10));
  EXPECT_EQ(existing, std::vector<std::string>{"/dummy/unregistered"});
  EXPECT_EQ(added, existing);
}

// The other half of the replay above: when that sample times out, the new callback hears it go, as the earlier one
// does, so neither is left believing in it.
TEST(DiscoveryTests, LoopbackTimesOutAReplayedSampleForEveryCallback) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(LoopbackConfig())));
  using Events = std::vector<Discovery::EventType>;
  Events existing;
  discovery.AsyncReceivePublishers(
      [&existing](Discovery::EventType event, const Sample&) { existing.push_back(event); });
  const auto handle = discovery.RegisterPublisher<test::Test>("/dummy/unregistered", "memfile", 2u);
  ev.RunFor(std::chrono::milliseconds(30));  // heartbeats deliver it again while it is registered
  discovery.Unregister(handle);
  Events added;
  discovery.AsyncReceivePublishers([&added](Discovery::EventType event, const Sample&) { added.push_back(event); });
  ev.RunFor(std::chrono::milliseconds(20));
  ASSERT_EQ(added, Events{Discovery::EventType::kNewRegistration});  // the replay
  ev.RunFor(std::chrono::milliseconds(400));                         // past the 200 ms sample timeout

  EXPECT_EQ(added, (Events{Discovery::EventType::kNewRegistration, Discovery::EventType::kNewUnregistration}));
  ASSERT_FALSE(existing.empty());
  EXPECT_EQ(existing.back(), Discovery::EventType::kNewUnregistration);
  EXPECT_EQ(std::ranges::count(existing, Discovery::EventType::kNewUnregistration), 1);
  EXPECT_FALSE(HasTopic(discovery.GetPubSamples(), "/dummy/unregistered"));
}

// Stopped before its replay ran, a callback hears nothing. The one left running shows the replay ran.
TEST(DiscoveryTests, LoopbackReplaySkipsACallbackStoppedBeforeItRan) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(LoopbackNoHeartbeatConfig())));
  discovery.RegisterPublisher<test::Test>("/dummy/publisher", "memfile", 2u);
  ev.RunFor(std::chrono::milliseconds(10));  // delivered while no callback exists
  unsigned stopped_count{0};
  unsigned kept_count{0};
  const auto stopped =
      discovery.AsyncReceivePublishers([&stopped_count](Discovery::EventType, const Sample&) { ++stopped_count; });
  discovery.AsyncReceivePublishers([&kept_count](Discovery::EventType, const Sample&) { ++kept_count; });
  discovery.StopReceive(stopped);
  ev.RunFor(std::chrono::milliseconds(10));
  EXPECT_EQ(stopped_count, 0u);
  EXPECT_EQ(kept_count, 1u);
}

// Each kind of callback is replayed the samples of its own kind and no others.
TEST(DiscoveryTests, LoopbackReplaysEachKindToItsOwnCallbacks) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(LoopbackNoHeartbeatConfig())));
  discovery.RegisterPublisher<test::Test>("/dummy/publisher", "memfile", 2u);
  discovery.RegisterSubscriber<test::Test>("/dummy/subscriber");
  discovery.RegisterServiceServer("dummy.Service", 1234, {});
  ev.RunFor(std::chrono::milliseconds(10));  // delivered while no callback exists
  std::vector<std::string> publishers;
  std::vector<std::string> subscribers;
  std::vector<std::string> services;
  discovery.AsyncReceivePublishers(
      [&publishers](Discovery::EventType, const Sample& sample) { publishers.push_back(sample.topic().tname()); });
  discovery.AsyncReceiveSubscribers(
      [&subscribers](Discovery::EventType, const Sample& sample) { subscribers.push_back(sample.topic().tname()); });
  discovery.AsyncReceiveServices(
      [&services](Discovery::EventType, const Sample& sample) { services.push_back(sample.service().sname()); });
  ev.RunFor(std::chrono::milliseconds(10));
  EXPECT_EQ(publishers, std::vector<std::string>{"/dummy/publisher"});
  EXPECT_EQ(subscribers, std::vector<std::string>{"/dummy/subscriber"});
  EXPECT_EQ(services, std::vector<std::string>{"dummy.Service"});
}

TEST(DiscoveryTests, RegisterSubscriber) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(TestConfig())));
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
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(TestConfig())));

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
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(TestConfig())));
  const auto handle = discovery.RegisterPublisher<test::Test>("/stable/id", "mem", 1u);

  const std::string id1 = discovery.GetSampleId(handle);
  const std::string id2 = discovery.GetSampleId(handle);

  ASSERT_FALSE(id1.empty());
  ASSERT_EQ(id1, id2);  // ID must be stable
}

TEST(DiscoveryTests, RegisterServiceServer) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(TestConfig())));
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
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(TestConfig())));

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
  Discovery discovery1("test_node1", ev, trellis::core::Config(YAML::Load(TestConfig())));
  Discovery discovery2("test_node2", ev, trellis::core::Config(YAML::Load(TestConfig())));
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
  Discovery discovery1("test_node1", ev, trellis::core::Config(YAML::Load(TestConfig())));
  Discovery discovery2("test_node2", ev, trellis::core::Config(YAML::Load(TestConfig(1))));
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
      "    port: " +
      std::to_string(Port(0)) +
      "\n"
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
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(LoopbackConfig())));
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

// Mutable access to a sample's in-process layer, which CreateProtoPubSubSample always adds.
discovery::TransportLayer& InProcessLayer(Sample& sample) {
  for (auto& layer : *sample.mutable_topic()->mutable_tlayer()) {
    if (layer.type() == discovery::tl_inproc) {
      return layer;
    }
  }
  throw std::logic_error{"no in-process layer"};
}

TEST(ProcessIdentityTests, SharesThisProcessRejectsAnotherProcessWithOurPidAndHostname) {
  // Containers with their own PID namespaces can share a pid and, under --network host, a hostname. Only the token
  // tells them apart.
  DescriptorStore store{""};
  Sample sample = utils::CreateProtoPubSubSample(store, "/dummy/topic", "", "test.Test", /* publisher = */ true,
                                                 "memfile", /* buffer_count = */ 2u);
  std::string& token = *InProcessLayer(sample).mutable_par_layer()->mutable_layer_par_inproc()->mutable_process_token();
  ASSERT_FALSE(token.empty());
  token[0] = static_cast<char>(token[0] ^ 1);
  EXPECT_FALSE(utils::SharesThisProcess(sample));
}

TEST(ProcessIdentityTests, SharesThisProcessRejectsALayerWithoutAToken) {
  // Older builds advertise the layer with no token. None of them can be in this address space, which runs this build.
  DescriptorStore store{""};
  Sample sample = utils::CreateProtoPubSubSample(store, "/dummy/topic", "", "test.Test", /* publisher = */ true,
                                                 "memfile", /* buffer_count = */ 2u);
  InProcessLayer(sample).clear_par_layer();
  EXPECT_FALSE(utils::SharesThisProcess(sample));
}

TEST(DiscoveryTests, PollIntervalDefaultsToAsyncReceive) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(TestConfig())));
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

// An id of 127 bytes, with its NUL, puts 0x80 in the id length field's low byte, which a signed char sign-extended.
TEST(DiscoveryTests, AcceptsASampleIdOf127BytesOrMore) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(TestConfig(20))));
  SendToDiscoveryPort(Port(20), WellFormedDatagram(FakeProcessSample(std::string(127, 'x'), "long_id_node")));
  ev.RunFor(std::chrono::milliseconds(50));
  EXPECT_TRUE(HasProcess(discovery.GetProcessSamples(), "long_id_node"));
}

// Every byte after the header has to be accounted for by len, and len must not claim more than the datagram holds.
// A well-formed datagram sent alongside shows the receiver was still taking samples.
TEST(DiscoveryTests, DropsDatagramsWhoseLenDisagreesWithTheirSize) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(TestConfig(21))));
  const uint16_t port = Port(21);

  // Bytes past what len covers. The sample within len parses, so without the check this one would be taken.
  SendToDiscoveryPort(port, WellFormedDatagram(FakeProcessSample("trailing", "trailing_node")) + "junk");

  // A len past the end of the datagram. Without the check the receiver reads beyond it, which shows up under a
  // sanitizer; here the sample just fails to parse, so this case only pins that it is dropped.
  const Sample long_len = FakeProcessSample("long_len", "long_len_node");
  const std::string long_len_payload = long_len.SerializeAsString();
  SendToDiscoveryPort(port,
                      Datagram(long_len.id(), long_len_payload, WellFormedLen(long_len.id(), long_len_payload) + 100));

  // A len too small to cover even the id made the payload size wrap around to nearly 2^64, and appending that much to
  // a multi-packet sample threw out of the event loop.
  SendToDiscoveryPort(port, Datagram("wrapped", "payload", 0, 0, 2, 100));
  SendToDiscoveryPort(port, Datagram("negative", "payload", -1));

  SendToDiscoveryPort(port, WellFormedDatagram(FakeProcessSample("valid", "valid_node")));
  EXPECT_NO_THROW(ev.RunFor(std::chrono::milliseconds(50)));
  const auto samples = discovery.GetProcessSamples();
  EXPECT_TRUE(HasProcess(samples, "valid_node"));
  EXPECT_FALSE(HasProcess(samples, "trailing_node"));
  EXPECT_FALSE(HasProcess(samples, "long_len_node"));
}

// The receiver read any datagram on its port as a sample, whatever its magic and version.
TEST(DiscoveryTests, DropsDatagramsWithAnotherMagicOrVersion) {
  auto ev = trellis::core::EventLoop();
  Discovery discovery("test_node", ev, trellis::core::Config(YAML::Load(TestConfig(22))));
  const uint16_t port = Port(22);

  std::string other_magic = WellFormedDatagram(FakeProcessSample("other_magic", "other_magic_node"));
  other_magic.replace(0, 4, "XXXX");
  SendToDiscoveryPort(port, other_magic);
  std::string other_version = WellFormedDatagram(FakeProcessSample("other_version", "other_version_node"));
  other_version[4] = 2;  // the version's low byte, little-endian
  SendToDiscoveryPort(port, other_version);

  SendToDiscoveryPort(port, WellFormedDatagram(FakeProcessSample("valid", "valid_node")));
  EXPECT_NO_THROW(ev.RunFor(std::chrono::milliseconds(50)));
  const auto samples = discovery.GetProcessSamples();
  EXPECT_TRUE(HasProcess(samples, "valid_node"));
  EXPECT_FALSE(HasProcess(samples, "other_magic_node"));
  EXPECT_FALSE(HasProcess(samples, "other_version_node"));
}

}  // namespace trellis::core::discovery
