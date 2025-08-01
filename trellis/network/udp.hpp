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

#ifndef TRELLIS_NETWORK_UDP_HPP
#define TRELLIS_NETWORK_UDP_HPP

#include <array>

#include "trellis/core/error_code.hpp"
#include "trellis/core/event_loop.hpp"

namespace trellis {
namespace network {

/**
 * UDP Class for performing asynchronous IO on a UDP socket
 * TODO list for whenever these are needed:
 * - Support timeouts on async operations
 * - Support binding to specific interface in `UDPReceiver`
 */
class UDP {
 public:
  using CompletionHandler = std::function<void(const trellis::core::error_code&, size_t)>;
  using ReceiveCallback =
      std::function<void(const trellis::core::error_code&, const asio::ip::udp::endpoint&, void*, size_t)>;
  /**
   * UDP opens a UDP socket for sending IPv4 traffic (does not bind an interface/port)
   *
   * @param loop the event loop thread handle to use for IO
   */
  explicit UDP(trellis::core::EventLoop loop) : loop_{loop}, socket_{*loop, asio::ip::udp::v4()} {
    socket_.non_blocking(true);  // for non-blocking synchronous operations
  }

  /**
   * UDP opens a UDP socket bound to a given port on all interfaces
   *
   * @param loop the event loop thread handle to use for IO
   * @param ipv4_port The UDP port to bind to
   */
  explicit UDP(trellis::core::EventLoop loop, uint16_t ipv4_port)
      : loop_{loop}, socket_{*loop, asio::ip::udp::endpoint(asio::ip::udp::v4(), ipv4_port)} {
    socket_.non_blocking(true);  // for non-blocking synchronous operations
  }

  /**
   * UDP creates a UDP socket assigned to the given file descriptor
   *
   * @param loop the event loop thread handle to use for IO
   * @param socket_fd The socket file descriptor to assign to this socket
   */
  explicit UDP(trellis::core::EventLoop loop, asio::ip::udp::socket::native_handle_type socket_fd)
      : loop_{loop}, socket_(*loop, asio::ip::udp::v4(), socket_fd) {
    socket_.non_blocking(true);  // for non-blocking synchronous operations
  }

  /**
   * UDP opens a UDP socket bound to a given port on the interface given by address
   *
   * @param loop the event loop thread handle to use for IO
   * @param ipv4_address the IPv4 address to bind to in dotted decimal notation (e.g. xxx.xxx.xxx.xxx)
   * @param ipv4_port The UDP port to bind to
   */
  explicit UDP(trellis::core::EventLoop loop, std::string ipv4_address, uint16_t ipv4_port)
      : loop_{loop}, socket_{*loop, asio::ip::udp::endpoint(asio::ip::make_address(ipv4_address), ipv4_port)} {
    socket_.non_blocking(true);  // for non-blocking synchronous operations
  }

  void AsyncSendTo(std::string ipv4_address, uint16_t ipv4_port, const void* data, size_t length,
                   CompletionHandler callback) {
    asio::ip::udp::endpoint destination(asio::ip::make_address(ipv4_address), ipv4_port);
    socket_.async_send_to(asio::buffer(data, length), destination, 0,
                          [callback](const trellis::core::error_code& code, size_t size) {
                            if (code == asio::error::operation_aborted) {
                              return;
                            }
                            callback(code, size);
                          });
  }

  /**
   * SendTo synchronously send data to a specific address and port
   *
   * This function performs a non-blocking synchronous send. If the send cannot be completed immediately,
   * it will return an error rather than blocking.
   *
   * @param ipv4_address the IPv4 address to send to in dotted decimal notation (e.g. xxx.xxx.xxx.xxx)
   * @param ipv4_port the UDP port to send to
   * @param data a pointer to the data to send
   * @param length the length of the data to send
   * @param bytes_sent output parameter that will contain the number of bytes sent on success
   * @return error_code indicating success or the specific error that occurred
   */
  trellis::core::error_code SendTo(std::string ipv4_address, uint16_t ipv4_port, const void* data, size_t length,
                                   size_t& bytes_sent) {
    asio::ip::udp::endpoint destination(asio::ip::make_address(ipv4_address), ipv4_port);
    trellis::core::error_code ec;
    bytes_sent = socket_.send_to(asio::buffer(data, length), destination, 0, ec);
    return ec;
  }

