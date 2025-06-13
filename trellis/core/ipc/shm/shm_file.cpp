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

namespace trellis::core::ipc::shm {

namespace {

int CreateOrOpen(std::string handle, bool owner) {
  const std::string posix_name = handle.size() ? ((handle[0] != '/') ? "/" + handle : handle) : handle;
  const auto flags = owner ? (O_CREAT | O_RDWR | O_EXCL) : O_RDONLY;
  const auto mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
  const int previous_umask =
      umask(000);  // set umask to nothing, so we can create files with all possible permission bits
  const int rt = ::shm_open(posix_name.c_str(), flags, mode);
  const auto err = errno;
  umask(previous_umask);  // reset umask to previous permissions
  if (rt < 0) {
    throw std::system_error(err, std::generic_category(), "ShmFile::CreateOrOpen failed " + posix_name);
  }
  return rt;
}

void Unmap(ShmFile::MapInfo map) {
  if (map.addr != nullptr) {
    ::munmap(map.addr, map.size);
  }
}

ShmFile::MapInfo Remap(int& fd, std::string handle, bool owner, size_t requested_size, ShmFile::MapInfo map) {
  if (map.addr == nullptr) {
    throw std::runtime_error("Attempt to remap with null map");
  }
  if (map.addr != nullptr) {
    Unmap(map);
    map.size = requested_size;
    const int prot = owner ? PROT_READ | PROT_WRITE : PROT_READ;
    if (owner) {
      if (::ftruncate(fd, map.size) == -1) {
        ::close(fd);
        throw std::system_error(errno, std::generic_category(), "ShmFile::Map ftruncate failed");
      }
    }

    map.addr = ::mmap(nullptr, map.size, prot, MAP_SHARED, fd, 0);
    if (map.addr == MAP_FAILED) {
      throw std::system_error(errno, std::generic_category(), "ShmFile::Remap failed");
    }
  }
  return map;
}

ShmFile::MapInfo Map(int fd, std::string handle, bool owner, size_t requested_size) {
  ShmFile::MapInfo map{};
  if (fd < 0) {
    throw std::runtime_error("Call to ShmFile::Map while fd is not open");
  }
  // As a non-owner the only amount of data that is guaranteed is the header. First we map that amount and use the
  // header metadata to determine how large of a region we need to remap.
  map.size = owner ? requested_size + ShmFile::kCombinedHeaderSize : ShmFile::kCombinedHeaderSize;
  const int prot = owner ? PROT_READ | PROT_WRITE : PROT_READ;
  if (owner) {
    if (::ftruncate(fd, map.size) == -1) {
      ::close(fd);
      throw std::system_error(errno, std::generic_category(), "ShmFile::Map ftråuncate failed");
    }
  }

  map.addr = ::mmap(nullptr, map.size, prot, MAP_SHARED, fd, 0);
  if (map.addr == MAP_FAILED) {
    throw std::system_error(errno, std::generic_category(), "ShmFile::Map failed");
  }

  ShmFile::ShmHeader* header = reinterpret_cast<ShmFile::ShmHeader*>(map.addr);
  if (owner) {
    // As the owner, we will map the size requested
    header->header_size = sizeof(ShmFile::ShmHeader);
    header->cur_data_size = sizeof(ShmFile::SMemFileHeader);
    header->max_data_size = requested_size;
  } else {
    // Each process, whether owner or not has a to decide how large of a region of memory to map into the process'
    // address space. In the case of a non-owner (reader), we use the header metadata to know how much memory to map.
    const auto cur_size = header->cur_data_size + ShmFile::kCombinedHeaderSize;
    map = Remap(fd, handle, owner, cur_size, map);
  }
  return map;
}

}  // namespace

ShmFile::ShmFile(std::string handle, bool owner, size_t requested_size)
    : handle_{handle}, owner_{owner}, fd_{CreateOrOpen(handle, owner)}, map_{Map(fd_, handle_, owner, requested_size)} {
  if (owner) {
    NamedResourceRegistry::Get().InsertShm(handle_);
  }
}

ShmFile::~ShmFile() {
  Unmap(map_);
  // Ensure we don't unlink if we don't own the file descriptor such as if this object was moved
  if (owner_ && fd_ >= 0) {
    ::shm_unlink(handle_.c_str());
  }
}

ShmFile::ShmFile(ShmFile&& other)
    : handle_{other.handle_},
      owner_{other.owner_},
      fd_{other.fd_},
      map_(std::move(other.map_)),
      send_count_{other.send_count_} {
  other.fd_ = -1;
  other.map_.addr = nullptr;
  other.map_.size = 0;
}

void ShmFile::Resize(size_t requested_size) {
  std::scoped_lock<std::mutex> lock(mutex_);
  const auto total_size = kCombinedHeaderSize + requested_size;
  map_ = Remap(fd_, handle_, owner_, total_size, map_);
  if (map_.size != total_size) {
    throw std::runtime_error(
        fmt::format("ShmFile::Resize Failed to resize mapped memory region to {} bytes", total_size));
  }
  ShmFile::ShmHeader* header = reinterpret_cast<ShmFile::ShmHeader*>(map_.addr);
  header->max_data_size = requested_size;
}

ShmFile::ReadInfo ShmFile::GetReadInfo() {
  std::scoped_lock<std::mutex> lock(mutex_);
  if (map_.addr == nullptr) {
    throw std::runtime_error("ShmFile::GetReadInfo map_.addr == nulllptr");
  }

  {  // First sanity check header and remap if needed
    const ShmHeader& header = *reinterpret_cast<const ShmHeader*>(static_cast<uint8_t*>(map_.addr));
    if (header.header_size != sizeof(ShmHeader)) {
      throw std::logic_error("ShmFile::GetReadInfo Inconsistency in header size!");
    }

    // We have to check the header every time and remap accordingly because the shared memory region size is adjusted at
    // runtime
    if (header.max_data_size + kCombinedHeaderSize > map_.size) {
      map_ = Remap(fd_, handle_, owner_, header.max_data_size + kCombinedHeaderSize, map_);
      if (map_.addr == nullptr) {
        throw std::runtime_error("ShmFile::GetReadInfo Failed to remap to the given data size");
      }
    }
  }

  const SMemFileHeader& memfile_header =
      *reinterpret_cast<const SMemFileHeader*>(static_cast<uint8_t*>(map_.addr) + sizeof(ShmHeader));
  return ReadInfo{.data = static_cast<uint8_t*>(map_.addr) + kCombinedHeaderSize, .size = memfile_header.data_size};
}

ShmFile::WriteInfo ShmFile::GetWriteInfo() {
  std::scoped_lock<std::mutex> lock(mutex_);
  // In the case of the writer, we need to return how much size is currently available and then the writer will populate
  // the header with the actual data_size for the reader to parse
  const auto data_size_available = map_.size > kCombinedHeaderSize ? map_.size - kCombinedHeaderSize : 0u;
  return WriteInfo{.data = static_cast<uint8_t*>(map_.addr) + kCombinedHeaderSize, .size = data_size_available};
}

ShmFile::SMemFileHeader ShmFile::FileHeader() { return MutableFileHeader(); }

ShmFile::SMemFileHeader& ShmFile::MutableFileHeader() {
  if (map_.size < kCombinedHeaderSize) {
    throw std::runtime_error("ShmFile::FileHeader called without enough bytes mapped.");
  }
  return *reinterpret_cast<SMemFileHeader*>(static_cast<uint8_t*>(map_.addr) + sizeof(ShmHeader));
}

ShmFile::ShmHeader ShmFile::Header() { return MutableHeader(); }

ShmFile::ShmHeader& ShmFile::MutableHeader() {
  if (map_.size < sizeof(ShmHeader)) {
    throw std::runtime_error("ShmFile::Header called without enough bytes mapped");
  }
  return *reinterpret_cast<ShmHeader*>(map_.addr);
}

}  // namespace trellis::core::ipc::shm
