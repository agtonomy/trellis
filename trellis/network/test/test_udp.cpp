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

#include "trellis/network/udp.hpp"

using trellis::network::UDP;
using trellis::network::UDPReceiver;

TEST(UDPTests, SendAndReceive) {
  trellis::core::EventLoop loop;
  static std::string msg{"The quick brown fox jumps over the lazy dog."};

  std::array<uint8_t, 1024> buffer;
  static unsigned receive_count = 0;

  UDP receiver(loop, 0);
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

  UDPReceiver<1024> receiver(loop, 0, [](const void* data, size_t size, const asio::ip::udp::endpoint& ep) {
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

  static constexpr unsigned expected_receive_count = 2;
  for (unsigned i = 0; i < expected_receive_count * 2; ++i) {
    loop.RunOne();  // call once per receive and send
  }
  ASSERT_EQ(receive_count, expected_receive_count);
  ASSERT_EQ(receive_bytes_count, 88);
}
