/*
 * Copyright 2015 Facebook, Inc.
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

#ifndef FOLLY_IO_FSUTIL_H_
#define FOLLY_IO_FSUTIL_H_

#include <boost/filesystem.hpp>

namespace folly {
namespace fs {

// Functions defined in this file are meant to extend the
// boost::filesystem library; functions will be named according to boost's
// naming conventions instead of ours.  For convenience, import the
// boost::filesystem namespace into folly::fs.
using namespace ::boost::filesystem;

/**
 * Check whether "path" starts with "prefix".
 * That is, if prefix has n path elements, then the first n elements of
 * path must be the same as prefix.
 *
 * There is a special case if prefix ends with a slash:
 * /foo/bar/ is not a prefix of /foo/bar, but both /foo/bar and /foo/bar/
 * are prefixes of /foo/bar/baz.
 */
bool starts_with(const path& p, const path& prefix);

/**
 * If "path" starts with "prefix", return "path" with "prefix" removed.
 * Otherwise, throw filesystem_error.
 */
path remove_prefix(const path& p, const path& prefix);

/**
 * Canonicalize the parent path, leaving the filename (last component)
 * unchanged.  You may use this before creating a file instead of
 * boost::filesystem::canonical, which requires that the entire path exists.
 */
path canonical_parent(const path& p, const path& basePath = current_path());

}  // namespace fs
}  // namespace folly

#endif /* FOLLY_IO_FSUTIL_H_ */
