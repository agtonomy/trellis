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
#include "trellis/utils/umask_guard/umask_guard.hpp"

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

    {
      trellis::utils::UmaskGuard guard(000);  // thread-safe umask manipulation
      socket_.bind(endpoint_);
    }
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
  if (socket_.is_open()) {
    socket_.close();
  }
  if (handle_.size() > 0 && reader_) {
    ::unlink(handle_.c_str());
  }
}

bool SocketEvent::Send(Event event) {
  try {
    socket_.send_to(asio::buffer(&event, sizeof(Event)), endpoint_);
  } catch (std::system_error& e) {
    Log::Debug("SocketEvent::Send - {}", e.what());
    return false;
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
