/*
 * Copyright (C) 2021 Agtonomy
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

#include "trellis/core/test/test_paths.hpp"

#include <fmt/core.h>

#include <cstdlib>
#include <memory>
#include <stdexcept>

#include "tools/cpp/runfiles/runfiles.h"

namespace trellis::core::test {

namespace {

using bazel::tools::cpp::runfiles::Runfiles;

// Runfiles keys are `<apparent repo name>/<package path>/<file>`. The apparent name is the one this repository uses to
// refer to itself, which for a Bazel module is its module name. BAZEL_CURRENT_REPOSITORY tells the runfiles library
// which repository is asking, so that it can translate `trellis` into the canonical repository name for both the
// root-module and the dependency case.
constexpr auto kRepoName = "trellis";
constexpr auto kPackagePath = "trellis/core/test";

}  // namespace

std::string DataPath(std::string_view filename) {
  auto error = std::string{};
  const auto runfiles = std::unique_ptr<Runfiles>{Runfiles::CreateForTest(BAZEL_CURRENT_REPOSITORY, &error)};
  if (runfiles == nullptr) {
    throw std::runtime_error{fmt::format("Unable to read the runfiles tree: {}", error)};
  }

  const auto key = fmt::format("{}/{}/{}", kRepoName, kPackagePath, filename);
  const auto path = runfiles->Rlocation(key);
  if (path.empty()) {
    throw std::runtime_error{fmt::format("No runfile found for {}, is it listed as a data dependency?", key)};
  }
  return path;
}

std::string TempPath(std::string_view filename) {
  const auto* const test_tmpdir = std::getenv("TEST_TMPDIR");
  if (test_tmpdir == nullptr) {
    throw std::runtime_error{"TEST_TMPDIR is unset, this must run as a bazel test"};
  }
  return fmt::format("{}/{}", test_tmpdir, filename);
}

}  // namespace trellis::core::test
