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

#ifndef TRELLIS_CORE_IPC_SHM_SHM_WRITER_HPP_
#define TRELLIS_CORE_IPC_SHM_SHM_WRITER_HPP_

#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include "trellis/core/ipc/shm/shm_file.hpp"
#include "trellis/core/ipc/shm/shm_read_write_lock.hpp"
#include "trellis/core/ipc/unix/socket_event.hpp"
#include "trellis/core/time.hpp"

namespace trellis::core::ipc::shm {

/**
 * @brief A class that manages writing to shared memory and notifying subscribed readers.
 *
 * The `ShmWriter` class is responsible for writing data to a pool of shared memory buffers,
 * protecting access with reader-writer locks, and notifying readers of new data via Unix socket
 * events. It supports multiple buffers to prevent contention and allow concurrent reads.
 */
class ShmWriter {
 public:
  /// Container type for the set of shared memory file buffers.
  using FilesContainer = std::vector<ShmFile>;

  /// Container type for the named reader-writer locks, keyed by buffer name.
  using ReadWriteLocksContainer = std::unordered_map<std::string, ShmReadWriteLock>;

  /**
   * @brief Constructs a `ShmWriter` instance.
   *
   * @param node_name Logical name of this node (typically an app name). Will be used in naming the generated files.
   * @param loop Event loop used to send notifications.
   * @param pid The process ID of the writer.
   * @param num_buffers Number of shared memory buffers to use.
   * @param buffer_size Initial size in bytes for each shared memory buffer.
   */
  ShmWriter(std::string_view node_name, trellis::core::EventLoop loop, int pid, size_t num_buffers, size_t buffer_size);

  ShmWriter(const ShmWriter&) = delete;
  ShmWriter& operator=(const ShmWriter&) = delete;
  ShmWriter(ShmWriter&&) = delete;
  ShmWriter& operator=(ShmWriter&&) = delete;

  /**
   * @brief Acquires access to a shared memory buffer for writing.
   *
   * The returned buffer is locked for exclusive access by the writer until
   * `ReleaseWriteAccess` is called. If the shared memory buffer is less than the given minimum size, it will be resized
   * automatically.
   *
   * @param minimum_size Minimum size in bytes required for the write operation.
   * @return WriteInfo Struct containing pointer to buffer and available size.
   */
  ShmFile::WriteInfo GetWriteAccess(size_t minimum_size);

  /**
   * @brief Releases the previously acquired write buffer and optionally notifies readers.
   *
   * This function unlocks the write lock and, if the write was successful, signals
   * subscribed readers with a socket event.
   *
   * @param now The timestamp of the write operation.
   * @param bytes_written Number of bytes written to the buffer.
   * @param success Whether the write was successful (used to determine if readers should be notified).
   */
  void ReleaseWriteAccess(const trellis::core::time::TimePoint& now, size_t bytes_written, bool success);

  /**
   * @brief Registers a reader by its process ID.
   *
   * This adds a socket event destination so that the reader can be notified when new data is available.
   *
   * @param reader_id Unique ID of the reader to add.
   */
  void AddReader(const std::string& reader_id);

  /**
   * @brief Unregisters a reader by its process ID.
   *
   * Removes the reader's socket notification target.
   *
   * @param reader_id Unique ID of the reader to add.
   */
  void RemoveReader(const std::string& reader_id);

  /**
   * @brief Returns the memory file prefix used by this writer.
   *
   * This is the base name used for all shared memory files (without buffer index suffix).
   *
   * @return The memory file prefix string.
   */
  const std::string& GetMemoryFilePrefix() const;

  /**
   * @brief Returns the number of buffers used by this writer.
   *
   * @return The buffer count.
   */
  uint32_t GetBufferCount() const;

 private:
  /**
   * @brief Sends a notification event to all readers for a specific buffer index.
   *
   * @param buffer_index The index of the buffer that was written to.
   */
  void SignalWriteEvent(unsigned buffer_index);

  trellis::core::EventLoop loop_;  ///< Event loop used for asynchronous reader notifications.
  int64_t writer_id_;              ///< Unique ID for the writer instance.
  const std::string base_name_;    ///< Base name used for generating shared memory file handles.
  FilesContainer files_;           ///< Set of shared memory file buffers.
  ReadWriteLocksContainer locks_;  ///< Lock set for controlling access to each buffer.
  std::unordered_map<std::string, unix::SocketEvent>
      events_{};              ///< Active reader socket event mappings keyed by reader ID.
  unsigned buffer_index_{0};  ///< Current buffer index for round-robin writing.
  unsigned sequence_{0};      ///< Monotonic sequence number for write tracking.
};

}  // namespace trellis::core::ipc::shm

#endif  // TRELLIS_CORE_IPC_SHM_SHM_WRITER_HPP_
