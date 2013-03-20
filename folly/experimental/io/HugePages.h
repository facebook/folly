/*
 * Copyright 2013 Facebook, Inc.
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

#ifndef FOLLY_IO_HUGEPAGES_H_
#define FOLLY_IO_HUGEPAGES_H_

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <boost/operators.hpp>

#include "folly/Range.h"
#include "folly/experimental/io/FsUtil.h"

namespace folly {

struct HugePageSize : private boost::totally_ordered<HugePageSize> {
  HugePageSize() : size(0) { }
  explicit HugePageSize(size_t s) : size(s) { }
  size_t size;
  fs::path mountPoint;
};

inline bool operator<(const HugePageSize& a, const HugePageSize& b) {
  return a.size < b.size;
}

inline bool operator==(const HugePageSize& a, const HugePageSize& b) {
  return a.size == b.size;
}

/**
 * Vector of (huge_page_size, mount_point), sorted by huge_page_size.
 * mount_point might be empty if no hugetlbfs file system is mounted for
 * that size.
 */
typedef std::vector<HugePageSize> HugePageSizeVec;

/**
 * Class to interface with Linux huge pages (hugetlbfs).
 */
class HugePages {
 public:
  HugePages();

  /**
   * Get list of supported huge page sizes and their mount points, if
   * hugetlbfs file systems are mounted for those sizes.
   */
  const HugePageSizeVec& sizes() const { return sizes_; }

  /**
   * Return the mount point for the requested huge page size.
   * 0 = use smallest available.
   * Throws on error.
   */
  const HugePageSize& getSize(size_t hugePageSize = 0) const;

  /**
   * Create a file on a huge page filesystem containing a copy of the data
   * from data.  If multiple huge page sizes are allowed, we
   * pick the smallest huge page size available, unless you request one
   * explicitly with the hugePageSize argument.
   *
   * The "path" argument must be rooted under the mount point for the
   * selected huge page size.  If relative, it is considered relative to the
   * mount point.
   *
   * We return a struct File structure containing the full path and size
   * (rounded up to a multiple of the huge page size)
   */
  struct File {
    File() : size(0) { }
    fs::path path;
    size_t size;
  };
  File create(
      ByteRange data, const fs::path& path,
      HugePageSize hugePageSize = HugePageSize()) const;

 private:
  HugePageSizeVec sizes_;
};

}  // namespace folly

#endif /* FOLLY_IO_HUGEPAGES_H_ */

