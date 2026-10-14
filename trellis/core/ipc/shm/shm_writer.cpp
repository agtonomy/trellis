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

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <exception>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <string_view>

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
    locks.emplace(
        std::make_pair(name, ShmReadWriteLock(ShmReadWriteLock::GenerateLockName(name), /* owner = */ true, config)));
  }
  return locks;
}

/**
 * @brief The pid in a writer's buffer or lock name, given the part after `trellis_<node>_`.
 *
 * That part is `<pid>_<writer id>_<NNN>`, optionally followed by the lock suffix. Requiring exactly three numeric
 * parts keeps out the objects of a node named `<node>_<digits>`, which share the prefix.
 */
std::optional<pid_t> OwnerPid(std::string_view suffix) {
  if (suffix.ends_with(ShmReadWriteLock::kLockSuffix)) {
    suffix.remove_suffix(ShmReadWriteLock::kLockSuffix.size());
  }
  const size_t first = suffix.find('_');
  const size_t second = first == std::string_view::npos ? first : suffix.find('_', first + 1);
  if (second == std::string_view::npos || suffix.find('_', second + 1) != std::string_view::npos) {
    return std::nullopt;
  }
  const auto is_number = [](std::string_view part) {
    return !part.empty() && part.find_first_not_of("0123456789") == std::string_view::npos;
  };
  const std::string_view pid_part = suffix.substr(0, first);
  if (!is_number(pid_part) || !is_number(suffix.substr(first + 1, second - first - 1)) ||
      !is_number(suffix.substr(second + 1))) {
    return std::nullopt;
  }
  pid_t pid{};
  if (std::from_chars(pid_part.data(), pid_part.data() + pid_part.size(), pid).ec != std::errc{}) {
    return std::nullopt;  // too large to be a pid
  }
  return pid;
}

/**
 * @brief Removes the shm buffers and locks with a name prefix that writers which have since exited left behind.
 *
 * Runs before this process creates its first writer, so an object that carries this process's own pid was left by an
 * earlier process that had the same pid, as happens when a container restarts. A process of another user counts as
 * alive (`kill` fails with `EPERM`, not `ESRCH`).
 *
 * @param file_name_prefix `trellis_<node>_`, the prefix of every object this node's writers create.
 */
void RemoveSHMFiles(std::string_view file_name_prefix) {
  std::set<std::string> unlinked, failed_to_unlink;

  std::string filename_str;
  for (const auto& file : std::filesystem::directory_iterator("/dev/shm/")) {
    filename_str = file.path().filename().string();
    if (!filename_str.starts_with(file_name_prefix)) {
      continue;
    }
    std::string_view suffix{filename_str};
    suffix.remove_prefix(file_name_prefix.size());
    const auto pid = OwnerPid(suffix);
    if (!pid.has_value()) {
      continue;
    }
    if (*pid != ::getpid() && !(::kill(*pid, 0) == -1 && errno == ESRCH)) {
      continue;  // its writer may still be using it
    }

    try {
      // shm_unlink takes a bare shm name, not a path; it prepends /dev/shm/ itself
      if (::shm_unlink(filename_str.c_str()) == 0 ||
          std::filesystem::remove(file.path())) {  // if unlink fails, rm might succeed
        unlinked.insert(filename_str);
        continue;
      }
    } catch (const std::exception& ex) {
      // all the un-removable files are collected into a single msg so that the log isn't flooded
    }
    failed_to_unlink.insert(filename_str);
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
    // Nothing was locked and no generation window was opened, so this return must not be paired with a release
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
    // Readers hold every slot. That is contention, not an error: the caller skips this message
    return ShmFile::WriteInfo{};
  }
  auto& file = files_[buffer_index_];
  auto& lock = locks_.at(file.Handle());
  try {
    auto write_info = file.GetWriteInfo();
    if (write_info.data == nullptr) {
      throw std::logic_error("ShmWriter::GetWriteAccess got nullptr from write_info");
    }
    if (write_info.size < minimum_size) {
      file.Resize(minimum_size);
      write_info = file.GetWriteInfo();
    }
    file.BeginWriteGeneration();
    return write_info;
  } catch (...) {
    // Release the lock here to prevent leaks before forwarding the exception.
    lock.Unlock();
    throw;
  }
}

void ShmWriter::ReleaseWriteAccess(const trellis::core::time::TimePoint& now, const size_t bytes_written,
                                   const bool success) {
  auto& file = files_.at(buffer_index_);
  auto& lock = locks_.at(file.Handle());

  {  // The header writes can throw; the lock has to be released and the seqlock window closed either way
    ShmReadWriteLock::UnlockGuard guard{lock};
    try {
      if (success) {
        file.SetHeader(bytes_written);
        // Advance the counter only once the stamp is durable, so a throw cannot burn a sequence number that
        // subscribers would then report as a dropped message
        file.SetFileHeader(bytes_written, sequence_ + 1, now, static_cast<uint64_t>(writer_id_));
        ++sequence_;
      } else {
        // The abandoned write may have clobbered the payload the committed header still describes
        file.InvalidateSequence();
      }
    } catch (...) {
      // A half-stamped header leaves the same hazard as an abandoned write. This throws only when the slot is
      // unmapped, in which case the generation counter below is unreachable anyway
      file.InvalidateSequence();
      // A skipped increment would invert the slot's parity for its lifetime
      file.EndWriteGeneration();
      throw;
    }
    // Runs even on the abandoned-write path: the caller may have partially clobbered the previous contents, so any
    // outstanding borrow of them has to be invalidated.
    file.EndWriteGeneration();
  }

  if (success) {
    SignalWriteEvent(buffer_index_);
    if (++buffer_index_ >= files_.size()) {
      buffer_index_ = 0;
    }
  }
}

ShmWriter::WriteAccessGuard::~WriteAccessGuard() {
  try {
    writer_.ReleaseWriteAccess(write_time_, bytes_written_, success_);
  } catch (const std::exception& ex) {
    trellis::core::Log::Error("ShmWriter::WriteAccessGuard failed to release write access: {}", ex.what());
  } catch (...) {
    trellis::core::Log::Error("ShmWriter::WriteAccessGuard failed to release write access: unknown exception");
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
