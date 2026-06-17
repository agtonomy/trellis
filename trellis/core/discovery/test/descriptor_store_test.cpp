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

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

namespace trellis::core::discovery {

namespace {

// Distinct directory per test instance so tests don't observe each other's blobs.
std::filesystem::path UniqueTestDir() {
  static int counter = 0;
  return std::filesystem::temp_directory_path() /
         ("trellis_descriptor_store_test_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
}

}  // namespace

class DescriptorStoreTest : public ::testing::Test {
 protected:
  void SetUp() override { dir_ = UniqueTestDir(); }
  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }

  std::filesystem::path dir_;
};

TEST_F(DescriptorStoreTest, RoundTrip) {
  DescriptorStore store(dir_.string());
  // Embedded NUL and high bytes to exercise true binary round-tripping.
  const std::string bytes("arbitrary\0descriptor\xff bytes", 27);

  const auto ref = store.Put(bytes);
  ASSERT_TRUE(ref.has_value());

  const auto resolved = store.Get(*ref);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, bytes);
}

TEST_F(DescriptorStoreTest, PutIsIdempotent) {
  DescriptorStore store(dir_.string());
  const std::string bytes = "the same content";

  const auto ref1 = store.Put(bytes);
  const auto ref2 = store.Put(bytes);
  ASSERT_TRUE(ref1.has_value());
  EXPECT_EQ(ref1, ref2);

  // Identical content is written exactly once.
  int fdset_files = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir_)) {
    if (entry.path().extension() == ".fdset") ++fdset_files;
  }
  EXPECT_EQ(fdset_files, 1);
}

TEST_F(DescriptorStoreTest, DistinctContentDistinctRef) {
  DescriptorStore store(dir_.string());

  const auto ref_a = store.Put("content A");
  const auto ref_b = store.Put("content B");
  ASSERT_TRUE(ref_a.has_value());
  ASSERT_TRUE(ref_b.has_value());
  EXPECT_NE(ref_a, ref_b);

  EXPECT_EQ(*store.Get(*ref_a), "content A");
  EXPECT_EQ(*store.Get(*ref_b), "content B");
}

TEST_F(DescriptorStoreTest, GetMissingRefReturnsNullopt) {
  DescriptorStore store(dir_.string());
  EXPECT_FALSE(store.Get("deadbeefdeadbeefdeadbeefdeadbeef").has_value());
}

TEST_F(DescriptorStoreTest, EmptyRefAndEmptyPutAreNoOps) {
  DescriptorStore store(dir_.string());
  EXPECT_FALSE(store.Put("").has_value());
  EXPECT_FALSE(store.Get("").has_value());
}

TEST_F(DescriptorStoreTest, PutReturnsNulloptWhenDirUnwritable) {
  // Plant a regular file where the store wants its directory, so create_directories() fails and the
  // blob cannot be persisted. Put must report failure rather than hand back an unresolvable hash.
  const std::filesystem::path blocking_file = dir_;
  std::filesystem::create_directories(blocking_file.parent_path());
  { std::ofstream(blocking_file) << "not a directory"; }

  DescriptorStore store(dir_.string());
  EXPECT_FALSE(store.Put("some descriptor bytes").has_value());
}

TEST_F(DescriptorStoreTest, RefreshRecreatesReapedBlob) {
  DescriptorStore writer(dir_.string());
  const std::string bytes = "a schema that gets reaped from /tmp";

  const auto ref = writer.Put(bytes);
  ASSERT_TRUE(ref.has_value());

  // Simulate a /tmp reaper deleting the blob out from under the running process. A peer store
  // (which never Put these bytes) observes the store purely through the filesystem.
  const std::filesystem::path blob = dir_ / (*ref + ".fdset");
  ASSERT_TRUE(std::filesystem::remove(blob));
  DescriptorStore peer(dir_.string());
  EXPECT_FALSE(peer.Get(*ref).has_value());

  writer.Refresh();

  const auto resolved = peer.Get(*ref);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, bytes);
}

TEST_F(DescriptorStoreTest, RefreshHealsCorruptBlob) {
  DescriptorStore writer(dir_.string());
  const std::string bytes = "a schema whose backing file is later truncated";

  const auto ref = writer.Put(bytes);
  ASSERT_TRUE(ref.has_value());

  // Overwrite the on-disk blob with different-sized content, modeling a truncated/partial prior
  // write. The size mismatch against the content-addressed bytes is the signal WriteIfAbsent uses
  // to re-publish rather than trust (and forever serve) the bad blob.
  const std::filesystem::path blob = dir_ / (*ref + ".fdset");
  { std::ofstream(blob, std::ios::binary | std::ios::trunc) << "corrupt"; }
  ASSERT_NE(std::filesystem::file_size(blob), bytes.size());

  writer.Refresh();

  // A peer reads purely from disk, so a correct result proves the file itself was healed.
  DescriptorStore peer(dir_.string());
  const auto resolved = peer.Get(*ref);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, bytes);
}

TEST_F(DescriptorStoreTest, GetAnswersSelfPublishedHashFromMemory) {
  DescriptorStore writer(dir_.string());
  const std::string bytes = "a schema this process both publishes and subscribes";

  const auto ref = writer.Put(bytes);
  ASSERT_TRUE(ref.has_value());

  // Even with the backing file reaped and no Refresh() yet, the publishing process resolves its
  // own hash from retained bytes. Get() does not heal the file; that remains Refresh()'s job.
  const std::filesystem::path blob = dir_ / (*ref + ".fdset");
  ASSERT_TRUE(std::filesystem::remove(blob));

  const auto resolved = writer.Get(*ref);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, bytes);
  EXPECT_FALSE(std::filesystem::exists(blob));
}

TEST_F(DescriptorStoreTest, RefreshOnlyRestoresBlobsThisStoreWrote) {
  const std::string bytes = "written by the publishing process only";

  DescriptorStore writer(dir_.string());
  const auto ref = writer.Put(bytes);
  ASSERT_TRUE(ref.has_value());

  const std::filesystem::path blob = dir_ / (*ref + ".fdset");
  ASSERT_TRUE(std::filesystem::remove(blob));

  // A peer store over the same dir that never wrote this blob cannot reconstruct it: it does not
  // hold the bytes, so Refresh() must leave the reaped blob missing rather than fabricate one.
  DescriptorStore peer(dir_.string());
  peer.Refresh();
  EXPECT_FALSE(std::filesystem::exists(blob));

  // The process that actually wrote it does restore it on Refresh().
  writer.Refresh();
  EXPECT_TRUE(std::filesystem::exists(blob));
}

TEST_F(DescriptorStoreTest, SeparateStoreInstancesShareBlobs) {
  const std::string bytes = "shared across processes on the same host";

  DescriptorStore writer(dir_.string());
  const auto ref = writer.Put(bytes);
  ASSERT_TRUE(ref.has_value());

  // A second, independent store over the same directory resolves the same ref, modeling a peer
  // process resolving a schema purely by reference.
  DescriptorStore reader(dir_.string());
  const auto resolved = reader.Get(*ref);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, bytes);
}

}  // namespace trellis::core::discovery
