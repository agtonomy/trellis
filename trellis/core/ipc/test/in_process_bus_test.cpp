/*
 * Copyright (C) 2026 Agtonomy
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

#include "trellis/core/ipc/in_process_bus.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace trellis::core::ipc {
namespace {

constexpr std::string_view kTopic = "/dummy/route_count_topic";

InProcessBus::ReceiveFn NoopReceiver() {
  return [](const shm::ShmFile::SMemFileHeader&, BorrowedPayload) {};
}

TEST(InProcessBusRouteCount, TracksSubscribeAndUnsubscribe) {
  const std::shared_ptr<InProcessBus> bus = InProcessBus::Instance();
  const std::string topic{kTopic};
  const InProcessBus::RouteCount count = bus->GetRouteCount(topic);
  ASSERT_EQ(count->load(), 0u);

  const trellis::core::EventLoop loop;
  const InProcessBus::Handle first = bus->Subscribe(topic, loop, NoopReceiver());
  EXPECT_EQ(count->load(), 1u);

  const InProcessBus::Handle second = bus->Subscribe(topic, loop, NoopReceiver());
  EXPECT_EQ(count->load(), 2u);

  bus->Unsubscribe(topic, first);
  EXPECT_EQ(count->load(), 1u);

  bus->Unsubscribe(topic, second);
  EXPECT_EQ(count->load(), 0u);
}

TEST(InProcessBusRouteCount, SurvivesTheTopicGoingEmpty) {
  // The trap in dropping an empty topic entry: a publisher holds its counter for its whole life, so erasing the entry
  // would hand the next subscriber a fresh counter and leave the publisher reading one that never moves again -- it
  // would stop publishing to the bus permanently, for a subscriber sitting right there in the same process.
  const std::shared_ptr<InProcessBus> bus = InProcessBus::Instance();
  const std::string topic{kTopic};
  const InProcessBus::RouteCount count = bus->GetRouteCount(topic);

  const trellis::core::EventLoop loop;
  const InProcessBus::Handle first = bus->Subscribe(topic, loop, NoopReceiver());
  ASSERT_EQ(count->load(), 1u);
  bus->Unsubscribe(topic, first);
  ASSERT_EQ(count->load(), 0u);

  const InProcessBus::Handle second = bus->Subscribe(topic, loop, NoopReceiver());
  EXPECT_EQ(count->load(), 1u) << "counter went stale once the topic emptied";
  bus->Unsubscribe(topic, second);
}

TEST(InProcessBusRouteCount, IsIndependentPerTopic) {
  const std::shared_ptr<InProcessBus> bus = InProcessBus::Instance();
  const InProcessBus::RouteCount a = bus->GetRouteCount("/dummy/topic_a");
  const InProcessBus::RouteCount b = bus->GetRouteCount("/dummy/topic_b");

  const trellis::core::EventLoop loop;
  const InProcessBus::Handle handle = bus->Subscribe("/dummy/topic_a", loop, NoopReceiver());
  EXPECT_EQ(a->load(), 1u);
  EXPECT_EQ(b->load(), 0u);
  bus->Unsubscribe("/dummy/topic_a", handle);
}

TEST(InProcessBusRouteCount, ObservingATopicDoesNotCreateARoute) {
  // GetRouteCount inserts the topic entry so the counter has somewhere to live. That must not look like a subscriber,
  // or every publisher would serialize a bus payload for nobody.
  const std::shared_ptr<InProcessBus> bus = InProcessBus::Instance();
  const InProcessBus::RouteCount count = bus->GetRouteCount("/dummy/observed_only");
  EXPECT_EQ(count->load(), 0u);
}

shm::ShmFile::SMemFileHeader Header(uint64_t writer_id, uint64_t sequence) {
  shm::ShmFile::SMemFileHeader header{};
  header.writer_id = writer_id;
  header.sequence = sequence;
  return header;
}

std::shared_ptr<const std::vector<uint8_t>> Payload() { return std::make_shared<const std::vector<uint8_t>>(1, 0); }

// A loop that isn't running used to collect one handler per message and get the whole backlog late. It now gets the
// newest `depth` per writer, from one drain: the most a shared memory reader could still find.
TEST(InProcessBusBackpressure, HoldsTheNewestDepthForALoopThatIsNotRunning) {
  const auto bus = InProcessBus::Instance();
  trellis::core::EventLoop loop;
  std::vector<uint64_t> received;
  const auto handle = bus->Subscribe("/dummy/bounded_topic", loop,
                                     [&received](const shm::ShmFile::SMemFileHeader& header, BorrowedPayload) {
                                       received.push_back(header.sequence);
                                     });
  for (uint64_t sequence = 1; sequence <= 10; ++sequence) {
    bus->Publish("/dummy/bounded_topic", {Header(1, sequence), Payload()}, /* depth = */ 3);
  }
  EXPECT_EQ((*loop).poll(), 1u) << "one drain, not a handler per message";
  EXPECT_EQ(received, (std::vector<uint64_t>{8, 9, 10}));
  bus->Unsubscribe("/dummy/bounded_topic", handle);
}

