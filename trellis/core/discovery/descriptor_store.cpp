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

#include "trellis/core/discovery/descriptor_store.hpp"

#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string_view>
#include <system_error>

#include "trellis/core/ipc/utils.hpp"
#include "trellis/core/logging.hpp"
#include "trellis/utils/umask_guard/umask_guard.hpp"

namespace trellis::core::discovery {

namespace {

constexpr std::string_view kDefaultDescriptorDir = "/tmp/trellis/descriptors";
constexpr std::string_view kDescriptorDirConfigKey = "trellis.discovery.descriptor_dir";

// 128-bit FNV-1a over the bytes, rendered as 32 hex chars. Needs a stable cross-process hash for
// content addressing, which rules out absl::Hash (per-process-seeded). Not a cryptographic hash:
// discovery is local-host-only and non-adversarial, so crafted-collision resistance buys nothing
// here, and 128 bits keeps random collisions negligible without pulling in a crypto dependency.
std::string Fnv1a128Hex(const std::string& bytes) {
  constexpr __uint128_t kFnvOffsetBasis =
      (static_cast<__uint128_t>(0x6c62272e07bb0142ULL) << 64) | 0x62b821756295c58dULL;
  constexpr __uint128_t kFnvPrime = (static_cast<__uint128_t>(0x0000000001000000ULL) << 64) | 0x000000000000013bULL;
  __uint128_t hash = kFnvOffsetBasis;
  for (const unsigned char byte : bytes) {
    hash ^= static_cast<__uint128_t>(byte);
    hash *= kFnvPrime;
  }
  const uint64_t hi = static_cast<uint64_t>(hash >> 64);
  const uint64_t lo = static_cast<uint64_t>(hash);
  return fmt::format("{:016x}{:016x}", hi, lo);
}

std::filesystem::path UniqueTempPath(const std::filesystem::path& final_path) {
  static std::atomic<uint64_t> counter{0};
  const auto suffix = fmt::format(".{}.{}.tmp", ::getpid(), counter.fetch_add(1));
  return final_path.string() + suffix;
}

}  // namespace

DescriptorStore::DescriptorStore(std::string dir, std::optional<uid_t> uid, std::optional<gid_t> gid)
    : dir_{std::move(dir)}, uid_{uid}, gid_{gid} {}

DescriptorStore::DescriptorStore(const Config& config)
    : DescriptorStore(config, ipc::utils::GetUidGidFromConfig(config)) {}

DescriptorStore::DescriptorStore(const Config& config, std::pair<std::optional<uid_t>, std::optional<gid_t>> uid_gid)
    : DescriptorStore(
          config.AsIfExists<std::string>(std::string(kDescriptorDirConfigKey), std::string(kDefaultDescriptorDir)),
          uid_gid.first, uid_gid.second) {}

std::filesystem::path DescriptorStore::PathForHash(const std::string& hash) const { return dir_ / (hash + ".fdset"); }

std::optional<std::string> DescriptorStore::Put(const std::string& fdset_bytes) {
  if (fdset_bytes.empty()) {
    return std::nullopt;
  }

  const std::string hash = Fnv1a128Hex(fdset_bytes);
  if (!WriteIfAbsent(hash, fdset_bytes)) {
    return std::nullopt;
  }

  // Retain the bytes so Refresh() can re-create the blob if its backing file is later reaped while
  // this process keeps broadcasting the hash.
  std::lock_guard lock(cache_mutex_);
  cache_.insert_or_assign(hash, fdset_bytes);
  return hash;
}

bool DescriptorStore::WriteIfAbsent(const std::string& hash, const std::string& fdset_bytes) const {
  const std::filesystem::path final_path = PathForHash(hash);

  std::error_code ec;
  if (std::filesystem::exists(final_path, ec)) {
    // write-once: trust an existing blob only if its size matches. Content addressing guarantees
    // identical bytes for the same hash, so a size mismatch means a truncated/corrupt prior write;
    // fall through and re-publish to heal it rather than serving the bad blob forever.
    const auto existing_size = std::filesystem::file_size(final_path, ec);
    if (!ec && existing_size == fdset_bytes.size()) {
      return true;
    }
  }

  const std::filesystem::path tmp_path = UniqueTempPath(final_path);
  std::ofstream f;
  {
    // umask(000) so the dir is world-writable and the blob world-readable: the store is shared
    // with non-connecting CLI tools that may run under other UIDs, matching the IPC socket /
    // crash counter convention. The guard is scoped to directory and file creation only; the
    // write and rename below are mode-independent and must not hold the process-wide lock for the
    // duration of a multi-KB write.
    trellis::utils::UmaskGuard guard(000, uid_, gid_);
    std::filesystem::create_directories(dir_, ec);
    if (ec) {
      Log::Warn("DescriptorStore: failed to create directory {}: {}", dir_.string(), ec.message());
      return false;
    }
    f.open(tmp_path, std::ios::binary | std::ios::trunc);
  }
  if (!f.is_open()) {
    Log::Warn("DescriptorStore: failed to open temp file {}", tmp_path.string());
    return false;
  }

  f.write(fdset_bytes.data(), static_cast<std::streamsize>(fdset_bytes.size()));
  f.close();  // close() flushes; checking fail() afterwards catches buffered write/flush errors
  if (f.fail()) {
    std::filesystem::remove(tmp_path, ec);
    Log::Warn("DescriptorStore: failed to write temp file {}", tmp_path.string());
    return false;
  }

  std::filesystem::rename(tmp_path, final_path, ec);
  if (ec) {
    // rename() atomically replaces an existing target, so it does NOT fail merely because a
    // concurrent writer already published final_path (the bytes are identical by construction,
    // so an overwrite is harmless). Reaching here means a genuine error (cross-device link,
    // ENOSPC, permissions): drop our temp and report failure so the caller inlines instead.
    std::filesystem::remove(tmp_path, ec);
    Log::Warn("DescriptorStore: failed to publish {}", final_path.string());
    return false;
  }
  return true;
}

std::optional<std::string> DescriptorStore::Get(const std::string& hash) const {
  if (hash.empty()) {
    return std::nullopt;
  }
  {
    std::lock_guard lock(cache_mutex_);
    if (const auto it = cache_.find(hash); it != cache_.end()) {
      return it->second;
    }
  }
  std::ifstream f(PathForHash(hash), std::ios::binary);
  if (!f.is_open()) {
    return std::nullopt;
  }
  std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (f.bad()) {
    return std::nullopt;
  }
  return bytes;
}

void DescriptorStore::Refresh() {
  std::lock_guard lock(cache_mutex_);
  for (const auto& [hash, bytes] : cache_) {
    WriteIfAbsent(hash, bytes);
  }
}

}  // namespace trellis::core::discovery
