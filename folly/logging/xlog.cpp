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

#include <folly/logging/xlog.h>

#include <type_traits>
#include <unordered_map>

#include <folly/Synchronized.h>
#include <folly/portability/PThread.h>

namespace folly {

namespace detail {
size_t& xlogEveryNThreadEntry(void const* const key) {
  using Map = std::unordered_map<void const*, size_t>;

  static auto pkey = [] {
    pthread_key_t k;
    pthread_key_create(&k, [](void* arg) {
      auto& map = *static_cast<Map**>(arg);
      delete map;
      // This destructor occurs during some arbitrary stage of thread teardown.
      // But some subsequent stage may invoke this function again! In which case
      // the map, which has already been deleted, must be recreated and a fresh
      // counter returned. Clearing the map pointer here signals that the map
      // has been deleted and that the next call to this function in the same
      // thread must recreate the map.
      map = nullptr;
    });
    return k;
  }();
  thread_local Map* map;

  if (!map) {
    pthread_setspecific(pkey, &map);
    map = new Map();
  }
  return (*map)[key];
}
} // namespace detail

namespace {
#ifdef FOLLY_XLOG_SUPPORT_BUCK1
/**
 * buck copies header files from their original location in the source tree
 * and places them under buck-out/ with a path like
 * buck-out/<rule-name-components>/<original-path>
 *
 * We want to strip off the buck-out/<rule-name-components> portion,
 * so that the filename we use is just the original path in the source tree.
 *
 * The <rule-name-component> section should always end in a path component that
 * includes a '#': it's format is <rule-name>#<parameters>, where <parameters>
 * is a comma separated list that never includes '/'.
 *
 * Search for the first path component with a '#', and strip off everything up
 * to this component.
 */
StringPiece stripBuckOutPrefix(StringPiece filename) {
  size_t idx = 0;
  while (true) {
    auto end = filename.find('/', idx);
    if (end == StringPiece::npos) {
      // We were unable to find where the buck-out prefix should end.
      return filename;
    }

    auto component = filename.subpiece(idx, end - idx);
    if (component.find('#') != StringPiece::npos) {
      return filename.subpiece(end + 1);
    }
    idx = end + 1;
  }
}
#endif // FOLLY_XLOG_SUPPORT_BUCK1

#ifdef FOLLY_XLOG_SUPPORT_BUCK2
/**
 * buck2 copies header files from their original location in the source tree
 * and places them under buck-out/ with a path like
 * buck-out/v2/gen/cell/hash/dirA/dirB/__rule_name__/buck-headers/dirA/dirB/Foo.h
 *
 * We want to strip off everything up to and including the "buck-headers" or
 * "buck-private-headers" component.
 */
StringPiece stripBuckV2Prefix(StringPiece filename) {
  static constexpr StringPiece commonPrefix("/buck-");
  static constexpr StringPiece headerPrefix("headers/");
  static constexpr StringPiece privatePrefix("private-headers/");

  size_t idx = 0;
  while (true) {
    // Look for directory strings starting with "/buck-"
    auto end = filename.find(commonPrefix, idx);
    if (end == StringPiece::npos) {
      // We were unable to find where the buck-out prefix should end.
      return filename;
    }

    // Check if this directory is either "/buck-headers/" or
    // "/buck-private-headers/".  If so, return the path stripped to here.
    const auto remainder = filename.subpiece(end + commonPrefix.size());
    if (remainder.startsWith(headerPrefix)) {
      return remainder.subpiece(headerPrefix.size());
    }
    if (remainder.startsWith(privatePrefix)) {
      return remainder.subpiece(privatePrefix.size());
    }

    // This directory was "/buck-something-else".  Keep looking.
    idx = end + 1;
  }
}
#endif // FOLLY_XLOG_SUPPORT_BUCK2

} // namespace

StringPiece getXlogCategoryNameForFile(StringPiece filename) {
  // Buck mangles the directory layout for header files.  Rather than including
  // them from their original location, it moves them into deep directories
  // inside buck-out, and includes them from there.
  //
  // If this path looks like a buck header directory, try to strip off the
  // buck-specific portion.
#ifdef FOLLY_XLOG_SUPPORT_BUCK1
  if (filename.startsWith("buck-out/")) {
    filename = stripBuckOutPrefix(filename);
  }
#endif // FOLLY_XLOG_SUPPORT_BUCK1
#ifdef FOLLY_XLOG_SUPPORT_BUCK2
  if (filename.startsWith("buck-out/v2/")) {
    filename = stripBuckV2Prefix(filename);
  }
#endif // FOLLY_XLOG_SUPPORT_BUCK2

  return filename;
}

template <bool IsInHeaderFile>
LogLevel XlogLevelInfo<IsInHeaderFile>::loadLevelFull(
    folly::StringPiece categoryName, bool isOverridden) {
  auto currentLevel = level_.load(std::memory_order_acquire);
  if (FOLLY_UNLIKELY(currentLevel == ::folly::LogLevel::UNINITIALIZED)) {
    return LoggerDB::get().xlogInit(
        isOverridden ? categoryName : getXlogCategoryNameForFile(categoryName),
        &level_,
        nullptr);
  }
  return currentLevel;
}

template <bool IsInHeaderFile>
LogCategory* XlogCategoryInfo<IsInHeaderFile>::init(
    folly::StringPiece categoryName, bool isOverridden) {
  return LoggerDB::get().xlogInitCategory(
      isOverridden ? categoryName : getXlogCategoryNameForFile(categoryName),
      &category_,
      &isInitialized_);
}

LogLevel XlogLevelInfo<false>::loadLevelFull(
    folly::StringPiece categoryName,
    bool isOverridden,
    XlogFileScopeInfo* fileScopeInfo) {
  auto currentLevel = fileScopeInfo->level.load(std::memory_order_acquire);
  if (FOLLY_UNLIKELY(currentLevel == ::folly::LogLevel::UNINITIALIZED)) {
    return LoggerDB::get().xlogInit(
        isOverridden ? categoryName : getXlogCategoryNameForFile(categoryName),
        &fileScopeInfo->level,
        &fileScopeInfo->category);
  }
  return currentLevel;
}

// Explicitly instantiations of XlogLevelInfo and XlogCategoryInfo
template class XlogLevelInfo<true>;
template class XlogCategoryInfo<true>;
} // namespace folly