TEST(InProcessBusBackpressure, BoundsEachWriterSeparatelyAndKeepsTheirOrder) {
  const auto bus = InProcessBus::Instance();
  trellis::core::EventLoop loop;
  std::vector<std::pair<uint64_t, uint64_t>> received;  // writer id, sequence
  const auto handle = bus->Subscribe("/dummy/two_writer_topic", loop,
                                     [&received](const shm::ShmFile::SMemFileHeader& header, BorrowedPayload) {
                                       received.emplace_back(header.writer_id, header.sequence);
                                     });
  for (uint64_t sequence = 1; sequence <= 5; ++sequence) {
    bus->Publish("/dummy/two_writer_topic", {Header(1, sequence), Payload()}, /* depth = */ 2);
    bus->Publish("/dummy/two_writer_topic", {Header(2, sequence), Payload()}, /* depth = */ 2);
  }
  (*loop).poll();
  EXPECT_EQ(received, (std::vector<std::pair<uint64_t, uint64_t>>{{1, 4}, {2, 4}, {1, 5}, {2, 5}}));
  bus->Unsubscribe("/dummy/two_writer_topic", handle);
}

// What arrives while a callback runs is held to `depth` too, so a slow callback catches up on recent messages.
TEST(InProcessBusBackpressure, ASlowCallbackGetsTheNewestOnceItCatchesUp) {
  const auto bus = InProcessBus::Instance();
  trellis::core::EventLoop loop;
  std::vector<uint64_t> received;
  std::promise<void> entered;
  std::promise<void> release;
  const auto handle = bus->Subscribe("/dummy/slow_topic", loop,
                                     [&received, &entered, released = release.get_future().share(), &loop](
                                         const shm::ShmFile::SMemFileHeader& header, BorrowedPayload) {
                                       received.push_back(header.sequence);
                                       if (header.sequence == 1) {
                                         entered.set_value();
                                         released.wait();
                                       } else if (header.sequence == 10) {
                                         (*loop).stop();
                                       }
                                     });
  bus->Publish("/dummy/slow_topic", {Header(1, 1), Payload()}, /* depth = */ 3);
  std::thread runner([&loop]() { loop.RunFor(std::chrono::seconds{5}); });
  entered.get_future().wait();
  for (uint64_t sequence = 2; sequence <= 10; ++sequence) {
    bus->Publish("/dummy/slow_topic", {Header(1, sequence), Payload()}, /* depth = */ 3);
  }
  release.set_value();
  runner.join();
  EXPECT_EQ(received, (std::vector<uint64_t>{1, 8, 9, 10}));
  bus->Unsubscribe("/dummy/slow_topic", handle);
}

