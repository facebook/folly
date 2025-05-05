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

#include <folly/experimental/io/FsUtil.h>

#include <folly/Exception.h>

#ifdef __APPLE__
#include <mach-o/dyld.h> // @manual
#endif

namespace bsys = ::boost::system;

namespace folly {
namespace fs {

namespace {
bool skipPrefix(const path& pth, const path& prefix, path::const_iterator& it) {
  it = pth.begin();
  for (auto& p : prefix) {
    if (it == pth.end()) {
      return false;
    }
    if (p == ".") {
      // Should only occur at the end, if prefix ends with a slash
      continue;
    }
    if (*it++ != p) {
      return false;
    }
  }
  return true;
}
} // namespace

bool starts_with(const path& pth, const path& prefix) {
  path::const_iterator it;
  return skipPrefix(pth, prefix, it);
}

path remove_prefix(const path& pth, const path& prefix) {
  path::const_iterator it;
  if (!skipPrefix(pth, prefix, it)) {
    throw filesystem_error(
        "Path does not start with prefix",
        pth,
        prefix,
        bsys::errc::make_error_code(bsys::errc::invalid_argument));
  }

  path p;
  for (; it != pth.end(); ++it) {
    p /= *it;
  }

  return p;
}

path canonical_parent(const path& pth, const path& base) {
  return canonical(pth.parent_path(), base) / pth.filename();
}

path executable_path() {
#ifdef __APPLE__
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buf(size - 1, '\0');
  auto data = const_cast<char*>(&*buf.data());
  _NSGetExecutablePath(data, &size);
  return path(std::move(buf));
#else
  return read_symlink("/proc/self/exe");
#endif
}

} // namespace fs
} // namespace folly
