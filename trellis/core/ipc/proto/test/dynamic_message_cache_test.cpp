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

#include "trellis/core/ipc/proto/dynamic_message_cache.hpp"

#include <google/protobuf/util/json_util.h>
#include <gtest/gtest.h>

#include "trellis/core/discovery/utils.hpp"
#include "trellis/core/test/test.pb.h"

namespace trellis::core::ipc::proto {

TEST(DynamicMessageCache, CreateMessageDynamically) {
  test::Test msg;
  const auto desc = discovery::utils::GetProtoMessageDescription(msg);
  trellis::core::ipc::proto::DynamicMessageCache cache(desc);

  auto dyn_msg = cache.Create("trellis.core.test.Test");
  ASSERT_TRUE(dyn_msg != nullptr);

  std::string json_raw;
  google::protobuf::util::JsonPrintOptions json_options;
  json_options.add_whitespace = false;
  json_options.always_print_primitive_fields = true;
  json_options.always_print_enums_as_ints = false;
  json_options.preserve_proto_field_names = false;
  auto status = google::protobuf::util::MessageToJsonString(*dyn_msg, &json_raw, json_options);
  // Our dynamic message should have the same fields as test::Test and with default values
  ASSERT_EQ(json_raw, "{\"id\":0,\"msg\":\"\"}");
  ASSERT_TRUE(status.ok());
}

TEST(DynamicMessageCache, CreateMessageThatDoesntExist) {
  test::Test msg;
  const auto desc = discovery::utils::GetProtoMessageDescription(msg);
  trellis::core::ipc::proto::DynamicMessageCache cache(desc);

  EXPECT_THROW(cache.Create("trellis.core.test.FakeMessage"), std::runtime_error);
}

}  // namespace trellis::core::ipc::proto
