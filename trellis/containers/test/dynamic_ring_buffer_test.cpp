#include "trellis/containers/dynamic_ring_buffer.hpp"

#include <gmock/gmock.h>

namespace trellis::containers {

namespace {

using testing::ElementsAre;
using testing::Eq;
using testing::IsEmpty;
using testing::Pointee;
using testing::SizeIs;

// unique_ptr is a good test element because it has move but not copy semantics.
using DynamicTestBuffer = DynamicRingBuffer<std::unique_ptr<int>>;

}  // namespace

TEST(DynamicRingBuffer, Empty) {
  const auto ring = DynamicTestBuffer{};
  ASSERT_THAT(ring, SizeIs(0));
  ASSERT_THAT(ring, IsEmpty());
  ASSERT_THAT(ring.begin(), Eq(ring.end()));
}

TEST(DynamicRingBuffer, PushBack) {
  auto ring = DynamicTestBuffer{};
  ring.push_back(std::make_unique<int>(0));
  ring.push_back(std::make_unique<int>(1));
  ring.push_back(std::make_unique<int>(2));
  ASSERT_THAT(ring, SizeIs(3));
  ASSERT_THAT(ring, ElementsAre(Pointee(0), Pointee(1), Pointee(2)));
}

TEST(DynamicRingBuffer, PopFront) {
  auto ring = DynamicTestBuffer{};
  ring.push_back(std::make_unique<int>(0));
  ring.push_back(std::make_unique<int>(1));
  ring.push_back(std::make_unique<int>(2));
  ring.pop_front();
  ASSERT_THAT(ring, SizeIs(2));
  ASSERT_THAT(ring, ElementsAre(Pointee(1), Pointee(2)));
}

TEST(DynamicRingBuffer, PushPopFront) {
  auto ring = DynamicTestBuffer{};
  ring.push_back(std::make_unique<int>(0));
  ring.push_back(std::make_unique<int>(1));
  ring.push_back(std::make_unique<int>(2));
  ring.pop_front();
  ring.pop_front();
  ring.push_back(std::make_unique<int>(3));
  ASSERT_THAT(ring, SizeIs(2));
  ASSERT_THAT(ring, ElementsAre(Pointee(2), Pointee(3)));
}

TEST(DynamicRingBuffer, MoveConstructor) {
  auto ring1 = DynamicTestBuffer{};
  ring1.push_back(std::make_unique<int>(0));
  const auto ring2 = std::move(ring1);
  ASSERT_THAT(ring1, SizeIs(0));
  ASSERT_THAT(ring2, ElementsAre(Pointee(0)));
}

TEST(DynamicRingBuffer, MoveAssignment) {
  auto ring1 = DynamicTestBuffer{};
  ring1.push_back(std::make_unique<int>(0));
  auto ring2 = DynamicTestBuffer{};
  ring2.push_back(std::make_unique<int>(1));
  ring2 = std::move(ring1);
  ASSERT_THAT(ring1, SizeIs(0));
  ASSERT_THAT(ring2, ElementsAre(Pointee(0)));
}

}  // namespace trellis::containers
