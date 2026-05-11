/*
 * Copyright (C) 2026 Agtonomy
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

#include "trellis/core/crash_counter.hpp"

#include <exception>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#include "trellis/core/logging.hpp"

namespace trellis {
namespace core {

namespace {

std::filesystem::path MarkerPath(std::string_view marker_dir, std::string_view node_name) {
  return std::filesystem::path(marker_dir) / (fmt::format("{}_crash_counter", node_name));
}

std::optional<int> ReadCounter(const std::filesystem::path& path) {
  std::ifstream f(path);
  if (!f.is_open()) return std::nullopt;
  int counter = 0;
  f >> counter;
  if (f.fail()) return std::nullopt;
  return counter;
}

bool WriteCounterAtomic(const std::filesystem::path& path, int value) {
  const auto tmp = path.string() + ".tmp";
  {
    std::ofstream f(tmp, std::ios::trunc);
    if (!f.is_open()) return false;
    f << value;
    if (f.fail()) return false;
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::filesystem::remove(tmp, ec);
    return false;
  }
  return true;
}

}  // namespace

CrashCounter::CrashCounter(std::string_view marker_dir, std::string_view node_name)
    : marker_path_{MarkerPath(marker_dir, node_name)} {
  std::error_code ec;
  std::filesystem::create_directories(marker_path_.parent_path(), ec);
  if (ec) {
    Log::Warn("CrashCounter: failed to create directory {}: {}", marker_path_.parent_path().string(), ec.message());
    return;
  }

  const bool marker_exists = std::filesystem::exists(marker_path_, ec);
  if (marker_exists) {
    auto counter_opt = ReadCounter(marker_path_);
    if (counter_opt.has_value()) {
      unclean_exit_count_ = counter_opt.value() + 1;
    } else {
      Log::Warn("CrashCounter: marker {} unreadable or unparseable; resetting count to 1", marker_path_.string());
      unclean_exit_count_ = 1;
    }
  } else {
    unclean_exit_count_ = 0;
  }

  if (!WriteCounterAtomic(marker_path_, unclean_exit_count_)) {
    Log::Warn("CrashCounter: failed to write marker {}", marker_path_.string());
  }
}

CrashCounter::~CrashCounter() {
  if (mark_unclean_ || std::uncaught_exceptions() > 0) return;
  std::error_code ec;
  std::filesystem::remove(marker_path_, ec);
  if (ec) {
    Log::Warn("CrashCounter: failed to remove marker {}: {}", marker_path_.string(), ec.message());
  }
}

}  // namespace core
}  // namespace trellis
