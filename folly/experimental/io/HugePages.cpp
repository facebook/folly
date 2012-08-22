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

#include "folly/experimental/io/HugePages.h"

#include <sys/mman.h>

#include <cctype>
#include <cstring>

#include <algorithm>
#include <stdexcept>
#include <system_error>

#include <boost/filesystem.hpp>
#include <boost/noncopyable.hpp>
#include <boost/regex.hpp>

#include <glog/logging.h>

#include "folly/Conv.h"
#include "folly/Format.h"
#include "folly/Range.h"
#include "folly/ScopeGuard.h"
#include "folly/String.h"
#include "folly/experimental/io/Stream.h"

namespace fs = ::boost::filesystem;

namespace folly {

namespace {

// Get the default huge page size
size_t getDefaultHugePageSize() {
  // We need to parse /proc/meminfo
  static const boost::regex regex(R"!(Hugepagesize:\s*(\d+)\s*kB)!");
  boost::cmatch match;
  for (auto& byteLine : byLine("/proc/meminfo")) {
    StringPiece line(byteLine);
    if (boost::regex_match(line.begin(), line.end(), match, regex)) {
      StringPiece numStr(line.begin() + match.position(1), match.length(1));
      return to<size_t>(numStr) * 1024;  // in KiB
    }
  }
  throw std::runtime_error("Can't find default huge page size");
}

// Get raw huge page sizes (without mount points, they'll be filled later)
HugePageSizeVec getRawHugePageSizes() {
  // We need to parse file names from /sys/kernel/mm/hugepages
  static const boost::regex regex(R"!(hugepages-(\d+)kB)!");
  boost::smatch match;
  HugePageSizeVec vec;
  fs::path path("/sys/kernel/mm/hugepages");
  for (fs::directory_iterator it(path); it != fs::directory_iterator(); ++it) {
    std::string filename(it->path().filename().native());
    if (boost::regex_match(filename, match, regex)) {
      StringPiece numStr(filename.data() + match.position(1), match.length(1));
      vec.emplace_back(to<size_t>(numStr) * 1024, "");
    }
  }
  return vec;
}

// Parse the value of a pagesize mount option
// Format: number, optional K/M/G/T suffix, trailing junk allowed
size_t parsePageSizeValue(StringPiece value) {
  static const boost::regex regex(R"!((\d+)([kmgt])?.*)!", boost::regex::icase);
  boost::cmatch match;
  if (!boost::regex_match(value.begin(), value.end(), match, regex)) {
    throw std::runtime_error("Invalid pagesize option");
  }
  char c = '\0';
  if (match.length(2) != 0) {
    c = tolower(value[match.position(2)]);
  }
  StringPiece numStr(value.data() + match.position(1), match.length(1));
  size_t size = to<size_t>(numStr);
  switch (c) {
  case 't': size *= 1024;
  case 'g': size *= 1024;
  case 'm': size *= 1024;
  case 'k': size *= 1024;
  }
  return size;
}

/**
 * Get list of supported huge page sizes and their mount points, if
 * hugetlbfs file systems are mounted for those sizes.
 */
HugePageSizeVec getHugePageSizes() {
  HugePageSizeVec sizeVec = getRawHugePageSizes();
  if (sizeVec.empty()) {
    return sizeVec;  // nothing to do
  }
  std::sort(sizeVec.begin(), sizeVec.end());

  size_t defaultHugePageSize = getDefaultHugePageSize();

  struct PageSizeLess {
    bool operator()(const std::pair<size_t, std::string>& a, size_t b) const {
      return a.first < b;
    }
    bool operator()(size_t a, const std::pair<size_t, std::string>& b) const {
      return a < b.first;
    }
  };

  // Read and parse /proc/mounts
  std::vector<StringPiece> parts;
  std::vector<StringPiece> options;
  for (auto& byteLine : byLine("/proc/mounts")) {
    StringPiece line(byteLine);
    parts.clear();
    split(" ", line, parts);
    // device path fstype options uid gid
    if (parts.size() != 6) {
      throw std::runtime_error("Invalid /proc/mounts line");
    }
    if (parts[2] != "hugetlbfs") {
      continue;  // we only care about hugetlbfs
    }

    options.clear();
    split(",", parts[3], options);
    size_t pageSize = defaultHugePageSize;
    // Search for the "pagesize" option, which must have a value
    for (auto& option : options) {
      // key=value
      const char* p = static_cast<const char*>(
          memchr(option.data(), '=', option.size()));
      if (!p) {
        continue;
      }
      if (StringPiece(option.data(), p) != "pagesize") {
        continue;
      }
      pageSize = parsePageSizeValue(StringPiece(p + 1, option.end()));
      break;
    }

    auto pos = std::lower_bound(sizeVec.begin(), sizeVec.end(), pageSize,
                                PageSizeLess());
    if (pos == sizeVec.end() || pos->first != pageSize) {
      throw std::runtime_error("Mount page size not found");
    }
    if (pos->second.empty()) {
      // Store mount point
      pos->second.assign(parts[1].data(), parts[1].size());
    }
  }

  return sizeVec;
}

// RAII wrapper around an open file, closes on exit unless you call release()
class ScopedFd : private boost::noncopyable {
 public:
  explicit ScopedFd(int fd) : fd_(fd) { }
  int fd() const { return fd_; }

