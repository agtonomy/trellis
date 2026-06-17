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

#ifndef TRELLIS_CORE_DISCOVERY_DESCRIPTOR_STORE_HPP_
#define TRELLIS_CORE_DISCOVERY_DESCRIPTOR_STORE_HPP_

#include <sys/types.h>

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "trellis/core/config.hpp"

namespace trellis::core::discovery {

/**
 * @brief Host-local, content-addressed blob store for protobuf descriptors.
 *
 * Schemas are immutable for the life of a publisher, so rather than re-broadcasting the full
 * serialized FileDescriptorSet in every discovery heartbeat, each descriptor is persisted once
 * to a host-local store and discovery samples carry only a small content hash. This is
 * sound because discovery is local-host-only (loopback broadcast): every peer that receives a
 * broadcast can read the same store.
 *
 * Blobs are written once (atomic temp-write + rename) and never unlinked at runtime: a
 * content-addressed file may be referenced by many live publishers, and the store is bounded by
 * the number of distinct schemas on a host. Because the backing directory can be cleared out from
 * under a running process (e.g. systemd-tmpfiles aging out /tmp), the store retains the bytes it
 * has written and Refresh() re-creates any blob that has gone missing; callers drive Refresh() from
 * their periodic heartbeat so a reaped blob is restored before peers fail to resolve its hash.
 *
 * @note for the most part, the class is a general content-addressed byte store. If a second
 * blob type ever needs the same treatment, this can be lifted into a generic store.
 */
class DescriptorStore {
 public:
  /**
   * @brief Construct a store rooted at an explicit directory.
   *
   * @param dir Directory under which descriptor blobs are written.
   * @param uid Optional effective uid for created files/dirs (for cross-UID IPC access).
   * @param gid Optional effective gid for created files/dirs.
   */
  explicit DescriptorStore(std::string dir, std::optional<uid_t> uid = std::nullopt,
                           std::optional<gid_t> gid = std::nullopt);

  /**
   * @brief Construct a store, pulling the directory and uid/gid from config.
   *
   * Directory: `trellis.discovery.descriptor_dir` (default `/tmp/trellis/descriptors`).
   * uid/gid: shared with IPC (`trellis.ipc.uid` / `trellis.ipc.gid`).
   */
  explicit DescriptorStore(const Config& config);

  /**
   * @brief Persist a descriptor blob and return its content hash.
   *
   * Idempotent: identical bytes map to an identical hash and are written at most once.
   *
   * @param fdset_bytes The serialized FileDescriptorSet bytes.
   * @return The content hash on success, or std::nullopt if `fdset_bytes` is empty or the blob
   *         could not be persisted (the failure is logged). A nullopt return means the caller must
   *         not broadcast a hash that no peer can resolve — fall back to inlining the bytes instead.
   */
  std::optional<std::string> Put(const std::string& fdset_bytes);

  /**
   * @brief Resolve a content hash back to the descriptor blob.
   *
   * Returns an in-memory copy if this store wrote the blob via Put(); otherwise reads it from the
   * on-disk store. The in-memory path means a hash that this process published resolves, even if
   * its backing file has since been reaped from disk.
   *
   * @note On-disk reads are not integrity-checked against `hash`; a corrupt blob is returned as-is.
   *
   * @param hash A content hash previously returned by Put().
   * @return The blob bytes, or std::nullopt if the hash is empty or not present.
   */
  std::optional<std::string> Get(const std::string& hash) const;

  /**
   * @brief Re-create any previously-Put blob whose backing file has gone missing.
   *
   * Content-addressed blobs live under a directory that may be cleared out from under a running
   * process (e.g. /tmp reapers). Call this periodically to restore reaped blobs so that peers can
   * keep resolving hashes that are still being broadcast. Idempotent and cheap when nothing is
   * missing (a stat per retained blob).
   */
  void Refresh();

 private:
  DescriptorStore(const Config& config, std::pair<std::optional<uid_t>, std::optional<gid_t>> uid_gid);

  std::filesystem::path PathForHash(const std::string& hash) const;

  /**
   * @brief Write fdset_bytes to the blob for `hash` unless an identical-sized blob already exists.
   * @return True if the blob is present afterwards (already existed or newly written), false on a
   *         persistence failure (logged).
   */
  bool WriteIfAbsent(const std::string& hash, const std::string& fdset_bytes) const;

  const std::filesystem::path dir_;
  const std::optional<uid_t> uid_;
  const std::optional<gid_t> gid_;

  mutable std::mutex cache_mutex_;
  // hash -> bytes for every blob this process Put(). Never evicted: it is the sole copy used to
  // restore reaped blobs in Refresh() and to early-return Get() for self-published hashes.
  std::unordered_map<std::string, std::string> cache_;
};

}  // namespace trellis::core::discovery

#endif  // TRELLIS_CORE_DISCOVERY_DESCRIPTOR_STORE_HPP_
