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

#include <gtest/gtest.h>

#include "trellis/core/proto_utils.hpp"

using namespace trellis::core;

TEST(TrellisProtoUtils, RawTopicNameTest) {
  const std::string topic_name = "foo_bar";
  const std::string raw_topic_name = proto_utils::GetRawTopicString(topic_name);
  ASSERT_TRUE(proto_utils::IsRawTopic(raw_topic_name));
  ASSERT_FALSE(proto_utils::IsRawTopic(topic_name));
  ASSERT_TRUE(proto_utils::IsRawTopic("/trellis/raw:hello_world"));
  ASSERT_EQ(proto_utils::GetTopicFromRawTopic("/trellis/raw:hello_world"), "hello_world");
}
