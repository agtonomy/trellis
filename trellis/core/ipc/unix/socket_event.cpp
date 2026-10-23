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
#include "trellis/core/ipc/utils.hpp"
#include "trellis/core/logging.hpp"
#include "trellis/utils/umask_guard/umask_guard.hpp"

namespace trellis::core::ipc::unix {

SocketEvent::SocketEvent(trellis::core::EventLoop loop, bool reader, std::string handle,
                         const trellis::core::Config& config)
    : reader_(reader), handle_(std::move(handle)), socket_(*loop) {
  if (reader_) {
    // Unlink if there is a collision with an existing resource
    ::unlink(handle_.c_str());

    // Open and bind the socket to the handle
    endpoint_ = asio::local::datagram_protocol::endpoint(handle_);
    socket_.open();
    socket_.non_blocking(true);
    socket_.set_option(asio::socket_base::send_buffer_size(sizeof(Event) * kMaxEventNumEvents));
    socket_.set_option(asio::socket_base::receive_buffer_size(sizeof(Event) * kMaxEventNumEvents));

    {
      const auto [uid_opt, gid_opt] = trellis::core::ipc::utils::GetUidGidFromConfig(config);
      trellis::utils::UmaskGuard guard(000, uid_opt, gid_opt);
      std::filesystem::path handle_path(handle_);
      std::filesystem::create_directories(handle_path.parent_path());
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

SocketEvent::~SocketEvent() { Stop(); }

void SocketEvent::Stop() {
  if (socket_.is_open()) {
    socket_.close();
  }
  if (handle_.size() > 0 && reader_) {
    ::unlink(handle_.c_str());
    // Drops one registration of the path. Stop() runs again from the destructor, which finds none to drop unless the
    // path was registered again in between, and would then drop that one.
    NamedResourceRegistry::Get().Remove(handle_);
  }
}

bool SocketEvent::Send(Event event) {
  // Connected once rather than addressed on every send, so the kernel looks up the reader's path and checks write
  // permission on its socket file once instead of for every event. The LSM check on the peer (AppArmor's, in a
  // container) still runs on every send. A send that finds the connected peer gone reconnects once, in case the reader
  // was replaced on the same path. Any other error, such as a full queue, fails the send without reconnecting.
  for (int attempt = 0; attempt < 2; ++attempt) {
    asio::error_code ec;
    if (!connected_) {
      socket_.connect(endpoint_, ec);
      connected_ = !ec;
    }
    if (connected_) {
      socket_.send(asio::buffer(&event, sizeof(Event)), asio::socket_base::message_flags(), ec);
      if (!ec) return true;
    }
    Log::Debug("SocketEvent::Send - {}", ec.message());
    const bool peer_gone = ec == asio::error::connection_refused || ec == asio::error::not_connected ||
                           ec == asio::error::connection_reset;
    if (!connected_ || !peer_gone) return false;
    connected_ = false;
  }
  return false;
}

void SocketEvent::AsyncReceive(ReceiveCallback callback) {
  callback_ = std::move(callback);
  StartReceive();
}

void SocketEvent::StartReceive() {
  if (!socket_.is_open()) {
    // There is no reason to wait for further events if the socket is closed.
    // If the socket has been closed then it is reasonable to assume that "this" will soon be destroyed.
    return;
  }

  socket_.async_wait(asio::socket_base::wait_read, [this](const std::error_code& ec) {
    if (ec != std::error_code{}) {
      // The expectation is that if the socket is shutdown then the error code will be aborted. Any error is reason to
      // not continue but in the case where the socket is closed, the error code will be for a bad file descriptor.
      if (ec == asio::error::operation_aborted) {
        return;
      } else {
        throw std::runtime_error(fmt::format("Socket event error: {}", ec.message()));
      }
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
