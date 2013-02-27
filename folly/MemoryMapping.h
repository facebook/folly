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

#ifndef FOLLY_MEMORYMAPPING_H_
#define FOLLY_MEMORYMAPPING_H_

#include "folly/FBString.h"
#include "folly/File.h"
#include "folly/Range.h"
#include <glog/logging.h>
#include <boost/noncopyable.hpp>

namespace folly {

/**
 * Maps files in memory (read-only).
 *
 * @author Tudor Bosman (tudorb@fb.com)
 */
class MemoryMapping : boost::noncopyable {
 public:
  /**
   * Lock the pages in memory?
   * TRY_LOCK  = try to lock, log warning if permission denied
   * MUST_LOCK = lock, fail assertion if permission denied.
   */
  enum class LockMode {
    TRY_LOCK,
    MUST_LOCK
  };
  /**
   * Map a portion of the file indicated by filename in memory, causing a CHECK
   * failure on error.
   *
   * By default, map the whole file.  length=-1: map from offset to EOF.
   * Unlike the mmap() system call, offset and length don't need to be
   * page-aligned.  length is clipped to the end of the file if it's too large.
   *
   * The mapping will be destroyed (and the memory pointed-to by data() will
   * likely become inaccessible) when the MemoryMapping object is destroyed.
   */
  explicit MemoryMapping(File file,
                         off_t offset=0,
                         off_t length=-1);

  virtual ~MemoryMapping();

  /**
   * Lock the pages in memory
   */
  bool mlock(LockMode lock);

  /**
   * Unlock the pages.
   * If dontneed is true, the kernel is instructed to release these pages
   * (per madvise(MADV_DONTNEED)).
   */
  void munlock(bool dontneed=false);

  /**
   * Hint that these pages will be scanned linearly.
   * madvise(MADV_SEQUENTIAL)
   */
  void hintLinearScan();

  /**
   * Advise the kernel about memory access.
   */
  void advise(int advice) const;

  /**
   * A bitwise cast of the mapped bytes as range of values. Only intended for
   * use with POD or in-place usable types.
   */
  template<class T>
  Range<const T*> asRange() const {
    size_t count = data_.size() / sizeof(T);
    return Range<const T*>(static_cast<const T*>(
                             static_cast<const void*>(data_.data())),
                           count);
  }

  /**
   * A range of bytes mapped by this mapping.
   */
  Range<const uint8_t*> range() const {
    return {data_.begin(), data_.end()};
  }

  /**
   * Return the memory area where the file was mapped.
   */
  StringPiece data() const {
    return asRange<const char>();
  }

  bool mlocked() const {
    return locked_;
  }

  int fd() const { return file_.fd(); }

 protected:
  MemoryMapping();

  void init(File file,
            off_t offset, off_t length,
            int prot,
            bool grow);

  File file_;
  void* mapStart_;
  off_t mapLength_;
  bool locked_;
  Range<uint8_t*> data_;
};

/**
 * Maps files in memory for writing.
 *
 * @author Tom Jackson (tjackson@fb.com)
 */
class WritableMemoryMapping : public MemoryMapping {
 public:
  explicit WritableMemoryMapping(File file,
                                 off_t offset = 0,
                                 off_t length = -1);
  /**
   * A bitwise cast of the mapped bytes as range of mutable values. Only
   * intended for use with POD or in-place usable types.
   */
  template<class T>
  Range<T*> asWritableRange() const {
    size_t count = data_.size() / sizeof(T);
    return Range<T*>(static_cast<T*>(
                       static_cast<void*>(data_.data())),
                     count);
  }

  /**
   * A range of mutable bytes mapped by this mapping.
   */
  Range<uint8_t*> writableRange() const {
    return data_;
  }
};

}  // namespace folly

#endif /* FOLLY_MEMORYMAPPING_H_ */
