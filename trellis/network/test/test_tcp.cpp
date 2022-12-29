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

#include <iostream>

#include "trellis/network/tcp.hpp"

using trellis::network::TCP;
using trellis::network::TCPServer;

TEST(TCPTests, MultipleConnections) {
  static unsigned connection_count{0};
  trellis::core::EventLoop loop;
  TCPServer server(loop, 0, [](const trellis::core::error_code& error, TCP socket) {
    ++connection_count;
    ASSERT_EQ(!error, true);
  });

  auto server_port = server.GetPort();

  TCP client1(loop, "127.0.0.1", server_port);
  TCP client2(loop, "127.0.0.1", server_port);

  loop.RunOne();
  loop.RunOne();
  ASSERT_EQ(connection_count, 2);
}

TEST(TCPTests, SendAndReceive) {
  static unsigned connection_count{0};
  static unsigned receive_request_count{0};
  static unsigned send_request_count{0};
  static unsigned send_response_count{0};
  static unsigned receive_response_count{0};
  static std::string test_msg{"The Quick Brown Fox Jumps Over The Lazy Dog."};
  static std::array<uint8_t, 1024> buffer;
  trellis::core::EventLoop loop;
  TCPServer server(loop, 0, [](const trellis::core::error_code& error, TCP socket) {
    ++connection_count;
    ASSERT_EQ(!error, true);
    socket.AsyncReceive(
        buffer.data(), buffer.size(), [socket](const trellis::core::error_code& error, size_t size) mutable {
          ++receive_request_count;
          ASSERT_EQ(!error, true);
          ASSERT_EQ(size, test_msg.size());
          std::stringstream response;
          response << " Received " << size << " bytes. Receive count = " << receive_request_count;
          std::string resp_msg = response.str();
          socket.AsyncSend(resp_msg.c_str(), resp_msg.size(), [](const trellis::core::error_code& error, size_t size) {
            ++send_response_count;
            ASSERT_EQ(!error, true);
            ASSERT_NE(size, 0);
          });
        });
  });

  auto server_port = server.GetPort();

  TCP client(loop, "127.0.0.1", server_port);
  loop.RunOne();
  ASSERT_EQ(connection_count, 1);

  client.AsyncSend(test_msg.c_str(), test_msg.size(), [](const trellis::core::error_code& error, size_t size) {
    ++send_request_count;
    ASSERT_EQ(!error, true);
    ASSERT_EQ(size, test_msg.size());
  });
  loop.RunOne();
  loop.RunOne();
  ASSERT_EQ(receive_request_count, 1);
  ASSERT_EQ(send_request_count, 1);

  client.AsyncReceive(buffer.data(), buffer.size(), [](const trellis::core::error_code& error, size_t size) {
    ++receive_response_count;
    ASSERT_EQ(size, 37);
  });

  loop.RunOne();
  loop.RunOne();
  ASSERT_EQ(receive_response_count, 1);
  ASSERT_EQ(send_response_count, 1);
}

TEST(TCPTests, AsyncConnect) {
  static unsigned connection_count{0};
  static unsigned connection_complete_count{0};
  trellis::core::EventLoop loop;
  TCPServer server(loop, 0, [](const trellis::core::error_code& error, TCP socket) {
    ++connection_count;
    ASSERT_EQ(!error, true);
  });

  auto server_port = server.GetPort();

  TCP client(loop, "127.0.0.1", server_port,
             [](const trellis::core::error_code& error) { ++connection_complete_count; });

  loop.RunOne();
  loop.RunOne();
  ASSERT_EQ(connection_count, 1);
  ASSERT_EQ(connection_complete_count, 1);
}
