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

#ifndef FOLLY_TESTUTIL_H_
#define FOLLY_TESTUTIL_H_

#include <map>
#include <string>
#include <folly/Range.h>
#include <folly/experimental/io/FsUtil.h>

namespace folly {
namespace test {

/**
 * Temporary file.
 *
 * By default, the file is created in a system-specific location (the value
 * of the TMPDIR environment variable, or /tmp), but you can override that
 * with a different (non-empty) directory passed to the constructor.
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
  explicit TemporaryFile(StringPiece namePrefix = StringPiece(),
                         fs::path dir = fs::path(),
                         Scope scope = Scope::UNLINK_ON_DESTRUCTION,
                         bool closeOnDestruction = true);
  ~TemporaryFile();

  // Movable, but not copiable
  TemporaryFile(TemporaryFile&&) = default;
  TemporaryFile& operator=(TemporaryFile&&) = default;

  int fd() const { return fd_; }
  const fs::path& path() const;

 private:
  Scope scope_;
  bool closeOnDestruction_;
  int fd_;
  fs::path path_;
};

/**
 * Temporary directory.
 *
 * By default, the temporary directory is created in a system-specific
 * location (the value of the TMPDIR environment variable, or /tmp), but you
 * can override that with a non-empty directory passed to the constructor.
 *
 * By default, the directory is recursively deleted when the TemporaryDirectory
 * object is destroyed, but that can be overridden with an argument
 * to the constructor.
 */

class TemporaryDirectory {
 public:
  enum class Scope {
    PERMANENT,
    DELETE_ON_DESTRUCTION
  };
  explicit TemporaryDirectory(StringPiece namePrefix = StringPiece(),
                              fs::path dir = fs::path(),
                              Scope scope = Scope::DELETE_ON_DESTRUCTION);
  ~TemporaryDirectory();

  // Movable, but not copiable
  TemporaryDirectory(TemporaryDirectory&&) = default;
  TemporaryDirectory& operator=(TemporaryDirectory&&) = default;

  const fs::path& path() const { return path_; }

 private:
  Scope scope_;
  fs::path path_;
};

/**
 * Changes into a temporary directory, and deletes it with all its contents
 * upon destruction, also changing back to the original working directory.
 */
class ChangeToTempDir {
public:
  ChangeToTempDir();
  ~ChangeToTempDir();

  // Movable, but not copiable
  ChangeToTempDir(ChangeToTempDir&&) = default;
  ChangeToTempDir& operator=(ChangeToTempDir&&) = default;

  const fs::path& path() const { return dir_.path(); }

private:
  fs::path initialPath_;
  TemporaryDirectory dir_;
};

/**
 * Easy PCRE regex matching. Note that pattern must match the ENTIRE target,
 * so use .* at the start and end of the pattern, as appropriate.  See
 * http://regex101.com/ for a PCRE simulator.
 */
#define EXPECT_PCRE_MATCH(pattern_stringpiece, target_stringpiece) \
  EXPECT_PRED2( \
    ::folly::test::detail::hasPCREPatternMatch, \
    pattern_stringpiece, \
    target_stringpiece \
  )
#define EXPECT_NO_PCRE_MATCH(pattern_stringpiece, target_stringpiece) \
  EXPECT_PRED2( \
    ::folly::test::detail::hasNoPCREPatternMatch, \
    pattern_stringpiece, \
    target_stringpiece \
  )

namespace detail {
  bool hasPCREPatternMatch(StringPiece pattern, StringPiece target);
  bool hasNoPCREPatternMatch(StringPiece pattern, StringPiece target);
}  // namespace detail

/**
 * Use these patterns together with CaptureFD and EXPECT_PCRE_MATCH() to
 * test for the presence (or absence) of log lines at a particular level:
 *
 *   CaptureFD stderr(2);
 *   LOG(INFO) << "All is well";
 *   EXPECT_NO_PCRE_MATCH(glogErrOrWarnPattern(), stderr.readIncremental());
 *   LOG(ERROR) << "Uh-oh";
 *   EXPECT_PCRE_MATCH(glogErrorPattern(), stderr.readIncremental());
 */
inline std::string glogErrorPattern() { return ".*(^|\n)E[0-9].*"; }
inline std::string glogWarningPattern() { return ".*(^|\n)W[0-9].*"; }
// Error OR warning
inline std::string glogErrOrWarnPattern() { return ".*(^|\n)[EW][0-9].*"; }

/**
 * Temporarily capture a file descriptor by redirecting it into a file.
 * You can consume its output either all-at-once or incrementally.
 * Great for testing logging (see also glog*Pattern()).
 */
class CaptureFD {
public:
  explicit CaptureFD(int fd);
  ~CaptureFD();

  /**
   * Restore the captured FD to its original state. It can be useful to do
   * this before the destructor so that you can read() the captured data and
   * log about it to the formerly captured stderr or stdout.
   */
  void release();

  /**
   * Reads the whole file into a string, but does not remove the redirect.
   */
  std::string read();

  /**
   * Read any bytes that were appended to the file since the last
   * readIncremental.  Great for testing line-by-line output.
   */
  std::string readIncremental();

private:
  TemporaryFile file_;

  int fd_;
  int oldFDCopy_;  // equal to fd_ after restore()

  off_t readOffset_;  // for incremental reading
};

#ifndef _MSC_VER
class EnvVarSaver {
public:
  EnvVarSaver();
  ~EnvVarSaver();
private:
  std::map<std::string, std::string> saved_;
};
#endif

}  // namespace test
}  // namespace folly

#endif /* FOLLY_TESTUTIL_H_ */
