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

#include "trellis/core/ipc/shm/mapping.hpp"

#include <fmt/core.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <system_error>

namespace trellis::core::ipc::shm {

Mapping::Mapping(const int fd, const bool owner, const size_t size) : size_{size} {
  if (owner) {
    if (::ftruncate(fd, size) == -1) {
      const int err = errno;  // capture before fmt::format can clobber it
      throw std::system_error(err, std::generic_category(), fmt::format("Mapping ftruncate to {} bytes failed", size));
    }
  }
  const int prot = owner ? PROT_READ | PROT_WRITE : PROT_READ;
  addr_ = ::mmap(nullptr, size, prot, MAP_SHARED, fd, 0);
  if (addr_ == MAP_FAILED) {
    const int err = errno;
    throw std::system_error(err, std::generic_category(), fmt::format("Mapping mmap of {} bytes failed", size));
  }
}

Mapping::~Mapping() { ::munmap(addr_, size_); }

}  // namespace trellis::core::ipc::shm
