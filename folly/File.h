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

#ifndef FOLLY_FILE_H_
#define FOLLY_FILE_H_

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

namespace folly {

/**
 * A File represents an open file.
 */
class File {
 public:
  /**
   * Creates an empty File object, for late initialization.
   */
  File();

  /**
   * Create a File object from an existing file descriptor.
   * Takes ownership of the file descriptor if ownsFd is true.
   */
  /* implicit */ File(int fd,
                      bool ownsFd = false);

  /**
   * Open and create a file object.  Throws on error.
   */
  /* implicit */ File(const char* name,
                      int flags = O_RDONLY,
                      mode_t mode = 0644);

  ~File();

  /**
   * Create and return a temporary, owned file (uses tmpfile()).
   */
  static File temporary();

  /**
   * Return the file descriptor, or -1 if the file was closed.
   */
  int fd() const { return fd_; }

  /**
   * Returns 'true' iff the file was successfully opened.
   */
  explicit operator bool() const {
    return fd_ >= 0;
  }

  /**
   * If we own the file descriptor, close the file and throw on error.
   * Otherwise, do nothing.
   */
  void close();

  /**
   * Closes the file (if owned).  Returns true on success, false (and sets
   * errno) on error.
   */
  bool closeNoThrow();

  /**
   * Releases the file descriptor; no longer owned by this File.
   */
  void release();

  /**
   * Swap this File with another.
   */
  void swap(File& other);

  // movable
  File(File&&);
  File& operator=(File&&);

 private:
  // unique
  File(const File&) = delete;
  File& operator=(const File&) = delete;

  int fd_;
  bool ownsFd_;
};

void swap(File& a, File& b);

}  // namespace folly

#endif /* FOLLY_FILE_H_ */
