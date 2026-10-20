/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// One binary that mixes F14 code compiled at -O0 and at -O2, like a program
// that links an unoptimized library into an optimized binary.
//
// Every object file that calls an F14 function out of line, or takes its
// address, emits its own copy, and in a static link the linker keeps the first
// copy of each. If a per-file
// macro such as __OPTIMIZE__ changed an inline function's types, signature or
// return layout, callers compiled one way would call a copy compiled the other
// way and misread its result.
//
// The link order makes that mix happen:
// - This file is built at -O2 and linked first. It emits optimized out-of-line
//   copies of F14Chunk::firstEmpty() (below), so the linker keeps those.
// - :f14_mixed_optimization_o0 comes next, so its -O0 F14Table code, which
//   calls firstEmpty() out of line, is the copy the linker keeps.
// - :f14_mixed_optimization_o2 comes last; it builds and reads the same maps
//   from optimized code.
// This covers -O0 callers of optimized callees. The other direction does not
// reproduce, because optimized code inlines firstEmpty().
//
// The test means something only for builds targeting aarch64 with SVE2, where
// F14 has two code paths, and only where BUCK applies the -O flags.
// Elsewhere it passes without testing much.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <folly/container/detail/F14Table.h>
#include <folly/portability/GTest.h>

namespace folly::f14::test {

#if FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE
namespace {

template <typename Item>
using Chunk = detail::F14Chunk<Item>;

template <typename Item>
using FirstEmptyResult = decltype(std::declval<Chunk<Item> const&>().firstEmpty(
    std::declval<detail::TagVector>()));

template <typename Item>
using FirstEmptyFn =
    FirstEmptyResult<Item> (Chunk<Item>::*)(detail::TagVector) const;

} // namespace

// Taking these addresses makes this optimized file emit an out-of-line
// F14Chunk::firstEmpty(TagVector) for the item types of F14ValueMap,
// F14NodeMap and F14VectorMap<int, int>. Its return type is what differed
// between -O0 and optimized files when the ISA path depended on __OPTIMIZE__.
// They are extern so that, although const, they have external linkage: the
// compiler must emit them, and with them the functions they point to.
extern FirstEmptyFn<std::pair<int const, int>> const gValueFirstEmpty =
    &Chunk<std::pair<int const, int>>::firstEmpty;
extern FirstEmptyFn<std::pair<int const, int>*> const gNodeFirstEmpty =
    &Chunk<std::pair<int const, int>*>::firstEmpty;
extern FirstEmptyFn<std::uint32_t> const gVectorFirstEmpty =
    &Chunk<std::uint32_t>::firstEmpty;
#endif

// Same definition as in F14MixedOptimizationOps.cpp. The body differs with
// __OPTIMIZE__ on purpose, to show which file's copy the linker kept: a
// deliberate, test-only ODR violation. Do not copy this pattern.
inline bool linkedCopyIsOptimized() {
#ifdef __OPTIMIZE__
  return true;
#else
  return false;
#endif
}

extern bool (*const gLinkedCopyIsOptimized)() = &linkedCopyIsOptimized;

// Defined in F14MixedOptimizationOps.cpp.
struct MixedOptMaps;

namespace o0 {
MixedOptMaps* makeMaps();
void destroyMaps(MixedOptMaps* maps);
void insertKeys(MixedOptMaps& maps, int kind, int begin, int end);
void eraseKeys(MixedOptMaps& maps, int kind, int begin, int end);
void eraseEvenKeysWhileIterating(MixedOptMaps& maps, int kind);
int countFound(MixedOptMaps& maps, int kind, int begin, int end);
std::size_t size(MixedOptMaps& maps, int kind);
long sumByIteration(MixedOptMaps& maps, int kind);
bool isOptimized();
bool callLinkedCopyIsOptimized();
std::string isaFacts();
} // namespace o0

namespace o2 {
MixedOptMaps* makeMaps();
void destroyMaps(MixedOptMaps* maps);
void insertKeys(MixedOptMaps& maps, int kind, int begin, int end);
void eraseKeys(MixedOptMaps& maps, int kind, int begin, int end);
void eraseEvenKeysWhileIterating(MixedOptMaps& maps, int kind);
int countFound(MixedOptMaps& maps, int kind, int begin, int end);
std::size_t size(MixedOptMaps& maps, int kind);
long sumByIteration(MixedOptMaps& maps, int kind);
bool isOptimized();
bool callLinkedCopyIsOptimized();
std::string isaFacts();
} // namespace o2

namespace {

constexpr int kValueMap = 0;
constexpr int kNodeMap = 1;
constexpr int kVectorMap = 2;

// Sum of the values insertKeys() gives keys in [begin, end), counting only
// odd keys if oddOnly.
long expectedSum(int begin, int end, bool oddOnly = false) {
  long sum = 0;
  for (int k = begin; k < end; ++k) {
    if (!oddOnly || k % 2 != 0) {
      sum += 2 * k;
    }
  }
  return sum;
}

void checkInsertFindEraseAcrossOptimizationLevels(int kind) {
  // Several chunks per map, so inserts reuse chunks that already hold items.
  constexpr int kN = 200;
  std::unique_ptr<MixedOptMaps, void (*)(MixedOptMaps*)> maps{
      o2::makeMaps(), &o0::destroyMaps};

  o0::insertKeys(*maps, kind, 0, kN);
  EXPECT_EQ(o2::countFound(*maps, kind, 0, kN), kN);
  EXPECT_EQ(o2::size(*maps, kind), kN);
  EXPECT_EQ(o2::sumByIteration(*maps, kind), expectedSum(0, kN));

  o2::insertKeys(*maps, kind, kN, 2 * kN);
  EXPECT_EQ(o0::countFound(*maps, kind, 0, 2 * kN), 2 * kN);
  EXPECT_EQ(o0::sumByIteration(*maps, kind), expectedSum(0, 2 * kN));

  o0::eraseKeys(*maps, kind, 0, kN);
  EXPECT_EQ(o2::countFound(*maps, kind, 0, kN), 0);
  EXPECT_EQ(o2::countFound(*maps, kind, kN, 2 * kN), kN);
  EXPECT_EQ(o2::size(*maps, kind), kN);

  o2::eraseEvenKeysWhileIterating(*maps, kind);
  EXPECT_EQ(o0::size(*maps, kind), kN / 2);
  EXPECT_EQ(
      o0::sumByIteration(*maps, kind),
      expectedSum(kN, 2 * kN, /* oddOnly */ true));

  o0::insertKeys(*maps, kind, 0, kN);
  EXPECT_EQ(o2::countFound(*maps, kind, 0, kN), kN);
  EXPECT_EQ(o2::size(*maps, kind), kN + kN / 2);
}

} // namespace

// Guards the setup itself: if the flags or the link stop mixing optimization
// levels, the other tests would pass without testing anything.
TEST(F14MixedOptimization, BuildMixesOptimizationLevels) {
#ifdef FOLLY_F14_MIXED_OPT_FLAGS_SET
  EXPECT_FALSE(o0::isOptimized());
  EXPECT_TRUE(o2::isOptimized());
  // The -O0 file calls the copy the linker kept, which is this file's.
  EXPECT_TRUE(o0::callLinkedCopyIsOptimized());
#else
  GTEST_SKIP() << "The -O flags were not applied in this build";
#endif
}

TEST(F14MixedOptimization, SameIsaPathAtEveryOptimizationLevel) {
  EXPECT_EQ(o0::isaFacts(), o2::isaFacts());
}

TEST(F14MixedOptimization, ValueMapAcrossOptimizationLevels) {
  checkInsertFindEraseAcrossOptimizationLevels(kValueMap);
}

TEST(F14MixedOptimization, NodeMapAcrossOptimizationLevels) {
  checkInsertFindEraseAcrossOptimizationLevels(kNodeMap);
}

TEST(F14MixedOptimization, VectorMapAcrossOptimizationLevels) {
  checkInsertFindEraseAcrossOptimizationLevels(kVectorMap);
}

} // namespace folly::f14::test
