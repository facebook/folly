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

#ifndef FOLLY_TESTUTIL_H_
#define FOLLY_TESTUTIL_H_

#include <string>

namespace folly {
namespace test {

/**
 * Temporary file.
 *
 * By default, the file is created in a system-specific location (the value
 * of the TMPDIR environment variable, or /tmp), but you can override that
 * by making "prefix" be a path (containing a '/'; use a prefix starting with
 * './' to create a file in the current directory).
 *
 * By default, the file is closed and deleted when the TemporaryFile object
 * is destroyed, but both these behaviors can be overridden with arguments
 * to the constructor.
 */
class TemporaryFile {
 public:
  enum class Scope {
    PERMANENT,
    UNLINK_IMMEDIATELY,
    UNLINK_ON_DESTRUCTION
  };
  explicit TemporaryFile(const char* prefix=nullptr,
                         Scope scope=Scope::UNLINK_ON_DESTRUCTION,
                         bool closeOnDestruction=true);
  ~TemporaryFile();

  int fd() const { return fd_; }
  const std::string& path() const;

 private:
  Scope scope_;
  bool closeOnDestruction_;
  int fd_;
  std::string path_;
};

}  // namespace test
}  // namespace folly

#endif /* FOLLY_TESTUTIL_H_ */

