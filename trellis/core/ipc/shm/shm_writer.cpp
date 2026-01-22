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

#include "trellis/core/ipc/shm/shm_writer.hpp"

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <sys/mman.h>

#include <filesystem>
#include <iostream>

#include "trellis/core/logging.hpp"

namespace trellis::core::ipc::shm {

namespace {

std::string GenerateBufferName(const std::string& base_name, size_t buf_num) {
  return fmt::format("{}_{:03}", base_name, buf_num);
}

ShmWriter::FilesContainer CreateBuffers(const std::string& base_name, size_t num_buffers, size_t buffer_size,
                                        const trellis::core::Config& config) {
  ShmWriter::FilesContainer files;
  for (size_t i = 0; i < num_buffers; ++i) {
    files.emplace_back(ShmFile(GenerateBufferName(base_name, i), /* owner = */ true, buffer_size, config));
  }
  return files;
}

ShmWriter::ReadWriteLocksContainer CreateReadWriteLocks(const ShmWriter::FilesContainer& files,
                                                        const trellis::core::Config& config) {
  ShmWriter::ReadWriteLocksContainer locks;
  for (const auto& file : files) {
    const auto& name = file.Handle();
    locks.emplace(std::make_pair(name, ShmReadWriteLock(name + "_mtx", /* owner = */ true, config)));
  }
  return locks;
}

/**
 * @brief Removes all shm files that haves names starting with a prefix and are followed by a number. This is intended
 * to be used when no files with that name prefix are being used.
 *
 * @param file_name_prefix The prefix for the files that will be deleted.
 */
void RemoveSHMFiles(std::string_view file_name_prefix) {
  std::set<std::string> unlinked, failed_to_unlink;

  std::string filename_str;
  for (const auto& file : std::filesystem::directory_iterator("/dev/shm/")) {
    filename_str = file.path().filename().string();
    // the find_first_not_of ensures that we aren't accidentally deleting files from different apps that have a
    // common stem name with this app. For instance, if this app is named foo, this avoids deleting files from a
    // different app named foo_4_thought.
    if (filename_str.starts_with(file_name_prefix) &&
        filename_str.substr(file_name_prefix.size(), std::string::npos).find_first_not_of("_0123456789") ==
            std::string::npos) {
      try {
        if (::shm_unlink(file.path().string().c_str()) == 0 ||
            std::filesystem::remove(file.path())) {  // if unlink fails, rm might succeed
          unlinked.insert(filename_str);
          continue;
        }
      } catch (const std::exception& ex) {
        // all the un-removable files are collected into a single msg so that the log isn't flooded
      }
      failed_to_unlink.insert(filename_str);
    }
  }

  if (!unlinked.empty()) {
    trellis::core::Log::Debug("Removed the following SHM files : {}", fmt::join(unlinked, ", "));
  }
  if (!failed_to_unlink.empty()) {
    trellis::core::Log::Warn("Unable to remove the following SHM files : {}", fmt::join(failed_to_unlink, ", "));
  }
}

}  // namespace

ShmWriter::ShmWriter(std::string_view node_name, trellis::core::EventLoop loop, const int pid, const size_t num_buffers,
                     const size_t buffer_size, const trellis::core::Config& config)
    : loop_{std::move(loop)},
      writer_id_{std::chrono::steady_clock::now().time_since_epoch().count()},
      base_name_{fmt::format("trellis_{}_{}_{}", node_name, pid, writer_id_)},
      config_{config} {
  // It may not be safe to call RemoveSHMFiles after the first time a ShmWriter is created by an application.
  // Subsequence calls might delete shm files that are currently in use by active ShmWriters that were created earlier
  // during the same program's lifetime.
  static std::once_flag remove_shm_once;
  std::call_once(remove_shm_once, RemoveSHMFiles, fmt::format("trellis_{}_", node_name));

  files_ = CreateBuffers(base_name_, num_buffers, buffer_size, config_);
  locks_ = CreateReadWriteLocks(files_, config_);
}

ShmFile::WriteInfo ShmWriter::GetWriteAccess(const size_t minimum_size) {
  if (files_.empty()) {
    return ShmFile::WriteInfo{};
  }
  if (buffer_index_ >= files_.size()) {
    throw std::logic_error("Memory file vector is too small, size = " + std::to_string(files_.size()));
  }

  unsigned retries{0};
  bool got_lock{false};
  while (!got_lock && retries < files_.size()) {
    auto& file = files_[buffer_index_];
    auto& lock = locks_.at(file.Handle());
    got_lock = lock.TryLockWrite();
    if (!got_lock) {
      if (++buffer_index_ >= files_.size()) {
        buffer_index_ = 0;
      }
    }
    ++retries;
  }
  if (!got_lock) {
    throw std::runtime_error("ShmWriter::GetWriteAccess - failed to obtain writer lock for every buffer!");
  }
  auto& file = files_[buffer_index_];
  auto write_info = file.GetWriteInfo();
  if (write_info.data == nullptr) {
    throw std::logic_error("ShmWriter::GetWriteAccess got nullptr from write_info");
  }
  if (write_info.size < minimum_size) {
    file.Resize(minimum_size);
    write_info = file.GetWriteInfo();
  }
  return write_info;
}

void ShmWriter::ReleaseWriteAccess(const trellis::core::time::TimePoint& now, const size_t bytes_written,
                                   const bool success) {
  auto& file = files_[buffer_index_];
  auto& lock = locks_.at(file.Handle());

  if (success) {
    file.SetHeader(bytes_written);
    ++sequence_;
    file.SetFileHeader(bytes_written, sequence_, now, static_cast<uint64_t>(writer_id_));
  }

  lock.Unlock();

  if (success) {
    SignalWriteEvent(buffer_index_);
    if (++buffer_index_ >= files_.size()) {
      buffer_index_ = 0;
    }
  }
}

void ShmWriter::AddReader(const std::string& reader_id) {
  if (!events_.contains(reader_id)) {
    const auto event_handle = fmt::format("/tmp/trellis/{}_{}_evt.sock", base_name_, reader_id);
    events_.emplace(std::piecewise_construct, std::forward_as_tuple(reader_id),
                    std::forward_as_tuple(loop_, /* reader = */ false, event_handle, config_));
  }
}

void ShmWriter::RemoveReader(const std::string& reader_id) {
  if (events_.contains(reader_id)) {
    events_.erase(reader_id);
  }
}

void ShmWriter::SignalWriteEvent(const unsigned buffer_index) {
  for (auto it = events_.begin(); it != events_.end();) {
    if (!it->second.Send(unix::SocketEvent::Event{.buffer_number = buffer_index})) {
      it = events_.erase(it);
    } else {
      ++it;
    }
  }
}

const std::string& ShmWriter::GetMemoryFilePrefix() const { return base_name_; }

uint32_t ShmWriter::GetBufferCount() const { return static_cast<uint32_t>(files_.size()); }

}  // namespace trellis::core::ipc::shm
