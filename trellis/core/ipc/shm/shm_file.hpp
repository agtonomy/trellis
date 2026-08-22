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

#ifndef TRELLIS_CORE_IPC_SHM_SHM_FILE_HPP_
#define TRELLIS_CORE_IPC_SHM_SHM_FILE_HPP_

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>

#include "trellis/core/config.hpp"
#include "trellis/core/ipc/shm/mapping.hpp"
#include "trellis/core/time.hpp"

namespace trellis::core::ipc::shm {

/**
 * @brief Represents a shared memory mapped region backed by a named file.
 *
 * Not thread-safe by design. The modules that own a ShmFile are responsible for synchronizing access to it: the
 * publisher's mutex on the writer side, the event loop on the reader side. ShmReadWriteLock excludes the peer process
 * and says nothing about threads within this one.
 */
class ShmFile {
 public:
  /**
   * @brief Provides const access to the shared memory region for reading.
   */
  struct ReadInfo {
    const void* data;  ///< Pointer to the data read.
    size_t size;       ///< Size of the data read.
  };

  /**
   * @brief Provides access to the shared memory region for writing.
   */
  struct WriteInfo {
    void* data;
    size_t size;
  };

  /**
   * @brief Header stored at the beginning of shared memory to describe layout and sizes.
   */
  struct ShmHeader {
    std::uint16_t header_size = sizeof(ShmHeader);  ///< Size of this header.
    std::array<std::uint8_t, 6> padding = {};       ///< Padding for 8-byte alignment on 64-bit Linux.
    std::uint64_t cur_data_size = 0;                ///< Current size of valid data.
    std::uint64_t max_data_size = 0;                ///< Maximum allowed data size.
  };

  /**
   * @brief Header used for additional metadata in the shared memory segment.
   */
  struct SMemFileHeader {
    uint16_t hdr_size = sizeof(SMemFileHeader);  ///< Size of this header.
    std::array<std::uint8_t, 6> padding = {};    ///< Padding for 64-bit word alignment.
    uint64_t data_size = 0;                      ///< Size of the payload data.
    uint64_t sequence = 0;                       ///< Version counter; 0 means never written or invalidated.
    uint64_t clock = 0;                          ///< Timestamp or clock value.
    uint64_t writer_id = 0;                      ///< ID of the writer process.
    /// Seqlock counter for this slot: odd while a write is in progress, even at rest. Writers increment it through
    /// std::atomic_ref under the slot's write lock. Readers copy this struct by value through the reader callback,
    /// reading this word non-atomically, which is race-free because they hold the slot's read lock.
    uint64_t generation = 0;
  };

  static_assert(std::is_trivially_copyable_v<SMemFileHeader>);
  // 2 (hdr_size) + 6 (padding) + 8 each for data_size, sequence, clock, writer_id, generation
  static_assert(sizeof(SMemFileHeader) == 48);
  static_assert(std::atomic_ref<uint64_t>::is_always_lock_free);
  // std::atomic_ref requires its referent to be suitably aligned. mmap returns page-aligned addresses, so the
  // generation word is 8-byte aligned iff its offset from the start of the mapping is.
  static_assert((sizeof(ShmHeader) + offsetof(SMemFileHeader, generation)) %
                    std::atomic_ref<uint64_t>::required_alignment ==
                0);

  /**
   * @brief Total size of both headers combined.
   */
  static constexpr size_t kCombinedHeaderSize = sizeof(ShmHeader) + sizeof(SMemFileHeader);

  /**
   * @brief Constructs a shared memory file wrapper.
   *
   * @param handle The name of the shared memory object.
   * @param owner Whether this instance owns the shared memory (creator).
   * @param requested_size Size of the memory region to allocate (ignored if not owner).
   * @param config Configuration object for reading IPC settings (uid/gid).
   */
  ShmFile(const std::string& handle, bool owner, size_t requested_size, const trellis::core::Config& config);

  /**
   * @brief Destructor to clean up resources.
   */
  ~ShmFile();

  ShmFile(const ShmFile&) = delete;
  ShmFile& operator=(const ShmFile&) = delete;

