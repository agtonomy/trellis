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

#ifndef TRELLIS_CORE_IPC_UNIX_SOCKET_EVENT_HPP_
#define TRELLIS_CORE_IPC_UNIX_SOCKET_EVENT_HPP_

#include <array>
#include <asio.hpp>
#include <functional>
#include <memory>
#include <string>

#include "trellis/core/event_loop.hpp"

namespace trellis::core::ipc::unix {

/**
 * @brief Class for sending and receiving small event notifications over UNIX domain sockets.
 *
 * This class is used to notify other processes (typically readers) of an event, such as new data
 * being available in shared memory. Events are encoded as small datagrams containing simple payloads.
 * Designed to work with ASIO for async operation.
 */
class SocketEvent {
 public:
  /**
   * @brief Struct representing a simple event payload.
   *
   * Contains a buffer index or other identifier sent from the writer to the reader.
   */
  struct Event {
    unsigned buffer_number;  ///< Identifier for the buffer associated with the event.
  };

  /// Callback type used to deliver received events asynchronously.
  using ReceiveCallback = std::function<void(Event)>;

  /**
   * @brief Constructs a new `SocketEvent` instance.
   *
   * @param loop Event loop used for asynchronous socket operations.
   * @param reader Whether this instance is used for receiving (true) or sending (false).
   * @param handle A unique identifier used to construct the UNIX socket path.
   */
  SocketEvent(trellis::core::EventLoop loop, bool reader, std::string handle);

  /// Destructor closes the socket and releases any OS-level resources.
  ~SocketEvent();

  /**
   * @brief Sends an event over the UNIX domain socket.
   *
   * @param event The event payload to send.
   * @return true if the send operation succeeded, false otherwise.
   */
  bool Send(Event event);

  /**
   * @brief Starts asynchronous reception of events using the given callback.
   *
   * Incoming events will trigger the callback.
   *
   * @param callback The function to call when an event is received.
   */
  void AsyncReceive(ReceiveCallback callback);

  SocketEvent(const SocketEvent&) = delete;
  SocketEvent& operator=(const SocketEvent&) = delete;
  SocketEvent(SocketEvent&&) = default;
  SocketEvent& operator=(SocketEvent&&) = delete;

 private:
  /**
   * @brief Begins waiting for an incoming datagram event.
   *
   * Called internally after setting the receive callback.
   */
  void StartReceive();

  trellis::core::EventLoop loop_;  ///< Event loop used for async I/O.
  bool reader_;                    ///< True if this is a receiving endpoint.
  std::string handle_;             ///< Name used to generate socket path.

  asio::local::datagram_protocol::socket socket_;      ///< ASIO socket for local domain datagrams.
  asio::local::datagram_protocol::endpoint endpoint_;  ///< Socket address.

  ReceiveCallback callback_;  ///< User-provided callback for incoming events.
};

}  // namespace trellis::core::ipc::unix

#endif  // TRELLIS_CORE_IPC_UNIX_SOCKET_EVENT_HPP_