// A delivery that throws must not leave the subscriber waiting on a drain that will never be posted, nor lose the
// messages queued behind it.
TEST(InProcessBusBackpressure, AThrowingDeliveryDoesNotStallTheQueue) {
  const auto bus = InProcessBus::Instance();
  trellis::core::EventLoop loop;
  std::vector<uint64_t> received;
  const auto handle = bus->Subscribe("/dummy/throwing_topic", loop,
                                     [&received](const shm::ShmFile::SMemFileHeader& header, BorrowedPayload) {
                                       if (header.sequence == 1) {
                                         throw std::runtime_error{"delivery failed"};
                                       }
                                       received.push_back(header.sequence);
                                     });
  bus->Publish("/dummy/throwing_topic", {Header(1, 1), Payload()}, /* depth = */ 3);
  bus->Publish("/dummy/throwing_topic", {Header(1, 2), Payload()}, /* depth = */ 3);
  EXPECT_THROW((*loop).poll(), std::runtime_error);
  bus->Publish("/dummy/throwing_topic", {Header(1, 3), Payload()}, /* depth = */ 3);
  (*loop).poll();
  EXPECT_EQ(received, (std::vector<uint64_t>{2, 3}));
  bus->Unsubscribe("/dummy/throwing_topic", handle);
}

// A message a callback publishes to its own topic is queued while the drain runs, and a drain is posted for it once
// the current one finishes, so it arrives after the one being delivered.
TEST(InProcessBusBackpressure, AMessagePublishedFromACallbackIsDeliveredNext) {
  const auto bus = InProcessBus::Instance();
  trellis::core::EventLoop loop;
  std::vector<uint64_t> received;
  const auto handle = bus->Subscribe("/dummy/republish_topic", loop,
                                     [&received, &bus](const shm::ShmFile::SMemFileHeader& header, BorrowedPayload) {
                                       received.push_back(header.sequence);
                                       if (header.sequence == 1) {
                                         bus->Publish("/dummy/republish_topic", {Header(1, 2), Payload()},
                                                      /* depth = */ 3);
                                       }
                                     });
  bus->Publish("/dummy/republish_topic", {Header(1, 1), Payload()}, /* depth = */ 3);
  EXPECT_EQ((*loop).poll(), 2u) << "a second drain, posted by the first";
  EXPECT_EQ(received, (std::vector<uint64_t>{1, 2}));
  bus->Unsubscribe("/dummy/republish_topic", handle);
}

// Each subscriber has its own queue, so one whose loop isn't running holds back nobody else on the topic.
TEST(InProcessBusBackpressure, ASubscriberThatIsBehindDoesNotHoldBackAnother) {
  const auto bus = InProcessBus::Instance();
  trellis::core::EventLoop stalled_loop;
  trellis::core::EventLoop running_loop;
  std::vector<uint64_t> stalled_received;
  std::vector<uint64_t> running_received;
  const auto stalled = bus->Subscribe("/dummy/two_subscriber_topic", stalled_loop,
                                      [&stalled_received](const shm::ShmFile::SMemFileHeader& header, BorrowedPayload) {
                                        stalled_received.push_back(header.sequence);
                                      });
  const auto running = bus->Subscribe("/dummy/two_subscriber_topic", running_loop,
                                      [&running_received](const shm::ShmFile::SMemFileHeader& header, BorrowedPayload) {
                                        running_received.push_back(header.sequence);
                                      });
  for (uint64_t sequence = 1; sequence <= 5; ++sequence) {
    bus->Publish("/dummy/two_subscriber_topic", {Header(1, sequence), Payload()}, /* depth = */ 3);
    (*running_loop).poll();
  }
  EXPECT_EQ(running_received, (std::vector<uint64_t>{1, 2, 3, 4, 5}));
  EXPECT_TRUE(stalled_received.empty());
  (*stalled_loop).poll();
  EXPECT_EQ(stalled_received, (std::vector<uint64_t>{3, 4, 5}));
  bus->Unsubscribe("/dummy/two_subscriber_topic", stalled);
  bus->Unsubscribe("/dummy/two_subscriber_topic", running);
}

}  // namespace
}  // namespace trellis::core::ipc
