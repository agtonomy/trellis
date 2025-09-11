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

#include <unistd.h>

#include <cstring>
#include <filesystem>

#include "trellis/core/ipc/named_resource_registry.hpp"
#include "trellis/core/logging.hpp"

namespace trellis::core::ipc::unix {

SocketEvent::SocketEvent(trellis::core::EventLoop loop, bool reader, std::string handle)
    : reader_(reader), handle_(std::move(handle)), socket_(*loop) {
  if (reader_) {
    // First create the base directories if they don't exist
    std::filesystem::path handle_path(handle_);
    std::filesystem::create_directories(handle_path.parent_path());

    // Unlink if there is a collision with an existing resource
    ::unlink(handle_.c_str());

    // Open and bind the socket to the handle
    endpoint_ = asio::local::datagram_protocol::endpoint(handle_);
    socket_.open();
    socket_.non_blocking(true);
    socket_.set_option(asio::socket_base::send_buffer_size(sizeof(Event) * kMaxEventNumEvents));
    socket_.set_option(asio::socket_base::receive_buffer_size(sizeof(Event) * kMaxEventNumEvents));

    const int previous_umask =
        ::umask(000);  // set umask to nothing, so we can create files with all possible permission bits
    socket_.bind(endpoint_);
    (void)::umask(previous_umask);  // reset umask to previous permissions
    if (::chmod(endpoint_.path().c_str(), 0777) < 0) {
      throw std::system_error(errno, std::generic_category(), "SocketEvent::chmod failed");
    }
    NamedResourceRegistry::Get().Insert(handle_);
  } else {
    endpoint_ = asio::local::datagram_protocol::endpoint(handle_);
    socket_.open();
    socket_.non_blocking(true);
  }
}

SocketEvent::~SocketEvent() {
  Log::Info("Trellis::SocketEvent destructor called for handle {}, reader={}", handle_, reader_);
  if (socket_.is_open()) {
    socket_.close();
  }
  if (handle_.size() > 0 && reader_) {
    ::unlink(handle_.c_str());
  }
}

bool SocketEvent::Send(Event event) {
  // Assign sequence number for drop detection
  event.sequence_number = send_sequence_++;

  asio::error_code ec;
  socket_.send_to(asio::buffer(&event, sizeof(Event)), endpoint_, 0, ec);

  /*
   * Error Handling Strategy for SocketEvent::Send
   *
   * Temporary Errors (don't remove reader):
   * - EAGAIN/EWOULDBLOCK: Socket buffer full - common under high load
   * - Other transient system errors
   *
   * Permanent Errors (remove reader):
   * - ENOENT: Socket file doesn't exist - reader process died
   * - ECONNREFUSED: Connection refused - reader socket closed
   * - ENOTCONN: Not connected - reader disconnected
   * - EMSGSIZE: Message too large - programming error
   *
   * This prevents sequence number jumps caused by temporary socket buffer
   * congestion while still cleaning up dead readers.
   */

  if (ec) {
    // Handle different error cases appropriately
    if (ec == asio::error::would_block || ec == asio::error::try_again) {
      // Temporary condition - socket buffer is full
      // This is expected under high load and should not cause reader removal
      // Log::Warn("Trellis::SocketEvent::Send - Socket buffer full, dropping notification for sequence {}",
      // event.sequence_number);
      return true;                            // Return true to prevent reader removal
    } else if (ec.value() == ENOENT ||        // No such file or directory
               ec.value() == ECONNREFUSED ||  // Connection refused
               ec.value() == ENOTCONN ||      // Transport endpoint is not connected
               ec.value() == ENOTSOCK) {      // Socket operation on non-socket
      // Permanent errors - reader socket doesn't exist
      // These indicate the reader process is gone - maybe?
      // we exclusively get "No such file or directory (error: 2)" if ec != 0
      // this causes
      Log::Warn("Trellis::SocketEvent::Send - error: {} (error: {})", ec.message(), ec.value());
      return false;  // Return false to remove this reader
    } else if (ec == asio::error::message_size) {
      // Programming error - message too large
      // Log::Warn("Trellis::SocketEvent::Send - message too large: {} (error: {})", ec.message(), ec.value());
      return false;
    } else {
      // Unexpected errors - log but don't remove reader immediately
      // Log::Warn("Trellis::SocketEvent::Send - Unexpected error: {} (error: {})", ec.message(), ec.value());
      return true;  // Give benefit of doubt, don't remove reader
    }
  }
  return true;
}

void SocketEvent::AsyncReceive(ReceiveCallback callback) {
  callback_ = std::move(callback);
  StartReceive();
}

void SocketEvent::StartReceive() {
  socket_.async_wait(asio::socket_base::wait_read, [this](const std::error_code& ec) {
    if (ec == asio::error::operation_aborted) {
      return;
    }

    std::array<char, sizeof(Event)> buf;
    asio::error_code read_ec;
    unsigned packets_received{0};

    do {
      const std::size_t bytes_recvd =
          socket_.receive_from(asio::buffer(buf), endpoint_, asio::socket_base::message_flags(), read_ec);

      if (!read_ec && bytes_recvd == sizeof(Event)) {
        Event event;
        std::memcpy(&event, buf.data(), sizeof(Event));
        ++packets_received;

        // Update total received count
        ++metrics_.total_received;

        // Check for sequence number gaps (dropped packets)
        if (!first_packet_received_) {
          // First packet - initialize expected sequence
          expected_sequence_ = event.sequence_number + 1;
          first_packet_received_ = true;
        } else {
          // Check for sequence gaps
          if (event.sequence_number != expected_sequence_) {
            if (event.sequence_number > expected_sequence_) {
              // Packets were dropped
              uint64_t dropped = event.sequence_number - expected_sequence_;
              metrics_.dropped_packets += dropped;
              Log::Warn("Trellis::SocketEvent::Receive - {} packet(s) dropped (expected seq {}, got {})", dropped,
                        expected_sequence_, event.sequence_number);
            } else if (event.sequence_number < expected_sequence_) {
              // Out-of-order or duplicate packet
              Log::Warn("Trellis::SocketEvent::Receive - Out-of-order packet (expected seq {}, got {})",
                        expected_sequence_, event.sequence_number);
            }
          }
          expected_sequence_ = event.sequence_number + 1;
        }

        if (callback_) {
          callback_(event);
        }
      }
    } while (!read_ec);  // exit on first error (e.g., EAGAIN / EWOULDBLOCK)

    // Update max burst size metric
    if (packets_received > metrics_.max_burst_size) {
      metrics_.max_burst_size = packets_received;
    }

    // Re-arm the wait for the next datagram
    StartReceive();
  });
}

}  // namespace trellis::core::ipc::unix
