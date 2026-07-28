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

#ifndef TRELLIS_CORE_TEST_TEST_PATHS_HPP_
#define TRELLIS_CORE_TEST_TEST_PATHS_HPP_

#include <string>
#include <string_view>

namespace trellis::core::test {

/**
 * @brief Resolve a data dependency of this package into an absolute filesystem path
 *
 * The layout of the runfiles tree depends on whether trellis is the root Bazel module or a dependency of another
 * module, so data files must be looked up through the runfiles library rather than by a workspace-relative path.
 *
 * @param filename the name of the file, relative to this package (no directory components)
 * @return the absolute path to the file
 * @throws std::runtime_error if the runfiles tree cannot be read or does not contain the file
 */
std::string DataPath(std::string_view filename);

/**
 * @brief Resolve a filename into an absolute path inside the test's writable temporary directory
 *
 * The runfiles tree is not a writable location, so tests that produce output must write here instead.
 *
 * @param filename the name of the file, relative to the temporary directory
 * @return the absolute path to the file
 * @throws std::runtime_error if the test environment does not provide a temporary directory
 */
std::string TempPath(std::string_view filename);

}  // namespace trellis::core::test

#endif  // TRELLIS_CORE_TEST_TEST_PATHS_HPP_
