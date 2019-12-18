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

#include <folly/experimental/symbolizer/SymbolizedFrame.h>

namespace folly {
namespace symbolizer {

namespace {

// Simplify a path -- as much as we can while not moving data around...
void simplifyPath(StringPiece& sp) {
  // Strip leading slashes and useless patterns (./), leaving one initial
  // slash.
  for (;;) {
    if (sp.empty()) {
      return;
    }

    // Strip leading slashes, leaving one.
    while (sp.startsWith("//")) {
      sp.advance(1);
    }

    if (sp.startsWith("/./")) {
      // Note 2, not 3, to keep it absolute
      sp.advance(2);
      continue;
    }

    if (sp.removePrefix("./")) {
      // Also remove any subsequent slashes to avoid making this path absolute.
      while (sp.startsWith('/')) {
        sp.advance(1);
      }
      continue;
    }

    break;
  }

  // Strip trailing slashes and useless patterns (/.).
  for (;;) {
    if (sp.empty()) {
      return;
    }

    // Strip trailing slashes, except when this is the root path.
    while (sp.size() > 1 && sp.removeSuffix('/')) {
    }

    if (sp.removeSuffix("/.")) {
      continue;
    }

    break;
  }
}
} // namespace

Path::Path(StringPiece baseDir, StringPiece subDir, StringPiece file)
    : baseDir_(baseDir), subDir_(subDir), file_(file) {
  using std::swap;

  // Normalize
  if (file_.empty()) {
    baseDir_.clear();
    subDir_.clear();
    return;
  }

  if (file_[0] == '/') {
    // file_ is absolute
    baseDir_.clear();
    subDir_.clear();
  }

  if (!subDir_.empty() && subDir_[0] == '/') {
    baseDir_.clear(); // subDir_ is absolute
  }

  simplifyPath(baseDir_);
  simplifyPath(subDir_);
  simplifyPath(file_);

  // Make sure it's never the case that baseDir_ is empty, but subDir_ isn't.
  if (baseDir_.empty()) {
    swap(baseDir_, subDir_);
  }
}

size_t Path::size() const {
  size_t size = 0;
  bool needsSlash = false;

  if (!baseDir_.empty()) {
    size += baseDir_.size();
    needsSlash = !baseDir_.endsWith('/');
  }

  if (!subDir_.empty()) {
    size += needsSlash;
    size += subDir_.size();
    needsSlash = !subDir_.endsWith('/');
  }

  if (!file_.empty()) {
    size += needsSlash;
    size += file_.size();
  }

  return size;
}

size_t Path::toBuffer(char* buf, size_t bufSize) const {
  size_t totalSize = 0;
  bool needsSlash = false;

  auto append = [&](StringPiece sp) {
    if (bufSize >= 2) {
      size_t toCopy = std::min(sp.size(), bufSize - 1);
      memcpy(buf, sp.data(), toCopy);
      buf += toCopy;
      bufSize -= toCopy;
    }
    totalSize += sp.size();
  };

  if (!baseDir_.empty()) {
    append(baseDir_);
    needsSlash = !baseDir_.endsWith('/');
  }
  if (!subDir_.empty()) {
    if (needsSlash) {
      append("/");
    }
    append(subDir_);
    needsSlash = !subDir_.endsWith('/');
  }
  if (!file_.empty()) {
    if (needsSlash) {
      append("/");
    }
    append(file_);
  }
  if (bufSize) {
    *buf = '\0';
  }
  assert(totalSize == size());
  return totalSize;
}

void Path::toString(std::string& dest) const {
  size_t initialSize = dest.size();
  dest.reserve(initialSize + size());
  if (!baseDir_.empty()) {
    dest.append(baseDir_.begin(), baseDir_.end());
  }
  if (!subDir_.empty()) {
    if (!dest.empty() && dest.back() != '/') {
      dest.push_back('/');
    }
    dest.append(subDir_.begin(), subDir_.end());
  }
  if (!file_.empty()) {
    if (!dest.empty() && dest.back() != '/') {
      dest.push_back('/');
    }
    dest.append(file_.begin(), file_.end());
  }
  assert(dest.size() == initialSize + size());
}

} // namespace symbolizer
} // namespace folly
