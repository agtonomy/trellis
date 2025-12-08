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

#ifndef TRELLIS_CORE_TEST_TEST_HPP_
#define TRELLIS_CORE_TEST_TEST_HPP_

#include <string>

#include "trellis/core/test/test.pb.h"

namespace trellis::core::test {
namespace arbitrary {

struct Test {
  unsigned int id;
  std::string msg;
};

trellis::core::test::Test ToProto(const trellis::core::test::arbitrary::Test& test) {
  auto ret = trellis::core::test::Test{};
  ret.set_id(test.id);
  ret.set_msg(test.msg);
  return ret;
}

trellis::core::test::arbitrary::Test FromProto(const trellis::core::test::Test& test) {
  return {.id = test.id(), .msg = test.msg()};
}

}  // namespace arbitrary
}  // namespace trellis::core::test

#endif  // TRELLIS_CORE_TEST_TEST_HPP_
