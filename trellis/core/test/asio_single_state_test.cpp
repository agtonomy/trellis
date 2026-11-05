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

#include <dlfcn.h>
#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>

#include "trellis/core/test/test_paths.hpp"

namespace {

// asio keeps process-wide state in static locals of functions such as get_signal_state(), which holds the list of
// signal services and the pipe its handler writes to, and get_misc_category(), whose address is what makes an error
// code equal asio::error::eof. asio.cpp compiles these functions out of line under ASIO_SEPARATE_COMPILATION, and
// their statics are then private to it. A target that includes asio's headers without the define compiles a second,
// inline copy, whose statics are process-wide unique symbols that the shared library exports. A host that loads
// libtrellis.so then binds its own asio code to that second copy, and nodes register with one signal-service list and
// unregister from the other.
//
// Each entry pairs such a function with its static. asio.cpp exports the function out of line, so finding it shows
// that the library contains separately compiled asio and that the function still has this name: the static's absence
// is then not just a renamed symbol.
struct FunctionWithStatic {
  const char* function;
  const char* inline_static;
};

constexpr std::array kFunctionsWithStatics = {
    FunctionWithStatic{"_ZN4asio6detail16get_signal_stateEv", "_ZZN4asio6detail16get_signal_stateEvE5state"},
    FunctionWithStatic{"_ZN4asio5error17get_misc_categoryEv", "_ZZN4asio5error17get_misc_categoryEvE8instance"},
};

struct DlCloser {
  void operator()(void* handle) const { dlclose(handle); }
};

class AsioSingleState : public testing::Test {
 protected:
  void SetUp() override {
    library_.reset(dlopen(trellis::core::test::RunfilePath("trellis", "libtrellis.so").c_str(), RTLD_NOW | RTLD_LOCAL));
    ASSERT_NE(library_, nullptr) << dlerror();
  }

  std::unique_ptr<void, DlCloser> library_;
};

}  // namespace

TEST_F(AsioSingleState, SharedLibraryContainsSeparatelyCompiledAsio) {
  for (const auto& entry : kFunctionsWithStatics) {
    EXPECT_NE(dlsym(library_.get(), entry.function), nullptr) << "libtrellis.so does not export " << entry.function;
  }
}

TEST_F(AsioSingleState, SharedLibraryExportsNoInlineCopyOfAsioState) {
  for (const auto& entry : kFunctionsWithStatics) {
    EXPECT_EQ(dlsym(library_.get(), entry.inline_static), nullptr)
        << "libtrellis.so exports " << entry.inline_static << ", a header-only copy of asio's state; some target that "
        << "includes asio was compiled without ASIO_SEPARATE_COMPILATION";
  }
}
