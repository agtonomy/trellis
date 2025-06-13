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
#include <mutex>
#include <string>

namespace trellis::core::ipc::shm {

/**
 * @brief Represents a shared memory mapped region backed by a named file.
 */
class ShmFile {
 public:
  /**
   * @brief Holds address and size information about a memory-mapped region.
   */
  struct MapInfo {
    void* addr{nullptr};  ///< Pointer to the mapped memory region.
    size_t size{0};       ///< Size of the mapped region.
  };

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
    uint64_t data_size = 0;                      ///< Size of the payload data.
    uint64_t sequence = 0;                       ///< Sequence number for versioning.
    uint64_t clock = 0;                          ///< Timestamp or clock value.
    uint64_t writer_id = 0;                      ///< ID of the writer process.
  };

  /**
   * @brief Total size of both headers combined.
   */
  static constexpr size_t kCombinedHeaderSize = sizeof(ShmHeader) + sizeof(SMemFileHeader);

  /**
   * @brief Constructs a shared memory file wrapper.
   *
   * @param handle The name of the shared memory object.
   * @param owner Whether this instance owns the shared memory (creator).
   * @param size Size of the memory region to allocate (ignored if not owner).
   */
  ShmFile(std::string handle, bool owner, size_t size);

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
  ShmFile(ShmFile&&);

  ShmFile& operator=(ShmFile&&) = delete;

  /**
   * @brief Checks if the shared memory region is currently mapped.
   * @return true if memory is mapped; false otherwise.
   */
  bool Mapped() const { return map_.addr != nullptr; }

  /**
   * @brief Get the pointer and length to the shared memory buffer for the purpose of reading
   * @return A ReadInfo structure containing the data pointer and size.
   */
  ReadInfo GetReadInfo();

  /**
   * @brief Gets the handle (name) of the shared memory object.
   * @return Constant reference to the handle string.
   */
  const std::string& Handle() const { return handle_; }

  /**
   * @brief Returns a copy of the current shared memory header.
   * @return A copy of the ShmHeader structure.
   */
  ShmHeader Header();

  /**
   * @brief Returns a reference to the shared memory header for modification.
   * @return Reference to the ShmHeader.
   */
  ShmHeader& MutableHeader();

  /**
   * @brief Returns a copy of the file header metadata.
   * @return A copy of the SMemFileHeader structure.
   */
  SMemFileHeader FileHeader();

  /**
   * @brief Returns a reference to the file header for modification.
   * @return Reference to the SMemFileHeader.
   */
  SMemFileHeader& MutableFileHeader();

  /**
   * @brief Resizes the shared memory region.
   * @param size New size for the shared memory.
   */
  void Resize(size_t size);

  /**
   * @brief Returns a buffer and size for writing new data.
   * @return A WriteInfo structure containing writable buffer and size.
   */
  WriteInfo GetWriteInfo();

 private:
  std::string handle_;      ///< Name/handle of the shared memory object.
  bool owner_;              ///< True if this instance created the shared memory.
  int fd_{-1};              ///< File descriptor backing the shared memory.
  MapInfo map_;             ///< Memory mapping information.
  unsigned send_count_{0};  ///< Number of times data has been sent.
  std::mutex mutex_;        ///< Mutex for thread-safe access.
};

}  // namespace trellis::core::ipc::shm

#endif  // TRELLIS_CORE_IPC_SHM_SHM_FILE_HPP_
