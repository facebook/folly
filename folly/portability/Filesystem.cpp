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

#include <folly/portability/Filesystem.h>

namespace folly::fs {

#if __cpp_lib_filesystem >= 201703

path lexically_normal_fn::operator()(path const& p) const {
  return p.lexically_normal();
}

#elif __cpp_lib_experimental_filesystem >= 201406

//  mimic: std::filesystem::path::lexically_normal, C++17
//  from: https://github.com/gulrak/filesystem/tree/v1.5.0, MIT
path lexically_normal_fn::operator()(path const& p) const {
  path dest;
  bool lastDotDot = false;
  for (path::string_type s : p) {
    if (s == ".") {
      dest /= "";
      continue;
    } else if (s == ".." && !dest.empty()) {
      auto root = p.root_path();
      if (dest == root) {
        continue;
      } else if (*(--dest.end()) != "..") {
        auto drepr = dest.native();
        if (drepr.back() == path::preferred_separator) {
          drepr.pop_back();
          dest = std::move(drepr);
        }
        dest.remove_filename();
        continue;
      }
    }
    if (!(s.empty() && lastDotDot)) {
      dest /= s;
    }
    lastDotDot = s == "..";
  }
  if (dest.empty()) {
    dest = ".";
  }
  return dest;
}

#else
#error require filesystem
#endif

} // namespace folly::fs
