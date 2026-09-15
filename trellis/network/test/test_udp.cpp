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

#include <string>
#include <vector>

#include "trellis/network/udp.hpp"

using trellis::network::UDP;
using trellis::network::UDPReceiver;

TEST(UDPTests, SendAndReceive) {
  trellis::core::EventLoop loop;
  static std::string msg{"The quick brown fox jumps over the lazy dog."};

  std::array<uint8_t, 1024> buffer;
  static unsigned receive_count = 0;

  UDP receiver(loop, static_cast<uint16_t>(0));
  UDP sender(loop);

  receiver.AsyncReceiveFrom(
      buffer.data(), buffer.size(),
      [](const trellis::core::error_code&, const asio::ip::udp::endpoint&, void* data, size_t size) {
        std::cout << "Received " << size << " bytes!" << std::endl;
        ++receive_count;
        ASSERT_EQ(msg.size(), size);
        auto r = memcmp(msg.data(), data, size);
        ASSERT_EQ(r, 0);
      });

  sender.AsyncSendTo(
      "127.0.0.1", receiver.GetPort(), msg.c_str(), msg.size(),
      [](const trellis::core::error_code&, size_t size) { std::cout << "Sent " << size << " bytes!" << std::endl; });

  // Run once for send and receive
  static constexpr unsigned expected_receive_count = 1;
  for (unsigned i = 0; i < expected_receive_count * 2; ++i) {
    loop.RunOne();  // call once per receive and send
  }
  ASSERT_EQ(receive_count, expected_receive_count);
}

TEST(UDPTests, Receiver) {
  trellis::core::EventLoop loop;

  static std::string msg{"The quick brown fox jumps over the lazy dog."};
  static unsigned receive_count = 0;
  static unsigned receive_bytes_count = 0;

  UDPReceiver<1024> receiver(loop, static_cast<uint16_t>(0),
                             [](const void* data, size_t size, const asio::ip::udp::endpoint& ep) {
                               std::cout << "Received " << size << " bytes!" << std::endl;
                               ++receive_count;
                               receive_bytes_count += size;
                             });

  UDP sender(loop);

  sender.AsyncSendTo(
      "127.0.0.1", receiver.GetPort(), msg.c_str(), msg.size(),
      [](const trellis::core::error_code&, size_t size) { std::cout << "Sent " << size << " bytes!" << std::endl; });
  sender.AsyncSendTo(
      "127.0.0.1", receiver.GetPort(), msg.c_str(), msg.size(),
      [](const trellis::core::error_code&, size_t size) { std::cout << "Sent " << size << " bytes!" << std::endl; });

  constexpr unsigned expected_receive_count = 2;
  loop.RunFor(std::chrono::milliseconds(5));
  ASSERT_EQ(receive_count, expected_receive_count);
  ASSERT_EQ(receive_bytes_count, 88);
}

// Three datagrams are queued before the loop runs, so one wakeup must deliver all of them. If the drain after the first
// packet is broken, only that packet arrives from this wakeup and the rest wait for the next one.
TEST(UDPTests, ReceiverDrainsBacklogInOneWakeup) {
  trellis::core::EventLoop loop;
  const std::string msg{"payload"};
  unsigned receive_count = 0;

  UDPReceiver<1024> receiver(
      loop, static_cast<uint16_t>(0),
      [&receive_count](const void*, size_t, const asio::ip::udp::endpoint&) { ++receive_count; });

  UDP sender(loop);
  constexpr unsigned expected_receive_count = 3;
  for (unsigned i = 0; i < expected_receive_count; ++i) {
    size_t bytes_sent = 0;
    ASSERT_FALSE(sender.SendTo("127.0.0.1", receiver.GetPort(), msg.data(), msg.size(), bytes_sent));
  }

  loop.RunOne();
  ASSERT_EQ(receive_count, expected_receive_count);
}

// A receiver built without a callback arms nothing, so the loop delivers nothing on its own and the owner's Drain()
// call is what hands the backlog over.
TEST(UDPTests, ReceiverWithoutCallbackIsDrainedByOwner) {
  trellis::core::EventLoop loop;
  const std::string msg{"The quick brown fox jumps over the lazy dog."};
  std::vector<std::string> received;

  UDPReceiver<1024> receiver(loop, static_cast<uint16_t>(0));

  UDP sender(loop);
  constexpr unsigned expected_receive_count = 3;
  for (unsigned i = 0; i < expected_receive_count; ++i) {
    size_t bytes_sent = 0;
    ASSERT_FALSE(sender.SendTo("127.0.0.1", receiver.GetPort(), msg.data(), msg.size(), bytes_sent));
    ASSERT_EQ(bytes_sent, msg.size());
  }

  loop.RunFor(std::chrono::milliseconds(5));
  ASSERT_TRUE(received.empty());

  receiver.Drain([&received](const void* data, size_t size, const asio::ip::udp::endpoint&) {
    received.emplace_back(static_cast<const char*>(data), size);
  });
  ASSERT_EQ(received.size(), expected_receive_count);
  for (const auto& payload : received) {
    EXPECT_EQ(payload, msg);
  }
}

// A zero length datagram is legal on UDP and must not be mistaken for an empty queue, or everything behind it would be
// stranded until the next drain.
TEST(UDPTests, DrainDeliversZeroLengthDatagram) {
  trellis::core::EventLoop loop;
  const std::string msg{"payload"};
  std::vector<size_t> received_sizes;

  UDPReceiver<1024> receiver(loop, static_cast<uint16_t>(0));

  UDP sender(loop);
  for (const size_t length : {msg.size(), size_t{0}, msg.size()}) {
    size_t bytes_sent = 0;
    ASSERT_FALSE(sender.SendTo("127.0.0.1", receiver.GetPort(), msg.data(), length, bytes_sent));
  }

  receiver.Drain(
      [&received_sizes](const void*, size_t size, const asio::ip::udp::endpoint&) { received_sizes.push_back(size); });
  ASSERT_EQ(received_sizes.size(), size_t{3});
  EXPECT_EQ(received_sizes[0], msg.size());
  EXPECT_EQ(received_sizes[1], size_t{0});
  EXPECT_EQ(received_sizes[2], msg.size());
}
