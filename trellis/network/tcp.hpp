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

#ifndef TRELLIS_NETWORK_TCP_HPP
#define TRELLIS_NETWORK_TCP_HPP

#include "trellis/core/error_code.hpp"
#include "trellis/core/event_loop.hpp"

namespace trellis {
namespace network {

/**
 * TCP class for performing asynchronous IO on a TCP socket
 *
 * TODO list for whenever these are needed:
 * - Support timeouts on async operations
 * - Support binding to specific endpoint (rather than just port)
 */
class TCP {
 public:
  using SocketPtr = std::shared_ptr<asio::ip::tcp::socket>;
  using IOCompletionHandler = std::function<void(const trellis::core::error_code&, size_t)>;
  using CompletionHandler = std::function<void(const trellis::core::error_code&)>;
  /**
   * TCP constructor for already created socket (such has from TCPServer)
   *
   * @param socket shared pointer to underlying asio tcp socket
   *
   * @see TCPServer
   */
  TCP(SocketPtr socket) : socket_{socket} {}

  /**
   * TCP constructor for a client-side socket
   *
   * This constructor will make a synchronous connection attempt to the remote host.
   *
   * @param loop the event loop thread handle to use for IO
   * @param remote_address a string representing the remote address to connect to
   * @param remote_ipv4_port the remote IPv4 port to connect to
   *
   */
  TCP(trellis::core::EventLoop loop, std::string remote_address, uint16_t remote_ipv4_port)
      : socket_{std::make_shared<asio::ip::tcp::socket>(*loop, asio::ip::tcp::v4())} {
    socket_->connect(asio::ip::tcp::endpoint(asio::ip::make_address(remote_address), remote_ipv4_port));
  }

  /**
   * TCP constructor for a client-side socket
   *
   * This constructor will make an asynchronous connection attempt to the remote host.
   *
   * @param loop the event loop thread handle to use for IO
   * @param remote_address a string representing the remote address to connect to
   * @param remote_ipv4_port the remote IPv4 port to connect to
   * @param callback handler for when connection is established
   *
   */
  TCP(trellis::core::EventLoop loop, std::string remote_address, uint16_t remote_ipv4_port, CompletionHandler callback)
      : socket_{std::make_shared<asio::ip::tcp::socket>(*loop, asio::ip::tcp::v4())} {
    socket_->async_connect(asio::ip::tcp::endpoint(asio::ip::make_address(remote_address), remote_ipv4_port), callback);
  }

  /**
   * AsyncSend send data on the socket asynchronously
   *
   * @param data a pointer to the buffer to send
   * @param length the size of the data in the buffer
   * @param callback the completion handler
   */
  void AsyncSend(const void* data, size_t length, IOCompletionHandler callback) {
    socket_->async_send(asio::buffer(data, length), callback);
  }

  /**
   * AsyncReceive receive data on the socket asynchronously
   *
   * @param data a pointer to the buffer to receive data in
   * @param length the size of the buffer to receive data in
   * @param callback the completion handler
   */
  void AsyncReceive(void* data, size_t length, IOCompletionHandler callback) {
    socket_->async_receive(asio::buffer(data, length), callback);
  }

  /**
   * Send send data on the socket synchronously
   *
   * @param data a pointer to the buffer to send
   * @param length the size of the data in the buffer
   *
   * @return number of bytes sent (throws on error)
   */
  std::size_t Send(const void* data, size_t length) { return socket_->send(asio::buffer(data, length)); }

  /**
   * Receive receive data on the socket synchronously
   *
   * @param data a pointer to the buffer to receive data in
   * @param length the size of the buffer to receive data in
   *
   * @return number of bytes received (throws on error)
   */
  std::size_t Receive(void* data, size_t length) { return socket_->receive(asio::buffer(data, length)); }

  /*
   * IsOpen whether or not the underlying socket is in the open state
   */
  bool IsOpen() const { return socket_->is_open(); }

  /**
   * GetPort retrieve the port that this socket is bound to
   *
   * This can be useful in cases where the OS selected the port for us (e.g. when 0 is used)
   * @return the port number this socket is currently bound to
   */
  uint16_t GetPort() const { return socket_->local_endpoint().port(); }

  /**
   * GetRemotePort retrieves the port that the remote peer is using (if available).
   *
   * This is useful for logging, debugging, or identifying which remote port is connected to us.
   * @return optionally, the port number the remote endpoint is currently using if connected
   */
  std::optional<uint16_t> GetRemotePort() const {
    trellis::core::error_code ec;
    const auto ep = socket_->remote_endpoint(ec);
    if (!ec) {
      return ep.port();
    }
    return {};
  }

  /**
   * GetAddress retrieve a string representing the address the socket is bound to
   *
   * @return a string containing the local address
   */
  std::string GetAddress() const { return socket_->local_endpoint().address().to_string(); }

  /**
   * Cancel all pending events
   */
  void Cancel() { socket_->cancel(); }

  /**
   * Close the underlying socket
   */
  void Close() { socket_->close(); }

 private:
  SocketPtr socket_;
};

/**
 * TCPServer class to manage a passive socket responsible for listening/accepting inbound connections
 */
class TCPServer {
 public:
  using NewConnectionHandler = std::function<void(const trellis::core::error_code&, TCP)>;

  /**
   * TCPServer construct a server instance listening on a given port
   *
   * @param loop the event loop thread handle to use for IO
   * @param ipv4_listen_port the ipv4 port to listen on
   * @param callback the handler for new inbound connections
   */
  TCPServer(trellis::core::EventLoop loop, uint16_t ipv4_listen_port, NewConnectionHandler callback)
      : loop_{loop},
        acceptor_{*loop, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), ipv4_listen_port)},
        connection_callback_{callback} {
    AcceptNextConnection();
  }

  uint16_t GetPort() const { return acceptor_.local_endpoint().port(); }
  std::string GetAddress() const { return acceptor_.local_endpoint().address().to_string(); }

 private:
  void AcceptNextConnection() {
    auto socket = std::make_shared<asio::ip::tcp::socket>(*loop_);
    acceptor_.async_accept(*socket, [this, socket](const trellis::core::error_code& error) {
      if (error == asio::error::operation_aborted) {
        // Exit immediately because `this` would have been inavlidated
        return;
      }
      connection_callback_(error, TCP(socket));  // pass socket back to user
      AcceptNextConnection();                    // accept any other inbounds
    });
  }
  trellis::core::EventLoop loop_;
  asio::ip::tcp::acceptor acceptor_;
  NewConnectionHandler connection_callback_;
};

}  // namespace network
}  // namespace trellis

#endif  // TRELLIS_NETWORK_TCP_HPP