  /**
   * @brief Move constructor.
   * @param other The ShmFile instance to move from.
   */
  ShmFile(ShmFile&& other);

  ShmFile& operator=(ShmFile&&) = delete;

  /**
   * @brief Checks if the shared memory region is currently mapped.
   * @return true if memory is mapped; false otherwise.
   */
  bool Mapped() const { return map_ != nullptr; }

  /**
   * @brief Checks if the shared memory file was properly initialized.
   * @return true if file descriptor is valid and memory is mapped; false otherwise.
   */
  bool IsInitialized() const { return fd_ >= 0 && Mapped(); }

  /**
   * @brief Get the pointer and length to the shared memory buffer for the purpose of reading
   *
   * Remaps first if the writer has grown the region.
   *
   * @return A ReadInfo structure containing the data pointer and size.
   */
  ReadInfo GetReadInfo();

  /**
   * @brief Opens the seqlock window on this slot, marking the payload as being rewritten.
   *
   * Must be called by the writer with this slot's write lock held, after any resize and before the first payload
   * store. The release fence keeps the subsequent payload stores from being reordered ahead of the counter bump; a
   * release operation on the counter alone would not, as release only orders the operations that precede it.
   */
  void BeginWriteGeneration();

  /**
   * @brief Closes the seqlock window on this slot, marking the payload as stable again.
   *
   * Must be called by the writer with this slot's write lock still held, after the payload and both headers are
   * complete.
   */
  void EndWriteGeneration();

  /**
   * @brief Zeroes the committed sequence, marking this slot's message stale.
   *
   * An abandoned or partially stamped write may have clobbered the payload while the header still describes the
   * previously committed message. A lagging reader draining an old event for this slot would pass the header checks
   * and deliver the clobbered bytes. Zeroing the sequence makes such a reader drop the slot as an already-seen
   * message; the next successful write re-stamps the real sequence. The size fields are left alone, so the slot is
   * identified as stale by its sequence rather than by an empty payload.
   *
   * Must be called by the writer with this slot's write lock held.
   */
  void InvalidateSequence();

  /**
   * @brief Gets the handle (name) of the shared memory object.
   * @return Constant reference to the handle string.
   */
  const std::string& Handle() const { return handle_; }

  /**
   * @brief Returns sets the bytes written into the shared memory header.
   * @param bytes_written
   */
  void SetHeader(size_t bytes_written);

  /**
   * @brief Sets the values within the file header.
   * @param bytes_written the number of bytes written
   * @param sequence the monotonic increasing sequence number
   * @param now the current timepoint
   * @param writer_id the unique writer id
   */
  void SetFileHeader(size_t bytes_written, uint64_t sequence, const trellis::core::time::TimePoint& now,
                     uint64_t writer_id);

  /**
   * @brief Grows the shared memory region.
   *
   * Grow-only: shrinking throws. Truncating the file would SIGBUS pinned mappings and lagging readers whose mappings
   * still cover the truncated pages.
   *
   * @param requested_size New size for the shared memory. Must not shrink the region.
   */
  void Resize(size_t requested_size);

  /**
   * @brief Returns a buffer and size for writing new data.
   * @return A WriteInfo structure containing writable buffer and size.
   */
  WriteInfo GetWriteInfo();

  /** getter method for the file header.
   *
   * @return the SMemFileHeader
   */
  const SMemFileHeader& GetFileHeader() const;

 private:
  /** private getter method for the header.
   *
   * @return the ShmHeader from the map_
   */
  ShmHeader& GetHeader() const;

  /** private getter method for the file header.
   *
   * @return the SMemFileHeader from the map_
   */
  SMemFileHeader& GetMutableFileHeader() const;

  std::string handle_;            ///< Name/handle of the shared memory object.
  bool owner_;                    ///< True if this instance created the shared memory.
  int fd_{-1};                    ///< File descriptor backing the shared memory.
  std::shared_ptr<Mapping> map_;  ///< Current memory mapping.
  unsigned send_count_{0};        ///< Number of times data has been sent.
};

}  // namespace trellis::core::ipc::shm

#endif  // TRELLIS_CORE_IPC_SHM_SHM_FILE_HPP_
