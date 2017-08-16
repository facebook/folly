/*
 * Copyright 2017 Facebook, Inc.
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

#include "memory_resource.h"

namespace folly {
namespace hazptr {

memory_resource** default_mr_ptr() {
  /* library-local */ static memory_resource* default_mr =
      new_delete_resource();
  DEBUG_PRINT(&default_mr << " " << default_mr);
  return &default_mr;
}

memory_resource* get_default_resource() {
  DEBUG_PRINT("");
  return *default_mr_ptr();
}

void set_default_resource(memory_resource* mr) {
  DEBUG_PRINT("");
  *default_mr_ptr() = mr;
}

memory_resource* new_delete_resource() {
  class new_delete : public memory_resource {
   public:
    void* allocate(
        const size_t bytes,
        const size_t alignment = folly::max_align_v) override {
      (void)alignment;
      void* p = static_cast<void*>(new char[bytes]);
      DEBUG_PRINT(this << " " << p << " " << bytes);
      return p;
    }
    void deallocate(
        void* p,
        const size_t bytes,
        const size_t alignment = folly::max_align_v) override {
      (void)alignment;
      (void)bytes;
      DEBUG_PRINT(p << " " << bytes);
      delete[] static_cast<char*>(p);
    }
  };
  static new_delete mr;
  return &mr;
}

} // namespace hazptr
} // namespace folly
