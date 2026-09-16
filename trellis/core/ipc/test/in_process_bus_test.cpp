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

#include <memory>
#include <string>

namespace trellis::core::ipc {
namespace {

constexpr std::string_view kTopic = "/dummy/route_count_topic";

InProcessBus::ReceiveFn NoopReceiver() {
  return [](const shm::ShmFile::SMemFileHeader&, const void*, size_t) {};
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

}  // namespace
}  // namespace trellis::core::ipc
