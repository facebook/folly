/*
 * Copyright 2012 Facebook, Inc.
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

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "folly/Range.h"

namespace folly {

/**
 * Vector of (huge_page_size, mount_point), sorted by huge_page_size.
 * mount_point might be empty if no hugetlbfs file system is mounted for
 * that size.
 */
typedef std::vector<std::pair<size_t, std::string>> HugePageSizeVec;

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
   * Create a file on a huge page filesystem containing a copy of the data
   * from data.  If multiple huge page sizes are allowed, we
   * pick the smallest huge page size available, unless you request one
   * explicitly with the hugePageSize argument.
   *
   * We return a struct File structure containing the full path and size
   * (rounded up to a multiple of the huge page size)
   */
  struct File {
    std::string path;
    size_t size;
  };
  File create(
      ByteRange data, StringPiece baseName, size_t hugePageSize = 0,
      mode_t mode = 0644) const;

 private:
  HugePageSizeVec sizes_;
};

}  // namespace folly

#endif /* FOLLY_IO_HUGEPAGES_H_ */

