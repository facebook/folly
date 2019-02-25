/*
 * Copyright 2016-present Facebook, Inc.
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

#include <folly/SingletonThreadLocal.h>

#include <cstdlib>
#include <iostream>

#include <folly/Demangle.h>

namespace folly {

namespace detail {

SingletonThreadLocalBase::UniqueBase::UniqueBase(
    Ref type,
    Ref tag,
    Ref make,
    Ref tltag,
    Value& value) noexcept {
  if (!value.init) {
    value.init = true;
    value.make = &make;
    value.tltag = &tltag;
  }
  if (*value.make == make && *value.tltag == tltag) {
    return;
  }

  auto const type_s = demangle(type.name());
  auto const tag_s = demangle(tag.name());
  auto const make_0_s = demangle(value.make->name());
  auto const make_1_s = demangle(make.name());
  auto const tltag_0_s = demangle(value.tltag->name());
  auto const tltag_1_s = demangle(tltag.name());
  std::ios_base::Init io_init;
  std::cerr << "Overloaded folly::SingletonThreadLocal<" << type_s << ", "
            << tag_s << ", ...> with differing trailing arguments:\n"
            << "  folly::SingletonThreadLocal<" << type_s << ", " << tag_s
            << ", " << make_0_s << ", " << tltag_0_s << ">\n"
            << "  folly::SingletonThreadLocal<" << type_s << ", " << tag_s
            << ", " << make_1_s << ", " << tltag_1_s << ">\n";
  std::abort();
}

} // namespace detail

} // namespace folly
