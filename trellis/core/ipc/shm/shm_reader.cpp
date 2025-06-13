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

#include "trellis/core/ipc/shm/shm_reader.hpp"

#include <fmt/core.h>

#include <regex>

namespace trellis::core::ipc::shm {

namespace {

std::string StripTrailingIndex(const std::string& input) {
  static const std::regex suffix_pattern("^(.*)_\\d+$");
  std::smatch match;
  if (std::regex_match(input, match, suffix_pattern)) {
    return match[1];
  }
  return input;
}

std::string GenerateMutexName(const std::string& name) { return name + "_mtx"; }

std::string GenerateEventSocketName(const std::vector<std::string>& names, const std::string& reader_id) {
  const std::string socket_name = fmt::format("/tmp/{}_{}_evt.sock", StripTrailingIndex(names.at(0)), reader_id);
  return socket_name;
}

std::vector<ShmReadWriteLock> CreateReaderWriterLocks(const std::vector<std::string>& names) {
  std::vector<ShmReadWriteLock> locks;
  for (const auto& name : names) {
    locks.emplace_back(ShmReadWriteLock(GenerateMutexName(name), /* owner = */ false));
  }
  return locks;
}

std::vector<ShmFile> CreateShmFiles(const std::vector<std::string>& names) {
  std::vector<ShmFile> files;
  for (const auto& name : names) {
    files.emplace_back(name, /* owner = */ false, 0);
  }
  return files;
}

}  // namespace

ShmReader::ShmReader(trellis::core::EventLoop loop, const std::string& reader_id, const std::vector<std::string>& names,
                     Callback receive_callback)
    : files_(CreateShmFiles(names)),
      locks_(CreateReaderWriterLocks(names)),
      evt_(unix::SocketEvent(loop, /* reader = */ true, GenerateEventSocketName(names, reader_id))),
      receive_callback_{std::move(receive_callback)} {
  evt_.AsyncReceive([this](unix::SocketEvent::Event event) { ProcessEvent(event); });
}

void ShmReader::ProcessEvent(const unix::SocketEvent::Event& event) {
  const auto buffer_index = event.buffer_number;
  auto& lock = locks_.at(buffer_index);
  auto& file = files_.at(buffer_index);
  if (lock.LockRead()) {
    auto read_info = file.GetReadInfo();
    const auto header = file.FileHeader();
    // Extra sanity check to make sure we don't call back with old data
    if (header.sequence > last_header_.sequence) {
      if (receive_callback_) receive_callback_(header, read_info.data, read_info.size);
      last_header_ = header;
    } else {
      throw std::logic_error(
          fmt::format("ShmReader::ProcessEvent sequence number of current header {} at buffer index {} is less than or "
                      "equal to the previous sequence number {}",
                      header.sequence, buffer_index, last_header_.sequence));
    }
    lock.Unlock();
  } else {
    throw std::runtime_error(
        "Failed to take reader lock for buffer {}. There is contention with the writer. We must be falling behind!");
  }
}

}  // namespace trellis::core::ipc::shm
