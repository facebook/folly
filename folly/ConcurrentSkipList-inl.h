/*
 * Copyright 2012 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// @author: Xin Liu <xliux@fb.com>

#ifndef FOLLY_CONCURRENTSKIPLIST_INL_H_
#define FOLLY_CONCURRENTSKIPLIST_INL_H_

#include <algorithm>
#include <climits>
#include <cmath>
#include <boost/random.hpp>

#include <glog/logging.h>
#include "folly/SmallLocks.h"
#include "folly/ThreadLocal.h"

namespace folly { namespace detail {

template<typename ValT, typename NodeT> class csl_iterator;

template<typename T>
class SkipListNode : boost::noncopyable {
  enum {
    IS_HEAD_NODE = 1,
    MARKED_FOR_REMOVAL = (1 << 1),
    FULLY_LINKED = (1 << 2),
  };
 public:
  typedef T value_type;

  template<typename U,
    typename=typename std::enable_if<std::is_convertible<U, T>::value>::type>
  static SkipListNode* create(int height, U&& data, bool isHead = false) {
    DCHECK(height >= 1 && height < 64) << height;

    size_t size = sizeof(SkipListNode) +
      height * sizeof(std::atomic<SkipListNode*>);
    auto* node = static_cast<SkipListNode*>(malloc(size));
    // do placement new
    new (node) SkipListNode(height, std::forward<U>(data), isHead);
    return node;
  }

  static void destroy(SkipListNode* node) {
    node->~SkipListNode();
    free(node);
  }

  // copy the head node to a new head node assuming lock acquired
  SkipListNode* copyHead(SkipListNode* node) {
    DCHECK(node != nullptr && height_ > node->height_);
    setFlags(node->getFlags());
    for (int i = 0; i < node->height_; ++i) {
      setSkip(i, node->skip(i));
    }
    return this;
  }

  inline SkipListNode* skip(int layer) const {
    DCHECK_LT(layer, height_);
    return skip_[layer].load(std::memory_order_consume);
  }

  // next valid node as in the linked list
  SkipListNode* next() {
    SkipListNode* node;
    for (node = skip(0);
        (node != nullptr && node->markedForRemoval());
        node = node->skip(0)) {}
    return node;
  }

  void setSkip(uint8_t h, SkipListNode* next) {
    DCHECK_LT(h, height_);
    skip_[h].store(next, std::memory_order_release);
  }

  value_type& data() { return data_; }
  const value_type& data() const { return data_; }
  int maxLayer() const { return height_ - 1; }
  int height() const { return height_; }

  std::unique_lock<MicroSpinLock> acquireGuard() {
    return std::unique_lock<MicroSpinLock>(spinLock_);
  }

  bool fullyLinked() const      { return getFlags() & FULLY_LINKED; }
  bool markedForRemoval() const { return getFlags() & MARKED_FOR_REMOVAL; }
  bool isHeadNode() const       { return getFlags() & IS_HEAD_NODE; }

  void setIsHeadNode() {
    setFlags(getFlags() | IS_HEAD_NODE);
  }
  void setFullyLinked() {
    setFlags(getFlags() | FULLY_LINKED);
  }
  void setMarkedForRemoval() {
    setFlags(getFlags() | MARKED_FOR_REMOVAL);
  }

 private:
  // Note! this can only be called from create() as a placement new.
  template<typename U>
  SkipListNode(uint8_t height, U&& data, bool isHead) :
      height_(height), data_(std::forward<U>(data)) {
    spinLock_.init();
    setFlags(0);
    if (isHead) setIsHeadNode();
    // need to explicitly init the dynamic atomic pointer array
    for (uint8_t i = 0; i < height_; ++i) {
      new (&skip_[i]) std::atomic<SkipListNode*>(nullptr);
    }
  }

  ~SkipListNode() {
    for (uint8_t i = 0; i < height_; ++i) {
      skip_[i].~atomic();
    }
  }

  uint16_t getFlags() const {
    return flags_.load(std::memory_order_consume);
  }
  void setFlags(uint16_t flags) {
    flags_.store(flags, std::memory_order_release);
  }

  // TODO(xliu): on x86_64, it's possible to squeeze these into
  // skip_[0] to maybe save 8 bytes depending on the data alignments.
  // NOTE: currently this is x86_64 only anyway, due to the
  // MicroSpinLock.
  std::atomic<uint16_t> flags_;
  const uint8_t height_;
  MicroSpinLock spinLock_;

  value_type data_;

  std::atomic<SkipListNode*> skip_[0];
};

class SkipListRandomHeight {
  enum { kMaxHeight = 64 };
 public:
  // make it a singleton.
  static SkipListRandomHeight *instance() {
    static SkipListRandomHeight instance_;
    return &instance_;
  }

  int getHeight(int maxHeight) const {
    DCHECK_LE(maxHeight, kMaxHeight) << "max height too big!";
    double p = randomProb();
    for (int i = 0; i < maxHeight; ++i) {
      if (p < lookupTable_[i]) {
        return i + 1;
      }
    }
    return maxHeight;
  }

  size_t getSizeLimit(int height) const {
    DCHECK_LT(height, kMaxHeight);
    return sizeLimitTable_[height];
  }

 private:

  SkipListRandomHeight() { initLookupTable(); }

  void initLookupTable() {
    // set skip prob = 1/E
    static const double kProbInv = exp(1);
    static const double kProb = 1.0 / kProbInv;
    static const size_t kMaxSizeLimit = std::numeric_limits<size_t>::max();

    double sizeLimit = 1;
    double p = lookupTable_[0] = (1 - kProb);
    sizeLimitTable_[0] = 1;
    for (int i = 1; i < kMaxHeight - 1; ++i) {
      p *= kProb;
      sizeLimit *= kProbInv;
      lookupTable_[i] = lookupTable_[i - 1] + p;
      sizeLimitTable_[i] = sizeLimit > kMaxSizeLimit ?
        kMaxSizeLimit :
        static_cast<size_t>(sizeLimit);
    }
    lookupTable_[kMaxHeight - 1] = 1;
    sizeLimitTable_[kMaxHeight - 1] = kMaxSizeLimit;
  }

  static double randomProb() {
    static ThreadLocal<boost::lagged_fibonacci2281> rng_;
    return (*rng_)();
  }

  double lookupTable_[kMaxHeight];
  size_t sizeLimitTable_[kMaxHeight];
};

}}

#endif  // FOLLY_CONCURRENTSKIPLIST_INL_H_
