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

#ifndef TRELLIS_CORE_IPC_SHM_SHM_READER_HPP_
#define TRELLIS_CORE_IPC_SHM_SHM_READER_HPP_

#include <functional>
#include <string>

#include "trellis/core/config.hpp"
#include "trellis/core/ipc/shm/shm_file.hpp"
#include "trellis/core/ipc/shm/shm_read_write_lock.hpp"
#include "trellis/core/ipc/unix/socket_event.hpp"

namespace trellis::core::ipc::shm {

/**
 * @brief A class that receives data from shared memory regions using event-driven notifications.
 *
 * `ShmReader` listens on a Unix domain socket for events indicating that new data has been written
 * into a shared memory region. It uses shared memory file objects to read the data and employs
 * a shared reader-writer lock to synchronize access with the writer. On data receipt, a user-defined
 * callback is invoked with a pointer to the data and its metadata.
 */
class ShmReader {
 public:
  /**
   * @brief Callback function type invoked when new data is received.
   *
   * @param header The metadata header associated with the received data.
   * @param data Pointer to the data buffer in shared memory.
   * @param size Size of the data in bytes.
   */
  using Callback = std::function<void(ShmFile::SMemFileHeader, const void*, size_t)>;

  /**
   * @brief Metrics struct that aggregates metrics from the underlying SocketEvent.
   */
  struct Metrics {
    unix::SocketEvent::Metrics socket_event;  ///< Metrics from the underlying socket event handler.
  };

  /**
   * @brief Factory method to create a new ShmReader instance.
   *
   * @note We use a factory to create the ShmReader because there is post constructor logic that is needed to fully
   * initialize the ShmReader. The ShmReader contains an event handler that contains a weak_ptr to the owning ShmReader.
   *
   * @note See constructor for parameter details. PrivateToken used to prevent direct instantiation.
   */
  template <typename... Args>
  static std::shared_ptr<ShmReader> Create(Args&&... args) {
    // There are multiple ways to use a factory method with a private constructor but this one co-locates all the
    // special code to make it happen and requires the least amount of modification to the owning class
    struct PrivateShmReader : ShmReader {
      explicit PrivateShmReader(Args&&... args) : ShmReader(std::forward<Args>(args)...) {}
    };

    auto reader = std::make_shared<PrivateShmReader>(std::forward<Args>(args)...);

    reader->evt_.AsyncReceive([weak_self = std::weak_ptr<ShmReader>(reader)](unix::SocketEvent::Event event) {
      if (auto self = weak_self.lock()) {
        self->ProcessEvent(event);
      }
    });

    return reader;
  }

  /**
   * @brief Get the current metrics for this ShmReader instance.
   *
   * @return Metrics struct containing nested metrics from the underlying SocketEvent.
   */
  Metrics GetMetrics() const { return {evt_.GetMetrics()}; }

  /**
   * @brief Checks if all shared memory files in this reader are properly initialized.
   *
   * @return true if all shared memory files have valid file descriptors and are mapped; false otherwise.
   */
  bool IsInitialized() const;

  ShmReader(const ShmReader&) = delete;
  ShmReader& operator=(const ShmReader&) = delete;
  ShmReader(ShmReader&&) = delete;
  ShmReader& operator=(ShmReader&&) = delete;

 private:
  /**
   * @brief Constructs a ShmReader instance. Do not call directly, use Create() instead.
   *
   * @param loop The event loop to integrate with for async socket notifications.
   * @param reader_id The unique ID of this reader, used for identifying socket events.
   * @param names A list of shared memory segment names to attach to.
   * @param receive_callback A user-defined callback invoked when new data is available.
   * @param config Configuration object for reading IPC settings (uid/gid).
   */
  ShmReader(trellis::core::EventLoop loop, const std::string& reader_id, const std::vector<std::string>& names,
            Callback receive_callback, const trellis::core::Config& config);

  /**
   * @brief Internal handler for socket events signaling that shared memory has been updated.
   *
   * @param event The socket event containing information about the data source.
   */
  void ProcessEvent(const unix::SocketEvent::Event& event);

  std::vector<ShmFile> files_;              ///< List of shared memory files being monitored.
  std::vector<ShmReadWriteLock> locks_;     ///< Associated locks for synchronizing access to shared memory.
  unix::SocketEvent evt_;                   ///< Socket event handler for receiving notifications.
  Callback receive_callback_;               ///< Callback to invoke when new data is available.
  ShmFile::SMemFileHeader last_header_;     ///< Most recently processed shared memory header.
  size_t num_current_dropped_messages_{0};  ///< Track number of messages dropped in a batch
};

}  // namespace trellis::core::ipc::shm

#endif  // TRELLIS_CORE_IPC_SHM_SHM_READER_HPP_
