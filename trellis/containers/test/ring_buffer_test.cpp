#include "trellis/containers/ring_buffer.hpp"

#include <gmock/gmock.h>

namespace trellis::containers {

namespace {

using testing::ElementsAre;
using testing::Eq;
using testing::IsEmpty;
using testing::Pointee;
using testing::SizeIs;

// unique_ptr is a good test element because it has move but not copy semantics.
using TestBuffer = RingBuffer<std::unique_ptr<int>, 3>;

}  // namespace

TEST(RingBuffer, Empty) {
  const auto ring = TestBuffer{};
  ASSERT_THAT(ring, SizeIs(0));
  ASSERT_THAT(ring, IsEmpty());
  ASSERT_THAT(ring.begin(), Eq(ring.end()));
}

TEST(RingBuffer, PushBack) {
  auto ring = TestBuffer{};
  ring.push_back(std::make_unique<int>(0));
  ring.push_back(std::make_unique<int>(1));
  ring.push_back(std::make_unique<int>(2));
  ASSERT_THAT(ring, SizeIs(3));
  ASSERT_THAT(ring, ElementsAre(Pointee(0), Pointee(1), Pointee(2)));
}

TEST(RingBuffer, PushBackOverflow) {
  auto ring = TestBuffer{};
  ring.push_back(std::make_unique<int>(0));
  ring.push_back(std::make_unique<int>(1));
  ring.push_back(std::make_unique<int>(2));
  ring.push_back(std::make_unique<int>(3));
  ring.push_back(std::make_unique<int>(4));
  ASSERT_THAT(ring, SizeIs(3));
  ASSERT_THAT(ring, ElementsAre(Pointee(2), Pointee(3), Pointee(4)));
}

}  // namespace trellis::containers
