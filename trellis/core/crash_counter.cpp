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
#include <map>
#include <mutex>
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

// Counts the previous run's exit and leaves a marker for this one. Returns the count for this start.
int WriteMarker(const std::filesystem::path& marker_path, std::optional<uid_t> uid, std::optional<gid_t> gid) {
  // umask(000) so the dir is created world-writable (0777) and the marker file
  // world-rw (0666). The marker dir is shared with IPC sockets that may be
  // bound by other UIDs; otherwise the first writer locks everyone else out.
  trellis::utils::UmaskGuard guard(000, uid, gid);

  std::error_code ec;
  std::filesystem::create_directories(marker_path.parent_path(), ec);
  if (ec) {
    Log::Warn("CrashCounter: failed to create directory {}: {}", marker_path.parent_path().string(), ec.message());
    return 0;
  }

  const auto count_opt = DeriveUncleanExitCount(marker_path);
  if (!count_opt.has_value()) {
    Log::Warn("CrashCounter: marker {} unreadable or unparseable; resetting count to 1", marker_path.string());
  }
  const int unclean_exit_count = count_opt.value_or(1);

  if (!WriteCounterAtomic(marker_path, unclean_exit_count)) {
    Log::Warn("CrashCounter: failed to write marker {}", marker_path.string());
  }
  return unclean_exit_count;
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

/// One run of a marker in this process. The first counter for the marker reads and rewrites it; the last one gone
/// removes it, unless any of them exited uncleanly.
struct CrashCounter::Run {
  Run(std::filesystem::path path, std::optional<uid_t> uid, std::optional<gid_t> gid)
      : marker_path{std::move(path)}, uid{uid}, gid{gid}, unclean_exit_count{WriteMarker(marker_path, uid, gid)} {}

  void RemoveMarker() const {
    trellis::utils::UmaskGuard guard(000, uid, gid);
    std::error_code ec;
    std::filesystem::remove(marker_path, ec);
    if (ec) {
      Log::Warn("CrashCounter: failed to remove marker {}: {}", marker_path.string(), ec.message());
    }
  }

  const std::filesystem::path marker_path;
  const std::optional<uid_t> uid;
  const std::optional<gid_t> gid;
  const int unclean_exit_count;  ///< Fixed at construction, so read without Runs::mutex
  int counters{0};               ///< Live CrashCounters sharing this run. Guarded by Runs::mutex
  bool unclean{false};           ///< Whether any of them exited uncleanly. Guarded by Runs::mutex
};

/// This process's live runs, by marker path. A run starts and ends under the mutex, so a new one never reads a marker
/// that an ending one is about to remove. That serializes the marker I/O of every counter in the process, whatever
/// its marker, which is acceptable only because counters are built and destroyed as rarely as nodes. Node-based, so a
/// counter's pointer to its run survives other insertions.
struct CrashCounter::Runs {
  std::mutex mutex;
  std::map<std::filesystem::path, Run> by_path;
};

CrashCounter::Runs& CrashCounter::GetRuns() {
  static Runs& runs = *new Runs;  // leaked: a counter may be destroyed during static destruction
  return runs;
}

CrashCounter::CrashCounter(std::string_view marker_dir, std::string_view node_name, std::optional<uid_t> uid,
                           std::optional<gid_t> gid) {
  auto& runs = GetRuns();
  const std::lock_guard lock{runs.mutex};
  const auto path = MarkerPath(marker_dir, node_name);
  // Constructs the run only for the first counter of the marker; a throw from that constructor inserts nothing
  auto& run = runs.by_path.try_emplace(path, path, uid, gid).first->second;
  ++run.counters;
  run_ = &run;
}

CrashCounter::CrashCounter(const Config& config, std::string_view node_name, std::optional<uid_t> uid,
                           std::optional<gid_t> gid)
    : CrashCounter(MarkerDirFromConfig(config), node_name, uid, gid) {}

CrashCounter::~CrashCounter() {
  auto& runs = GetRuns();
  const std::lock_guard lock{runs.mutex};
  if (mark_unclean_ || std::uncaught_exceptions() > 0) {
    run_->unclean = true;
  }
  if (--run_->counters > 0) {
    return;
  }
  if (!run_->unclean) {
    run_->RemoveMarker();
  }
  // By iterator: the key would be a reference into the very node being erased
  runs.by_path.erase(runs.by_path.find(run_->marker_path));
}

int CrashCounter::UncleanExitCount() const { return run_->unclean_exit_count; }

}  // namespace core
}  // namespace trellis
