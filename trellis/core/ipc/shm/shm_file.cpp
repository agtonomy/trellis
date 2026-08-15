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

#include "trellis/core/ipc/shm/shm_file.hpp"

#include <fcntl.h> /* For O_* constants */
#include <fmt/core.h>
#include <sys/mman.h>
#include <sys/stat.h> /* For mode constants */
#include <unistd.h>   /* For sysconf */

#include <cstring>
#include <stdexcept>
#include <system_error>

#include "trellis/core/ipc/named_resource_registry.hpp"
#include "trellis/core/ipc/shm/mapping.hpp"
#include "trellis/core/ipc/utils.hpp"
#include "trellis/utils/umask_guard/umask_guard.hpp"

namespace trellis::core::ipc::shm {

namespace {

int CreateOrOpen(const std::string& handle, const bool owner, const trellis::core::Config& config) {
  const std::string posix_name = handle.starts_with('/') ? handle : "/" + handle;
  const auto flags = owner ? (O_CREAT | O_RDWR | O_EXCL) : O_RDONLY;
  constexpr auto mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

  int rt;
  int err;
  {
    const auto [uid_opt, gid_opt] = trellis::core::ipc::utils::GetUidGidFromConfig(config);
    trellis::utils::UmaskGuard guard(000, uid_opt, gid_opt);
    rt = ::shm_open(posix_name.c_str(), flags, mode);
    err = errno;  // capture before guard restores umask
  }

  // If we're not the owner and the file doesn't exist, return -1 instead of throwing
  if (!owner && rt < 0 && err == ENOENT) {
    return -1;
  }
  // In all other cases, throw
  if (rt < 0) {
    throw std::system_error(err, std::generic_category(), "ShmFile::CreateOrOpen failed " + posix_name);
  }
  return rt;
}

std::shared_ptr<Mapping> Map(const int fd, const bool owner, const size_t requested_size) {
  if (fd < 0) {
    throw std::runtime_error("Call to ShmFile::Map while fd is not open");
  }
  // As a non-owner the only amount of data that is guaranteed is the header. First we map that amount and use the
  // header metadata to determine how large of a region we need to remap.
  const auto map_size = owner ? requested_size + ShmFile::kCombinedHeaderSize : ShmFile::kCombinedHeaderSize;
  auto map = std::make_shared<Mapping>(fd, owner, map_size);

  ShmFile::ShmHeader* header = static_cast<ShmFile::ShmHeader*>(map->Addr());
  if (owner) {
    // As the owner, we will map the size requested
    header->header_size = sizeof(ShmFile::ShmHeader);
    header->cur_data_size = sizeof(ShmFile::SMemFileHeader);
    header->max_data_size = requested_size;
  } else {
    // Each process, whether owner or not, has to decide how large of a region of memory to map into the process'
    // address space. In the case of a non-owner (reader), we use the header metadata to know how much memory to map.
    const auto cur_size = header->cur_data_size + ShmFile::kCombinedHeaderSize;
    map = std::make_shared<Mapping>(fd, owner, cur_size);
  }
  return map;
}

}  // namespace

ShmFile::ShmFile(const std::string& handle, const bool owner, const size_t requested_size,
                 const trellis::core::Config& config)
    : handle_{handle}, owner_{owner}, fd_{CreateOrOpen(handle, owner, config)} {
  if (fd_ >= 0) {
    try {
      map_ = Map(fd_, owner, requested_size);
    } catch (...) {
      // The destructor won't run when the constructor throws, so release the fd and the created segment here
      if (owner_) {
        ::shm_unlink(handle_.c_str());
      }
      ::close(fd_);
      fd_ = -1;
      throw;
    }
  }
  if (owner) {
    NamedResourceRegistry::Get().InsertShm(handle_);
  }
}

ShmFile::~ShmFile() {
  // Ensure we don't unlink or close if we don't own the file descriptor such as if this object was moved
  if (fd_ >= 0) {
    if (owner_) {
      ::shm_unlink(handle_.c_str());
    }
    // MAP_SHARED mappings stay valid after close, so readers keep their memory
    ::close(fd_);
  }
}

ShmFile::ShmFile(ShmFile&& other)
    : handle_{other.handle_},
      owner_{other.owner_},
      fd_{other.fd_},
      map_(std::move(other.map_)),
      send_count_{other.send_count_} {
  other.fd_ = -1;
}

void ShmFile::Resize(const size_t requested_size) {
  std::lock_guard lock(mutex_);
  if (map_ == nullptr) {
    throw std::runtime_error("ShmFile::Resize called while unmapped");
  }
  const auto total_size = kCombinedHeaderSize + requested_size;
  if (total_size < map_->Size()) {
    throw std::logic_error(fmt::format(
        "ShmFile::Resize shrink from {} to {} bytes is not supported: pinned mappings rely on the file only growing",
        map_->Size(), total_size));
  }
  map_ = std::make_shared<Mapping>(fd_, owner_, total_size);
  ShmFile::ShmHeader* header = static_cast<ShmFile::ShmHeader*>(map_->Addr());
  header->max_data_size = requested_size;
}

ShmFile::ReadInfo ShmFile::GetReadInfo() {
  std::lock_guard lock(mutex_);
  if (map_ == nullptr) {
    throw std::runtime_error("ShmFile::GetReadInfo called while unmapped");
  }

  {  // First sanity check header and remap if needed
    const ShmHeader& header = *reinterpret_cast<const ShmHeader*>(static_cast<uint8_t*>(map_->Addr()));
    if (header.header_size != sizeof(ShmHeader)) {
      throw std::logic_error("ShmFile::GetReadInfo Inconsistency in header size!");
    }

    // We have to check the header every time and remap accordingly because the shared memory region size is adjusted at
    // runtime
    if (header.max_data_size + kCombinedHeaderSize > map_->Size()) {
      map_ = std::make_shared<Mapping>(fd_, owner_, header.max_data_size + kCombinedHeaderSize);
    }
  }

  const auto* base = static_cast<const uint8_t*>(map_->Addr());
  const SMemFileHeader& memfile_header = *reinterpret_cast<const SMemFileHeader*>(base + sizeof(ShmHeader));
  return ReadInfo{.data = base + kCombinedHeaderSize, .size = memfile_header.data_size};
}

ShmFile::WriteInfo ShmFile::GetWriteInfo() {
  std::lock_guard lock(mutex_);
  if (map_ == nullptr) {
    throw std::runtime_error("ShmFile::GetWriteInfo called while unmapped");
  }
  // In the case of the writer, we need to return how much size is currently available and then the writer will populate
  // the header with the actual data_size for the reader to parse
  const auto data_size_available = map_->Size() > kCombinedHeaderSize ? map_->Size() - kCombinedHeaderSize : 0u;
  return WriteInfo{.data = static_cast<uint8_t*>(map_->Addr()) + kCombinedHeaderSize, .size = data_size_available};
}

void ShmFile::SetFileHeader(const size_t bytes_written, const unsigned sequence,
                            const trellis::core::time::TimePoint& now, const uint64_t writer_id) {
  auto& file_header = GetMutableFileHeader();
  file_header.hdr_size = sizeof(ShmFile::SMemFileHeader);
  file_header.data_size = bytes_written;
  file_header.sequence = sequence;
  file_header.clock = trellis::core::time::TimePointToNanoseconds(now);
  file_header.writer_id = writer_id;
}

void ShmFile::SetHeader(const size_t bytes_written) {
  GetHeader().cur_data_size = sizeof(ShmFile::SMemFileHeader) + bytes_written;
}

ShmFile::ShmHeader& ShmFile::GetHeader() const {
  if (map_ == nullptr || map_->Size() < sizeof(ShmHeader)) {
    throw std::runtime_error("ShmFile::Header called without enough bytes mapped");
  }
  return *static_cast<ShmHeader*>(map_->Addr());
}

ShmFile::SMemFileHeader& ShmFile::GetMutableFileHeader() const {
  if (map_ == nullptr || map_->Size() < kCombinedHeaderSize) {
    throw std::runtime_error("ShmFile::FileHeader called without enough bytes mapped.");
  }
  return *reinterpret_cast<SMemFileHeader*>(static_cast<uint8_t*>(map_->Addr()) + sizeof(ShmHeader));
}

const ShmFile::SMemFileHeader& ShmFile::GetFileHeader() const { return GetMutableFileHeader(); }

}  // namespace trellis::core::ipc::shm