  /**
   * AsyncReceiveFrom receive data into the given buffer and callback when complete
   *
   * @param data a pointer to the buffer which to receive data in
   * @param length the length of the buffer to prevent overrunning it
   * @param callback the function to call when data is received (or an error occurs)
   */
  void AsyncReceiveFrom(void* data, size_t length, ReceiveCallback callback) {
    asio::mutable_buffer buffer(data, length);
    socket_.async_receive_from(buffer, sender_endpoint_, 0,
                               [this, data, callback](const trellis::core::error_code& code, size_t size) {
                                 if (code == asio::error::operation_aborted) {
                                   return;
                                 }
                                 callback(code, sender_endpoint_, data, size);
                               });
  }

  /**
   * GetPort retrieve the port that this socket is bound to
   *
   * This can be useful in cases where the OS selected the port for us (e.g. when 0 is used)
   * @return the port number this socket is currently bound to
   */
  uint16_t GetPort() const { return socket_.local_endpoint().port(); }

  /**
   * GetSocket retrieve the underlying asio socket
   *
   * @return a reference to the asio socket
   */
  asio::ip::udp::socket& GetSocket() { return socket_; }

 private:
  void ReceiveHandler(const trellis::core::error_code& code, std::size_t bytes_transferred) {}
  trellis::core::EventLoop loop_;
  asio::ip::udp::socket socket_;

  // when receiving, we must maintain this memory to hold the sender's endpoint
  asio::ip::udp::endpoint sender_endpoint_;
};

/**
 * UDPReceiver Class to handle the use case where you just want to continuously receive from a UDP socket
 *
 * This class allocates a static buffer internally to handle the inbound messages so that the user doesn't
 * have to manage buffers at all. The template argument `BUFFER_SIZE` specifies the static buffer size and
 * must be large enough to hold the messages we expect to come inbound on this socket.
 */
template <size_t BUFFER_SIZE = 1024>
class UDPReceiver {
 public:
  using Callback = std::function<void(const void*, size_t, const asio::ip::udp::endpoint&)>;

  /**
   * UDPReceiver constructor for receiving UDP packets
   *
   * @param loop the event loop thread handle to use for IO
   * @param ipv4_port the IPv4 port number to bind this socket to
   * @param callback the callback to invoke any time inbound data has arrived
   */
  explicit UDPReceiver(trellis::core::EventLoop loop, uint16_t ipv4_port, Callback callback)
      : udp_(loop, ipv4_port), callback_{callback} {
    udp_.AsyncReceiveFrom(buffer_.data(), buffer_.size(),
                          [this](const trellis::core::error_code& code, const asio::ip::udp::endpoint& ep, void*,
                                 size_t size) { DidReceive(code, ep, size); });
  }

  /**
   * UDPReceiver constructor for receiving UDP packets
   *
   * @param loop the event loop thread handle to use for IO
   * @param ipv4_port the IPv4 port number to bind this socket to
   * @param callback the callback to invoke any time inbound data has arrived
   */
  explicit UDPReceiver(trellis::core::EventLoop loop, asio::ip::udp::socket::native_handle_type socket_fd,
                       Callback callback)
      : udp_(loop, socket_fd), callback_{callback} {
    udp_.AsyncReceiveFrom(buffer_.data(), buffer_.size(),
                          [this](const trellis::core::error_code& code, const asio::ip::udp::endpoint& ep, void*,
                                 size_t size) { DidReceive(code, ep, size); });
  }

  uint16_t GetPort() const { return udp_.GetPort(); }

 private:
  void DidReceive(const trellis::core::error_code& code, const asio::ip::udp::endpoint& ep, size_t size) {
    // call to user for the first packet
    callback_(buffer_.data(), size, ep);

    // Drain the socket of any more packets before we go back to the event loop
    auto& socket = udp_.GetSocket();
    while (socket.available()) {
      asio::ip::udp::endpoint sender_endpoint;
      trellis::core::error_code ec;
      size_t bytes_received = socket.receive_from(asio::buffer(buffer_.data(), buffer_.size()), sender_endpoint, 0, ec);
      if (ec || bytes_received == 0) {
        break;
      }
      callback_(buffer_.data(), bytes_received, sender_endpoint);
    }

    // queue up next receive
    udp_.AsyncReceiveFrom(buffer_.data(), buffer_.size(),
                          [this](const trellis::core::error_code& code, const asio::ip::udp::endpoint& ep, void*,
                                 size_t size) { DidReceive(code, ep, size); });
  }
  std::array<uint8_t, BUFFER_SIZE> buffer_;
  UDP udp_;
  Callback callback_;
};

}  // namespace network
}  // namespace trellis

#endif  // TRELLIS_NETWORK_UDP_HPP
