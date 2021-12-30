/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#pragma once

#include <forward_list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/intrusive/avl_set.hpp>

#include <folly/Range.h>
#include <folly/experimental/symbolizer/Elf.h>
#include <folly/hash/Hash.h>
#include <folly/memory/ReentrantAllocator.h>
#include <folly/portability/Config.h>

namespace folly {
namespace symbolizer {

#if FOLLY_HAVE_ELF

class ElfCacheBase {
 public:
  virtual std::shared_ptr<ElfFile> getFile(StringPiece path) = 0;
  virtual ~ElfCacheBase() {}
};

/**
 * Cache ELF files. Async-signal-safe: does memory allocation via mmap.
 *
 * Not MT-safe. May not be used concurrently from multiple threads.
 */
class SignalSafeElfCache : public ElfCacheBase {
 public:
  std::shared_ptr<ElfFile> getFile(StringPiece path) override;

  //  Path
  //
  //  A minimal implementation of the subset of std::string used below, as if:
  //
  //    using Path = std::basic_string<
  //        char, std::char_traits<char>, reentrant_allocator<char>>;
  //
  //  Since some library implementations of std::basic_string, as on CentOS 7,
  //  do not build when instantiated with a non-default-constructible allocator,
  //  and since other library replacements, such as folly::basic_fbstring, just
  //  ignore the allocator parameter.
  class Path {
   public:
    Path(
        char const* data,
        std::size_t size,
        reentrant_allocator<char> const& alloc) noexcept;
    Path() = delete;
    Path(Path const&) = delete;
    void operator=(Path const&) = delete;

    /* implicit */ operator StringPiece() const noexcept { return data_; }

    char const* c_str() const noexcept { return data_.data(); }

    friend bool operator<(Path const& a, Path const& b) noexcept {
      return a.data_ < b.data_;
    }

   private:
    std::vector<char, reentrant_allocator<char>> data_;
  };

  struct Entry : boost::intrusive::avl_set_base_hook<> {
    Path path;
    std::shared_ptr<ElfFile> file;
    bool init = false;

    explicit Entry(StringPiece p, reentrant_allocator<char> alloc) noexcept
        : path{p.data(), p.size(), alloc},
          file{std::allocate_shared<ElfFile>(alloc)} {}
    Entry(Entry const&) = delete;
    Entry& operator=(Entry const& that) = delete;

    friend bool operator<(Entry const& a, Entry const& b) noexcept {
      return a.path < b.path;
    }
  };

  struct State {
    reentrant_allocator<void> alloc{
        reentrant_allocator_options().block_size_lg(16).large_size_lg(12)};
    std::forward_list<Entry, reentrant_allocator<Entry>> list{alloc};
    // note: map entry dtors check that they have already been unlinked
    boost::intrusive::avl_set<Entry> map; // must follow list
  };
  Optional<State> state_;
};

/**
 * General-purpose ELF file cache.
 *
 * LRU of given capacity. MT-safe (uses locking). Not async-signal-safe.
 */
class ElfCache : public ElfCacheBase {
 public:
  std::shared_ptr<ElfFile> getFile(StringPiece path) override;

 private:
  std::mutex mutex_;

  struct Entry {
    std::string path;
    ElfFile file;
  };

  static std::shared_ptr<ElfFile> filePtr(const std::shared_ptr<Entry>& e);

  std::unordered_map<StringPiece, std::shared_ptr<Entry>, Hash> files_;
};

#endif // FOLLY_HAVE_ELF

} // namespace symbolizer
} // namespace folly
