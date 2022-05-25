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

#pragma once

#include <memory>

#include <folly/Memory.h>
#include <folly/Synchronized.h>

namespace folly {
namespace compression {

template <typename T, typename Creator, typename Deleter, typename Resetter>
class CompressionContextPool {
 private:
  using InternalRef = std::unique_ptr<T, Deleter>;

  class ReturnToPoolDeleter {
   public:
    using Pool = CompressionContextPool<T, Creator, Deleter, Resetter>;

    explicit ReturnToPoolDeleter(Pool* pool) : pool_(pool) { DCHECK(pool); }

    void operator()(T* t) {
      InternalRef ptr(t, pool_->deleter_);
      pool_->add(std::move(ptr));
    }

   private:
    Pool* pool_;
  };

 public:
  using Object = T;
  using Ref = std::unique_ptr<T, ReturnToPoolDeleter>;

  explicit CompressionContextPool(
      Creator creator = Creator(),
      Deleter deleter = Deleter(),
      Resetter resetter = Resetter())
      : creator_(std::move(creator)),
        deleter_(std::move(deleter)),
        resetter_(std::move(resetter)),
        stack_(),
        created_(0) {}

  Ref get() {
    auto stack = stack_.wlock();
    if (stack->empty()) {
      T* t = creator_();
      if (t == nullptr) {
        throw_exception<std::bad_alloc>();
      }
      created_++;
      return Ref(t, get_deleter());
    }
    auto ptr = std::move(stack->back());
    stack->pop_back();
    if (!ptr) {
      throw_exception<std::logic_error>(
          "A nullptr snuck into our context pool!?!?");
    }
    return Ref(ptr.release(), get_deleter());
  }

  size_t created_count() const { return created_.load(); }

  size_t size() { return stack_.rlock()->size(); }

  ReturnToPoolDeleter get_deleter() { return ReturnToPoolDeleter(this); }

  Resetter& get_resetter() { return resetter_; }

  void flush_deep() {
    flush_shallow();
    // no backing stack, so deep == shallow
  }

  void flush_shallow() {
    auto stack = stack_.wlock();
    stack->resize(0);
  }

 private:
  void add(InternalRef ptr) {
    DCHECK(ptr);
    resetter_(ptr.get());
    stack_.wlock()->push_back(std::move(ptr));
  }

  Creator creator_;
  Deleter deleter_;
  Resetter resetter_;

  folly::Synchronized<std::vector<InternalRef>> stack_;

  std::atomic<size_t> created_;
};
} // namespace compression
} // namespace folly
