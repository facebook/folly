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

#include <atomic>
#include <iostream>

#include <folly/ConcurrentBSkipList.h>
#include <folly/portability/GTest.h>
#include <folly/test/ConcurrentBSkipListTestKeys.h>

using folly::bskip_test::AoS16BKey24;
using folly::bskip_test::Bench16B;

namespace {

template <typename Traits>
struct NodeLayoutProbe : folly::bskip_detail::BSkipNode<Traits> {
  using Base = folly::bskip_detail::BSkipNode<Traits>;
  using Base::nextMinKey_;
  using NextMinKeyStorage = std::remove_cvref_t<decltype(nextMinKey_)>;
};

template <typename Traits>
size_t logicalLeafBytes() {
  size_t bytes = Traits::kMaxKeys * sizeof(typename Traits::key_type);
  if constexpr (Traits::kHasSeparatePayload) {
    bytes += Traits::kMaxKeys * sizeof(typename Traits::payload_type);
  }
  return bytes;
}

template <typename Traits>
constexpr size_t payloadSize() {
  if constexpr (Traits::kHasPayload) {
    return sizeof(typename Traits::payload_type);
  }
  return 0;
}

template <typename Traits>
constexpr size_t payloadStorageSize() {
  if constexpr (Traits::kHasPayload) {
    return sizeof(typename Traits::PayloadStorage);
  }
  return 0;
}

template <typename Traits>
void printLayout(const char* label) {
  using Node = folly::bskip_detail::BSkipNode<Traits>;
  using LeafStorage = folly::bskip_detail::LeafStorage<Traits>;
  using Leaf = folly::bskip_detail::BSkipNodeLeaf<Traits>;
  using Probe = NodeLayoutProbe<Traits>;
  using NextMinKeyStorage = typename Probe::NextMinKeyStorage;
  using Seq = folly::bskip_detail::Seqlock;

  constexpr size_t kNodeFieldBytes = sizeof(std::atomic<Node*>) + sizeof(Seq) +
      sizeof(folly::RWSpinLock) + sizeof(NextMinKeyStorage) +
      sizeof(folly::relaxed_atomic<uint8_t>) + sizeof(uint8_t);
  const size_t leafLogicalBytes = logicalLeafBytes<Traits>();

  std::cout << label << "\n";
  std::cout
      << "  key_size=" << sizeof(typename Traits::key_type)
      << " key_storage_size=" << sizeof(typename Traits::KeyStorage)
      << " key_storage_align=" << alignof(typename Traits::KeyStorage) << "\n";
  std::cout << "  payload_size=" << payloadSize<Traits>()
            << " payload_storage_size=" << payloadStorageSize<Traits>() << "\n";
  std::cout << "  nextMinKey_storage_size=" << sizeof(NextMinKeyStorage)
            << " nextMinKey_storage_align=" << alignof(NextMinKeyStorage)
            << "\n";
  std::cout << "  node_size=" << sizeof(Node) << " node_align=" << alignof(Node)
            << " node_padding=" << (sizeof(Node) - kNodeFieldBytes) << "\n";
  std::cout
      << "  leaf_storage_size=" << sizeof(LeafStorage) << " leaf_storage_align="
      << alignof(LeafStorage) << " logical_leaf_bytes=" << leafLogicalBytes
      << " leaf_storage_overhead=" << (sizeof(LeafStorage) - leafLogicalBytes)
      << "\n";
  std::cout << "  leaf_size=" << sizeof(Leaf) << " leaf_align=" << alignof(Leaf)
            << "\n";
}

TEST(ConcurrentBSkipListLayout, ReportSizes) {
  using U32Traits = folly::bskip_detail::
      InternalTraits<uint32_t, std::less<uint32_t>, 16, 16>;
  using Key16BLockedTraits = folly::bskip_detail::InternalTraits<
      Bench16B,
      std::less<Bench16B>,
      16,
      16,
      folly::KeyReadPolicy::Locked,
      uint64_t>;
  using Key16BOptimisticTraits = folly::bskip_detail::InternalTraits<
      Bench16B,
      std::less<Bench16B>,
      16,
      16,
      folly::KeyReadPolicy::RelaxedAtomic,
      uint64_t>;
  using AoS16BLockedTraits = folly::bskip_detail::InternalTraits<
      AoS16BKey24,
      std::less<AoS16BKey24>,
      24,
      16,
      folly::KeyReadPolicy::Locked>;

  std::cout << std::boolalpha;
  std::cout << "Bench16B hardware_atomic="
            << folly::bskip_detail::kIsHardwareAtomic<Bench16B> << "\n";
  std::cout << "AoS16BKey24 hardware_atomic="
            << folly::bskip_detail::kIsHardwareAtomic<AoS16BKey24> << "\n";

  printLayout<U32Traits>("uint32 SoA");
  printLayout<Key16BLockedTraits>("Key16B SoA locked");
  printLayout<Key16BOptimisticTraits>("Key16B SoA optimistic");
  printLayout<AoS16BLockedTraits>("Key16B AoS locked");

  SUCCEED();
}

} // namespace
