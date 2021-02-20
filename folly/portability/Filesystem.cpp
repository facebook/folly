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

#include <folly/portability/Filesystem.h>

#include <boost/filesystem.hpp>

namespace folly::fs {

namespace boost_fs = boost::filesystem;

#if __cpp_lib_filesystem >= 201703

path lexically_normal_fn::operator()(path const& p) const {
  return p.lexically_normal();
}

#elif __cpp_lib_experimental_filesystem >= 201406

path lexically_normal_fn::operator()(path const& p) const {
  return path(boost_fs::path(p.native()).lexically_normal().native());
}

#else
#error require filesystem
#endif

} // namespace folly::fs
