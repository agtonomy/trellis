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

namespace trellis::core::ipc::shm {

namespace {

std::string GenerateBufferName(const std::string& base_name, size_t buf_num) {
  return fmt::format("{}_{:03}", base_name, buf_num);
}

ShmWriter::FilesContainer CreateBuffers(const std::string& base_name, size_t num_buffers, size_t buffer_size) {
  ShmWriter::FilesContainer files;
  for (size_t i = 0; i < num_buffers; ++i) {
    files.emplace_back(ShmFile(GenerateBufferName(base_name, i), /* owner = */ true, buffer_size));
  }
  return files;
}

ShmWriter::ReadWriteLocksContainer CreateReadWriteLocks(const ShmWriter::FilesContainer& files) {
  ShmWriter::ReadWriteLocksContainer locks;
  for (const auto& file : files) {
    const auto& name = file.Handle();
    locks.emplace(std::make_pair(name, ShmReadWriteLock(name + "_mtx", /* owner = */ true)));
  }
  return locks;
}

}  // namespace

ShmWriter::ShmWriter(trellis::core::EventLoop loop, int pid, size_t num_buffers, size_t buffer_size)
    : loop_{loop},
      writer_id_{std::chrono::steady_clock::now().time_since_epoch().count()},
      base_name_{fmt::format("trellis_publisher_{}_{}", pid, writer_id_)},
      files_(CreateBuffers(base_name_, num_buffers, buffer_size)),
      locks_(CreateReadWriteLocks(files_)) {}

ShmFile::WriteInfo ShmWriter::GetWriteAccess(size_t minimum_size) {
  ShmFile::WriteInfo info{};
  if (files_.empty()) {
    return info;
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

void ShmWriter::ReleaseWriteAccess(const trellis::core::time::TimePoint& now, size_t bytes_written, bool success) {
  auto& file = files_[buffer_index_];
  auto& lock = locks_.at(file.Handle());

  if (success) {
    auto& header = file.MutableHeader();
    header.cur_data_size = sizeof(ShmFile::SMemFileHeader) + bytes_written;

    ++sequence_;

    auto& file_header = file.MutableFileHeader();
    file_header.hdr_size = sizeof(ShmFile::SMemFileHeader);
    file_header.data_size = bytes_written;
    file_header.sequence = sequence_;
    file_header.clock = time::TimePointToNanoseconds(now);
    file_header.writer_id = static_cast<uint64_t>(writer_id_);
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
  if (events_.find(reader_id) == events_.end()) {
    const auto event_handle = fmt::format("/tmp/trellis/{}_{}_evt.sock", base_name_, reader_id);
    events_.emplace(std::piecewise_construct, std::forward_as_tuple(reader_id),
                    std::forward_as_tuple(loop_, /* reader = */ false, event_handle));
  }
}

void ShmWriter::RemoveReader(const std::string& reader_id) {
  if (events_.find(reader_id) != events_.end()) {
    events_.erase(reader_id);
  }
}

void ShmWriter::SignalWriteEvent(unsigned buffer_index) {
  for (auto it = events_.begin(); it != events_.end();) {
    if (!it->second.Send(unix::SocketEvent::Event{.buffer_number = buffer_index})) {
      it = events_.erase(it);
    } else {
      ++it;
    }
  }
}

std::vector<std::string> ShmWriter::GetMemoryFileList() const {
  std::vector<std::string> list;
  for (const auto& file : files_) {
    list.push_back(file.Handle());
  }
  return list;
}

}  // namespace trellis::core::ipc::shm