  void release() {
    fd_ = -1;
  }

  void close() {
    if (fd_ == -1) {
      return;
    }
    int r = ::close(fd_);
    fd_ = -1;
    if (r == -1) {
      throw std::system_error(errno, std::system_category(), "close failed");
    }
  }

  ~ScopedFd() {
    try {
      close();
    } catch (...) {
      PLOG(ERROR) << "close failed!";
    }
  }

 private:
  int fd_;
};

// RAII wrapper that deletes a file upon destruction unless you call release()
class ScopedDeleter : private boost::noncopyable {
 public:
  explicit ScopedDeleter(std::string name) : name_(std::move(name)) { }
  void release() {
    name_.clear();
  }

  ~ScopedDeleter() {
    if (name_.empty()) {
      return;
    }
    int r = ::unlink(name_.c_str());
    if (r == -1) {
      PLOG(ERROR) << "unlink failed";
    }
  }
 private:
  std::string name_;
};

// RAII wrapper around a mmap mapping, munmaps upon destruction unless you
// call release()
class ScopedMmap : private boost::noncopyable {
 public:
  ScopedMmap(void* start, size_t size) : start_(start), size_(size) { }

  void* start() const { return start_; }
  size_t size() const { return size_; }

  void release() {
    start_ = MAP_FAILED;
  }

  void munmap() {
    if (start_ == MAP_FAILED) {
      return;
    }
    int r = ::munmap(start_, size_);
    start_ = MAP_FAILED;
    if (r == -1) {
      throw std::system_error(errno, std::system_category(), "munmap failed");
    }
  }

  ~ScopedMmap() {
    try {
      munmap();
    } catch (...) {
      PLOG(ERROR) << "munmap failed!";
    }
  }
 private:
  void* start_;
  size_t size_;
};

}  // namespace

HugePages::HugePages() : sizes_(getHugePageSizes()) { }

HugePages::File HugePages::create(ByteRange data,
                                  StringPiece baseName,
                                  size_t hugePageSize,
                                  mode_t mode) const {
  // Pick an appropriate size.
  StringPiece mountPath;
  if (hugePageSize == 0) {
    for (auto& p : sizes_) {
      if (p.second.empty()) {
        continue;  // not mounted
      }
      hugePageSize = p.first;
      mountPath = StringPiece(p.second);
      break;
    }
    if (hugePageSize == 0) {
      throw std::runtime_error("No huge page filesystem mounted");
    }
  } else {
    // Linear search is just fine
    for (auto& p : sizes_) {
      if (p.first == hugePageSize) {
        if (p.second.empty()) {
          throw std::runtime_error(
              "No huge page filesystem mounted with requested page size");
        }
        mountPath = StringPiece(p.second);
      }
    }
    if (mountPath.empty()) {
      throw std::runtime_error("Requested huge page size not found");
    }
  }

  // Round size up
  File file;
  file.size = data.size() / hugePageSize * hugePageSize;
  if (file.size != data.size()) {
    file.size += hugePageSize;
  }

  file.path = folly::format("{}/{}", mountPath, baseName).str();
  ScopedFd fd(open(file.path.c_str(), O_RDWR | O_CREAT | O_TRUNC, mode));
  if (fd.fd() == -1) {
    throw std::system_error(errno, std::system_category(), "open failed");
  }

  ScopedDeleter deleter(file.path);

  ScopedMmap map(mmap(nullptr, file.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                      fd.fd(), 0),
                 file.size);
  if (map.start() == MAP_FAILED) {
    throw std::system_error(errno, std::system_category(), "mmap failed");
  }

  memcpy(map.start(), data.data(), data.size());

  map.munmap();
  deleter.release();
  fd.close();

  return file;
}

}  // namespace folly

