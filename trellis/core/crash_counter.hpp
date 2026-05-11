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

#ifndef TRELLIS_CORE_CRASH_COUNTER_HPP_
#define TRELLIS_CORE_CRASH_COUNTER_HPP_

#include <filesystem>
#include <string>
#include <string_view>

namespace trellis {
namespace core {

/**
 * CrashCounter detects whether the previous run of a named node exited cleanly
 * and exposes a count of consecutive unclean exits.
 *
 * Construct as a member of (or alongside) a Node so its lifetime brackets the
 * event loop. A marker file at /tmp/trellis/<node_name>_crash_counter is
 * consulted on construction: if present, the previous run did not reach the
 * destructor and is treated as a crash; the counter is incremented and
 * rewritten. Clean destruction deletes the marker so the next start sees zero.
 *
 * Destruction during stack unwinding (std::uncaught_exceptions() > 0) is
 * auto-detected and treated as unclean. Callers that catch unrecoverable
 * exceptions upstream and then let the counter destruct normally must invoke
 * MarkUncleanExit() so the marker is preserved across the next start.
 *
 * Assumes at most one instance of a given node name per host. Concurrent
 * instances would corrupt the on-disk count; enforce uniqueness upstream.
 */
class CrashCounter {
 public:
  explicit CrashCounter(std::string_view marker_dir, std::string_view node_name);
  ~CrashCounter();

  CrashCounter(const CrashCounter&) = delete;
  CrashCounter& operator=(const CrashCounter&) = delete;
  CrashCounter(CrashCounter&&) = delete;
  CrashCounter& operator=(CrashCounter&&) = delete;

  /**
   * Number of consecutive unclean exits leading up to the current start.
   * Zero on a clean start.
   */
  int UncleanExitCount() const { return unclean_exit_count_; }

  /**
   * Mark this run as having ended abnormally (e.g. an exception caught by
   * Node::Run). Suppresses the marker delete in the destructor so the next
   * start counts this run as a crash. Idempotent.
   */
  void MarkUncleanExit() { mark_unclean_ = true; }

 private:
  std::filesystem::path marker_path_;
  int unclean_exit_count_{0};
  bool mark_unclean_{false};
};

}  // namespace core
}  // namespace trellis

#endif  // TRELLIS_CORE_CRASH_COUNTER_HPP_
