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

#include "trellis/core/config.hpp"
#include "trellis/core/logging.hpp"
#include "trellis/utils/umask_guard/umask_guard.hpp"

namespace trellis {
namespace core {

namespace {

const std::string kDefaultMarkerDir = "/tmp/trellis";
const std::string kMarkerDirConfigKey = "trellis.crash_counter.marker_dir";

std::string MarkerDirFromConfig(const Config& config) {
  return config.AsIfExists<std::string>(kMarkerDirConfigKey, kDefaultMarkerDir);
}

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

// Returns std::nullopt for a corrupt marker so callers can choose their own
// fallback and logging (the ctor warns and writes 1; peek silently reports 1).
std::optional<int> DeriveUncleanExitCount(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return 0;
  const auto counter_opt = ReadCounter(path);
  if (!counter_opt.has_value()) return std::nullopt;
  return counter_opt.value() + 1;
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

namespace crash_counter {

int PeekUncleanExitCount(std::string_view marker_dir, std::string_view node_name) {
  return DeriveUncleanExitCount(MarkerPath(marker_dir, node_name)).value_or(1);
}

int PeekUncleanExitCount(const Config& config, std::string_view node_name) {
  return PeekUncleanExitCount(MarkerDirFromConfig(config), node_name);
}

}  // namespace crash_counter

CrashCounter::CrashCounter(std::string_view marker_dir, std::string_view node_name, std::optional<uid_t> uid,
                           std::optional<gid_t> gid)
    : marker_path_{MarkerPath(marker_dir, node_name)}, uid_{uid}, gid_{gid} {
  // umask(000) so the dir is created world-writable (0777) and the marker file
  // world-rw (0666). The marker dir is shared with IPC sockets that may be
  // bound by other UIDs; otherwise the first writer locks everyone else out.
  trellis::utils::UmaskGuard guard(000, uid_, gid_);

  std::error_code ec;
  std::filesystem::create_directories(marker_path_.parent_path(), ec);
  if (ec) {
    Log::Warn("CrashCounter: failed to create directory {}: {}", marker_path_.parent_path().string(), ec.message());
    return;
  }

  const auto count_opt = DeriveUncleanExitCount(marker_path_);
  if (!count_opt.has_value()) {
    Log::Warn("CrashCounter: marker {} unreadable or unparseable; resetting count to 1", marker_path_.string());
  }
  unclean_exit_count_ = count_opt.value_or(1);

  if (!WriteCounterAtomic(marker_path_, unclean_exit_count_)) {
    Log::Warn("CrashCounter: failed to write marker {}", marker_path_.string());
  }
}

CrashCounter::CrashCounter(const Config& config, std::string_view node_name, std::optional<uid_t> uid,
                           std::optional<gid_t> gid)
    : CrashCounter(MarkerDirFromConfig(config), node_name, uid, gid) {}

CrashCounter::~CrashCounter() {
  if (mark_unclean_ || std::uncaught_exceptions() > 0) return;
  trellis::utils::UmaskGuard guard(000, uid_, gid_);
  std::error_code ec;
  std::filesystem::remove(marker_path_, ec);
  if (ec) {
    Log::Warn("CrashCounter: failed to remove marker {}: {}", marker_path_.string(), ec.message());
  }
}

}  // namespace core
}  // namespace trellis
