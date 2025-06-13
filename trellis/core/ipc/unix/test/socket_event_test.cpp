/*
 * Copyright (C) 2025 Agtonomy
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

#include "trellis/core/ipc/unix/socket_event.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>

#include "trellis/core/event_loop.hpp"

namespace fs = std::filesystem;
using namespace trellis::core::ipc::unix;

class TrellisSocketEventTest : public ::testing::Test {
 protected:
  void SetUp() override {
    socket_path_ = "/tmp/test_socket_event.sock";
    fs::remove(socket_path_);
    loop_ = std::make_shared<trellis::core::EventLoop>();
  }

  void TearDown() override { fs::remove(socket_path_); }

  void PumpLoopUntil(std::function<bool()> condition, int max_ms = 200) {
    for (int i = 0; i < max_ms && !condition(); ++i) {
      loop_->PollOne();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  std::shared_ptr<trellis::core::EventLoop> loop_;
  std::string socket_path_;
};

TEST_F(TrellisSocketEventTest, SendAndReceiveSingleEvent) {
  std::atomic<bool> received{false};
  SocketEvent::Event received_event{};

  SocketEvent reader(*loop_, true, socket_path_);
  reader.AsyncReceive([&](SocketEvent::Event ev) {
    received_event = ev;
    received = true;
  });

  SocketEvent writer(*loop_, false, socket_path_);
  SocketEvent::Event sent_event{42};
  writer.Send(sent_event);

  PumpLoopUntil([&] { return received.load(); });

  ASSERT_TRUE(received);
  EXPECT_EQ(received_event.buffer_number, sent_event.buffer_number);
}

TEST_F(TrellisSocketEventTest, MultipleEventsAreReceived) {
  const int kNumEvents = 5;
  std::atomic<int> received_count{0};

  SocketEvent reader(*loop_, true, socket_path_);
  reader.AsyncReceive([&](SocketEvent::Event ev) {
    EXPECT_GE(ev.buffer_number, 0u);
    EXPECT_LT(ev.buffer_number, static_cast<unsigned>(kNumEvents));
    received_count++;
  });

  SocketEvent writer(*loop_, false, socket_path_);
  for (unsigned i = 0; i < kNumEvents; ++i) {
    SocketEvent::Event event{i};
    writer.Send(event);
  }

  PumpLoopUntil([&] { return received_count.load() == kNumEvents; }, 500);

  EXPECT_EQ(received_count.load(), kNumEvents);
}
