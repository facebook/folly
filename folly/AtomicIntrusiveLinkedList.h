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

#include <atomic>
#include <cassert>
#include <utility>

namespace folly {

/**
 * A very simple atomic single-linked list primitive.
 *
 * Usage:
 *
 * class MyClass {
 *   AtomicIntrusiveLinkedListHook<MyClass> hook_;
 * }
 *
 * AtomicIntrusiveLinkedList<MyClass, &MyClass::hook_> list;
 * list.insert(&a);
 * list.sweep([] (MyClass* c) { doSomething(c); }
 */
template <class T>
struct AtomicIntrusiveLinkedListHook {
  T* next{nullptr};
};

template <class T, AtomicIntrusiveLinkedListHook<T> T::*HookMember>
class AtomicIntrusiveLinkedList {
 public:
  AtomicIntrusiveLinkedList() {}

  AtomicIntrusiveLinkedList(const AtomicIntrusiveLinkedList&) = delete;
  AtomicIntrusiveLinkedList& operator=(const AtomicIntrusiveLinkedList&) =
      delete;

  AtomicIntrusiveLinkedList(AtomicIntrusiveLinkedList&& other) noexcept
      : head_(other.head_.exchange(nullptr, std::memory_order_acq_rel)) {}

  // Absent because would be too error-prone to use correctly because of
  // the requirement that lists are empty upon destruction.
  AtomicIntrusiveLinkedList& operator=(
      AtomicIntrusiveLinkedList&& other) noexcept = delete;

  /**
   * Move the currently held elements to a new list.
   * The current list becomes empty, but concurrent threads
   * might still add new elements to it.
   *
   * Equivalent to calling a move constructor, but more linter-friendly
   * in case you still need the old list.
   */
  AtomicIntrusiveLinkedList spliceAll() { return std::move(*this); }

  /**
   * Move-assign the current list to `other`, then reverse-sweep
   * the old list with the provided callback `func`.
   *
   * A safe replacement for the move assignment operator, which is absent
   * because of the resource leak concerns.
   */
  template <typename F>
  void reverseSweepAndAssign(AtomicIntrusiveLinkedList&& other, F&& func) {
    auto otherHead = other.head_.exchange(nullptr, std::memory_order_acq_rel);
    auto head = head_.exchange(otherHead, std::memory_order_acq_rel);
    unlinkAll(head, std::forward<F>(func));
  }

  /**
   * Note: The list must be empty on destruction.
   */
  ~AtomicIntrusiveLinkedList() { assert(empty()); }

  /**
   * Returns the current head of the list.
   *
   * WARNING: The returned pointer might not be valid if the list
   * is modified concurrently!
   */
  T* unsafeHead() const { return head_.load(std::memory_order_acquire); }

  /**
   * Returns true if the list is empty.
   *
   * WARNING: This method's return value is only valid for a snapshot
   * of the state, it might become stale as soon as it's returned.
   */
  bool empty() const { return unsafeHead() == nullptr; }

  /**
   * Atomically insert t at the head of the list.
   * @return True if the inserted element is the only one in the list
   *         after the call.
   */
  bool insertHead(T* t) {
    assert(next(t) == nullptr);

    auto oldHead = head_.load(std::memory_order_relaxed);
    do {
      next(t) = oldHead;
      /* oldHead is updated by the call below.

         NOTE: we don't use next(t) instead of oldHead directly due to
         compiler bugs (GCC prior to 4.8.3 (bug 60272), clang (bug 18899),
         MSVC (bug 819819); source:
         http://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange */
    } while (!head_.compare_exchange_weak(
        oldHead, t, std::memory_order_release, std::memory_order_relaxed));

    return oldHead == nullptr;
  }

  /**
   * Replaces the head with nullptr,
   * and calls func() on the removed elements in the order from tail to head.
   * Returns false if the list was empty.
   */
  template <typename F>
  bool sweepOnce(F&& func) {
    if (auto head = head_.exchange(nullptr, std::memory_order_acq_rel)) {
      auto rhead = reverse(head);
      unlinkAll(rhead, std::forward<F>(func));
      return true;
    }
    return false;
  }

  /**
   * Repeatedly replaces the head with nullptr,
   * and calls func() on the removed elements in the order from tail to head.
   * Stops when the list is empty.
   */
  template <typename F>
  void sweep(F&& func) {
    while (sweepOnce(func)) {
    }
  }

  /**
   * Similar to sweep() but calls func() on elements in LIFO order.
   *
   * func() is called for all elements in the list at the moment
   * reverseSweep() is called.  Unlike sweep() it does not loop to ensure the
   * list is empty at some point after the last invocation.  This way callers
   * can reason about the ordering: elements inserted since the last call to
   * reverseSweep() will be provided in LIFO order.
   *
   * Example: if elements are inserted in the order 1-2-3, the callback is
   * invoked 3-2-1.  If the callback moves elements onto a stack, popping off
   * the stack will produce the original insertion order 1-2-3.
   */
  template <typename F>
  void reverseSweep(F&& func) {
    // We don't loop like sweep() does because the overall order of callbacks
    // would be strand-wise LIFO which is meaningless to callers.
    auto head = head_.exchange(nullptr, std::memory_order_acq_rel);
    unlinkAll(head, std::forward<F>(func));
  }

 private:
  std::atomic<T*> head_{nullptr};

  static T*& next(T* t) { return (t->*HookMember).next; }

  /* Reverses a linked list, returning the pointer to the new head
     (old tail) */
  static T* reverse(T* head) {
    T* rhead = nullptr;
    while (head != nullptr) {
      auto t = head;
      head = next(t);
      next(t) = rhead;
      rhead = t;
    }
    return rhead;
  }

  /* Unlinks all elements in the linked list fragment pointed to by `head',
   * calling func() on every element */
  template <typename F>
  static void unlinkAll(T* head, F&& func) {
    while (head != nullptr) {
      auto t = head;
      head = next(t);
      next(t) = nullptr;
      func(t);
    }
  }
};

} // namespace folly
