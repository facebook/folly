/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/detail/SingletonStackTrace.h>

#include <folly/experimental/symbolizer/ElfCache.h>
#include <folly/experimental/symbolizer/Symbolizer.h>
#include <folly/portability/Config.h>

namespace folly {
namespace detail {

std::string getSingletonStackTrace() {
#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

  // Get and symbolize stack trace
  constexpr size_t kMaxStackTraceDepth = 100;
  auto addresses =
      std::make_unique<symbolizer::FrameArray<kMaxStackTraceDepth>>();

  if (!getStackTraceSafe(*addresses)) {
    return "";
  } else {
    symbolizer::ElfCache elfCache;

    symbolizer::Symbolizer symbolizer(&elfCache);
    symbolizer.symbolize(*addresses);

    symbolizer::StringSymbolizePrinter printer;
    printer.println(*addresses);
    return printer.str();
  }

#else

  return "";

#endif // FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF
}

} // namespace detail
} // namespace folly
