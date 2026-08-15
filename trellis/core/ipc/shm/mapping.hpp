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

#ifndef TRELLIS_CORE_IPC_SHM_MAPPING_HPP_
#define TRELLIS_CORE_IPC_SHM_MAPPING_HPP_

#include <cstddef>

namespace trellis::core::ipc::shm {

/**
 * @brief An owned mmap'd view of a shared memory file.
 *
 * Mappings are held by shared_ptr so that a holder can keep a region mapped across a remap; the region is unmapped
 * when the last reference drops. Backing files only ever grow and every mapping of a file aliases the same pages, so
 * an older (smaller) mapping stays coherent with the current one for the bytes it covers.
 */
class Mapping {
 public:
  /**
   * @brief Maps `size` bytes of the file into this process' address space.
   *
   * As the owner, the file is grown to `size` first and mapped read-write; otherwise it is mapped read-only. The
   * file descriptor is not retained: the caller keeps ownership of it, and it may be closed after construction
   * (MAP_SHARED mappings stay valid after close).
   *
   * @param fd File descriptor of the shared memory file.
   * @param owner Whether the calling process owns (writes) the file.
   * @param size Size of the region to map in bytes.
   * @throws std::system_error if growing or mapping the file fails.
   */
  Mapping(int fd, bool owner, size_t size);

  /**
   * @brief Unmaps the region.
   */
  ~Mapping();

  Mapping(const Mapping&) = delete;
  Mapping& operator=(const Mapping&) = delete;
  Mapping(Mapping&&) = delete;
  Mapping& operator=(Mapping&&) = delete;

  /**
   * @brief Gets the base address of the mapped region.
   * @return Pointer to the start of the mapping.
   */
  void* Addr() const { return addr_; }

  /**
   * @brief Gets the size of the mapped region.
   * @return Size of the mapping in bytes.
   */
  size_t Size() const { return size_; }

 private:
  void* addr_;
  size_t size_;
};

}  // namespace trellis::core::ipc::shm

#endif  // TRELLIS_CORE_IPC_SHM_MAPPING_HPP_
