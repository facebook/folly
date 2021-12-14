/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <string>

#include <folly/synchronization/Hazptr.h>

namespace folly {

/** Wide CAS.
 */
template <typename T, template <typename> class Atom = std::atomic>
class HazptrWideCAS {
  struct Node : public hazptr_obj_base<Node, Atom> {
    T val_;
    explicit Node(T v = {}) : val_(v) {}
  };

  Atom<Node*> node_;

 public:
  HazptrWideCAS() : node_(new Node()) {}

  ~HazptrWideCAS() { delete node_.load(std::memory_order_relaxed); }

  bool cas(T& u, T& v) {
    Node* n = new Node(v);
    hazptr_holder<Atom> hptr = make_hazard_pointer<Atom>();
    Node* p;
    while (true) {
      p = hptr.protect(node_);
      if (p->val_ != u) {
        delete n;
        return false;
      }
      if (node_.compare_exchange_weak(
              p, n, std::memory_order_release, std::memory_order_relaxed)) {
        break;
      }
    }
    hptr.reset_protection();
    p->retire();
    return true;
  }
};

} // namespace folly
