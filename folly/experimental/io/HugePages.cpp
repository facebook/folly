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

#include "folly/experimental/io/HugePages.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include <cctype>
#include <cstring>

#include <algorithm>
#include <stdexcept>
#include <system_error>

#include <boost/noncopyable.hpp>
#include <boost/regex.hpp>

#include <glog/logging.h>

#include "folly/Conv.h"
#include "folly/Format.h"
#include "folly/Range.h"
#include "folly/ScopeGuard.h"
#include "folly/String.h"

#include "folly/experimental/Gen.h"
#include "folly/experimental/FileGen.h"
#include "folly/experimental/StringGen.h"

namespace folly {

namespace {

// Get the default huge page size
size_t getDefaultHugePageSize() {
  // We need to parse /proc/meminfo
  static const boost::regex regex(R"!(Hugepagesize:\s*(\d+)\s*kB)!");
  size_t pageSize = 0;
  boost::cmatch match;

  bool error = gen::byLine("/proc/meminfo") |
    [&] (StringPiece line) -> bool {
      if (boost::regex_match(line.begin(), line.end(), match, regex)) {
        StringPiece numStr(line.begin() + match.position(1), match.length(1));
        pageSize = to<size_t>(numStr) * 1024;  // in KiB
        return false;  // stop
      }
      return true;
    };

  if (error) {
    throw std::runtime_error("Can't find default huge page size");
  }
  return pageSize;
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
      vec.emplace_back(to<size_t>(numStr) * 1024);
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
    bool operator()(const HugePageSize& a, size_t b) const {
      return a.size < b;
    }
    bool operator()(size_t a, const HugePageSize& b) const {
      return a < b.size;
    }
  };

  // Read and parse /proc/mounts
  std::vector<StringPiece> parts;
  std::vector<StringPiece> options;

  gen::byLine("/proc/mounts") | gen::eachAs<StringPiece>() |
    [&](StringPiece line) {
      parts.clear();
      split(" ", line, parts);
      // device path fstype options uid gid
      if (parts.size() != 6) {
        throw std::runtime_error("Invalid /proc/mounts line");
      }
      if (parts[2] != "hugetlbfs") {
        return;  // we only care about hugetlbfs
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
      if (pos == sizeVec.end() || pos->size != pageSize) {
        throw std::runtime_error("Mount page size not found");
      }
      if (pos->mountPoint.empty()) {
        // Store mount point
        pos->mountPoint = fs::canonical(fs::path(parts[1].begin(),
                                                 parts[1].end()));
      }
    };

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
  explicit ScopedDeleter(fs::path name) : name_(std::move(name)) { }
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
  fs::path name_;
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

const HugePageSize& HugePages::getSize(size_t hugePageSize) const {
  // Linear search is just fine.
  for (auto& p : sizes_) {
    if (p.mountPoint.empty()) {
      continue;  // not mounted
    }
    if (hugePageSize == 0 || hugePageSize == p.size) {
      return p;
    }
  }
  throw std::runtime_error("Huge page not supported / not mounted");
}

HugePages::File HugePages::create(ByteRange data,
                                  const fs::path& path,
                                  HugePageSize hugePageSize) const {
  namespace bsys = ::boost::system;
  if (hugePageSize.size == 0) {
    hugePageSize = getSize();
  }

  // Round size up
  File file;
  file.size = data.size() / hugePageSize.size * hugePageSize.size;
  if (file.size != data.size()) {
    file.size += hugePageSize.size;
  }

  {
    file.path = fs::canonical_parent(path, hugePageSize.mountPoint);
    if (!fs::starts_with(file.path, hugePageSize.mountPoint)) {
      throw fs::filesystem_error(
          "HugePages::create: path not rooted at mount point",
          file.path, hugePageSize.mountPoint,
          bsys::errc::make_error_code(bsys::errc::invalid_argument));
    }
  }
  ScopedFd fd(open(file.path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666));
  if (fd.fd() == -1) {
    throw std::system_error(errno, std::system_category(), "open failed");
  }

  ScopedDeleter deleter(file.path);

  ScopedMmap map(mmap(nullptr, file.size, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_POPULATE, fd.fd(), 0),
                 file.size);
  if (map.start() == MAP_FAILED) {
    throw std::system_error(errno, std::system_category(), "mmap failed");
  }

  // Ignore madvise return code
  madvise(const_cast<unsigned char*>(data.data()), data.size(),
          MADV_SEQUENTIAL);
  // Why is this not memcpy, you ask?
  // The SSSE3-optimized memcpy in glibc likes to copy memory backwards,
  // rendering any prefetching from madvise useless (even harmful).
  const unsigned char* src = data.data();
  size_t size = data.size();
  unsigned char* dest = reinterpret_cast<unsigned char*>(map.start());
  if (reinterpret_cast<uintptr_t>(src) % 8 == 0) {
    const uint64_t* src8 = reinterpret_cast<const uint64_t*>(src);
    size_t size8 = size / 8;
    uint64_t* dest8 = reinterpret_cast<uint64_t*>(dest);
    while (size8--) {
      *dest8++ = *src8++;
    }
    src = reinterpret_cast<const unsigned char*>(src8);
    dest = reinterpret_cast<unsigned char*>(dest8);
    size %= 8;
  }
  memcpy(dest, src, size);

  map.munmap();
  deleter.release();
  fd.close();

  return file;
}

}  // namespace folly

