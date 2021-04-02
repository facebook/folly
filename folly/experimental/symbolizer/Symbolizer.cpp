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

#include <folly/experimental/symbolizer/Symbolizer.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include <folly/FileUtil.h>
#include <folly/Memory.h>
#include <folly/ScopeGuard.h>
#include <folly/String.h>
#include <folly/experimental/symbolizer/Dwarf.h>
#include <folly/experimental/symbolizer/Elf.h>
#include <folly/experimental/symbolizer/ElfCache.h>
#include <folly/experimental/symbolizer/LineReader.h>
#include <folly/experimental/symbolizer/detail/Debug.h>
#include <folly/lang/SafeAssert.h>
#include <folly/lang/ToAscii.h>
#include <folly/portability/Config.h>
#include <folly/portability/SysMman.h>
#include <folly/portability/Unistd.h>
#include <folly/tracing/AsyncStack.h>

#if FOLLY_HAVE_SWAPCONTEXT
// folly/portability/Config.h (thus features.h) must be included
// first, and _XOPEN_SOURCE must be defined to unable the context
// functions on macOS.
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE
#endif
#include <ucontext.h>
#endif

#if FOLLY_HAVE_BACKTRACE
#include <execinfo.h>
#endif

namespace folly {
namespace symbolizer {

namespace {
template <typename PrintFunc>
void printAsyncStackInfo(PrintFunc print) {
  char buf[to_ascii_size_max<16, uint64_t>];
  auto printHex = [&print, &buf](uint64_t val) {
    print("0x");
    print(StringPiece(buf, to_ascii_lower<16>(buf, val)));
  };

  // Print async stack trace, if available
  const auto* asyncStackRoot = tryGetCurrentAsyncStackRoot();
  const auto* asyncStackFrame =
      asyncStackRoot ? asyncStackRoot->getTopFrame() : nullptr;

  print("\n");
  print("*** Check failure async stack trace: ***\n");
  print("*** First async stack root: ");
  printHex((uint64_t)asyncStackRoot);
  print(", normal stack frame pointer holding async stack root: ");
  printHex(
      asyncStackRoot ? (uint64_t)asyncStackRoot->getStackFramePointer() : 0);
  print(", return address: ");
  printHex(asyncStackRoot ? (uint64_t)asyncStackRoot->getReturnAddress() : 0);
  print(" ***\n");
  print("*** First async stack frame pointer: ");
  printHex((uint64_t)asyncStackFrame);
  print(", return address: ");
  printHex(asyncStackFrame ? (uint64_t)asyncStackFrame->getReturnAddress() : 0);
  print(", async stack trace: ***\n");
}
} // namespace

#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

namespace {

ElfCache* defaultElfCache() {
  static auto cache = new ElfCache();
  return cache;
}

void setSymbolizedFrame(
    SymbolizedFrame& frame,
    const std::shared_ptr<ElfFile>& file,
    uintptr_t address,
    LocationInfoMode mode,
    folly::Range<SymbolizedFrame*> extraInlineFrames = {}) {
  frame.clear();
  frame.found = true;

  auto sym = file->getDefinitionByAddress(address);
  if (!sym.first) {
    return;
  }

  frame.addr = address;
  frame.file = file;
  frame.name = file->getSymbolName(sym);

  Dwarf(file.get())
      .findAddress(address, mode, frame.location, extraInlineFrames);
}

} // namespace

bool Symbolizer::isAvailable() {
  return detail::get_r_debug();
}

Symbolizer::Symbolizer(
    ElfCacheBase* cache, LocationInfoMode mode, size_t symbolCacheSize)
    : cache_(cache ? cache : defaultElfCache()), mode_(mode) {
  if (symbolCacheSize > 0) {
    symbolCache_.emplace(folly::in_place, symbolCacheSize);
  }
}

size_t Symbolizer::symbolize(
    folly::Range<const uintptr_t*> addrs,
    folly::Range<SymbolizedFrame*> frames) {
  size_t addrCount = addrs.size();
  size_t frameCount = frames.size();
  FOLLY_SAFE_CHECK(addrCount <= frameCount, "Not enough frames.");
  size_t remaining = addrCount;

  auto const dbg = detail::get_r_debug();
  if (dbg == nullptr) {
    return 0;
  }
  if (dbg->r_version != 1) {
    return 0;
  }

  char selfPath[PATH_MAX + 8];
  ssize_t selfSize;
  if ((selfSize = readlink("/proc/self/exe", selfPath, PATH_MAX + 1)) == -1) {
    // Something has gone terribly wrong.
    return 0;
  }
  selfPath[selfSize] = '\0';

  for (size_t i = 0; i < addrCount; i++) {
    frames[i].addr = addrs[i];
  }

  // Find out how many frames were filled in.
  auto countFrames = [](folly::Range<SymbolizedFrame*> framesRange) {
    return std::distance(
        framesRange.begin(),
        std::find_if(framesRange.begin(), framesRange.end(), [&](auto frame) {
          return !frame.found;
        }));
  };

  for (auto lmap = dbg->r_map; lmap != nullptr && remaining != 0;
       lmap = lmap->l_next) {
    // The empty string is used in place of the filename for the link_map
    // corresponding to the running executable.  Additionally, the `l_addr' is
    // 0 and the link_map appears to be first in the list---but none of this
    // behavior appears to be documented, so checking for the empty string is
    // as good as anything.
    auto const objPath = lmap->l_name[0] != '\0' ? lmap->l_name : selfPath;

    auto const elfFile = cache_->getFile(objPath);
    if (!elfFile) {
      continue;
    }

    for (size_t i = 0; i < addrCount && remaining != 0; ++i) {
      auto& frame = frames[i];
      if (frame.found) {
        continue;
      }

      auto const addr = frame.addr;
      if (symbolCache_) {
        // Need a write lock, because EvictingCacheMap brings found item to
        // front of eviction list.
        auto lockedSymbolCache = symbolCache_->wlock();

        auto const iter = lockedSymbolCache->find(addr);
        if (iter != lockedSymbolCache->end()) {
          size_t numCachedFrames = countFrames(folly::range(iter->second));
          // 1 entry in cache is the non-inlined function call and that one
          // already has space reserved at `frames[i]`
          auto numInlineFrames = numCachedFrames - 1;
          if (numInlineFrames <= frameCount - addrCount) {
            // Move the rest of the frames to make space for inlined frames.
            std::move_backward(
                frames.begin() + i + 1,
                frames.begin() + addrCount,
                frames.begin() + addrCount + numInlineFrames);
            // Overwrite frames[i] too (the non-inlined function call entry).
            std::copy(
                iter->second.begin(),
                iter->second.begin() + numInlineFrames + 1,
                frames.begin() + i);
            i += numInlineFrames;
            addrCount += numInlineFrames;
          }
          continue;
        }
      }

      // Get the unrelocated, ELF-relative address by normalizing via the
      // address at which the object is loaded.
      auto const adjusted = addr - reinterpret_cast<uintptr_t>(lmap->l_addr);
      size_t numInlined = 0;
      if (elfFile->getSectionContainingAddress(adjusted)) {
        if (mode_ == LocationInfoMode::FULL_WITH_INLINE &&
            frameCount > addrCount) {
          size_t maxInline = std::min<size_t>(
              Dwarf::kMaxInlineLocationInfoPerFrame, frameCount - addrCount);
          // First use the trailing empty frames (index starting from addrCount)
          // to get the inline call stack, then rotate these inline functions
          // before the caller at `frame[i]`.
          folly::Range<SymbolizedFrame*> inlineFrameRange(
              frames.begin() + addrCount,
              frames.begin() + addrCount + maxInline);
          setSymbolizedFrame(frame, elfFile, adjusted, mode_, inlineFrameRange);

          numInlined = countFrames(inlineFrameRange);
          // Rotate inline frames right before its caller frame.
          std::rotate(
              frames.begin() + i,
              frames.begin() + addrCount,
              frames.begin() + addrCount + numInlined);
          addrCount += numInlined;
        } else {
          setSymbolizedFrame(frame, elfFile, adjusted, mode_);
        }
        --remaining;
        if (symbolCache_) {
          // frame may already have been set here.  That's ok, we'll just
          // overwrite, which doesn't cause a correctness problem.
          CachedSymbolizedFrames cacheFrames;
          std::copy(
              frames.begin() + i,
              frames.begin() + i + std::min(numInlined + 1, cacheFrames.size()),
              cacheFrames.begin());
          symbolCache_->wlock()->set(addr, cacheFrames);
        }
        // Skip over the newly added inlined items.
        i += numInlined;
      }
    }
  }

  return addrCount;
}

FastStackTracePrinter::FastStackTracePrinter(
    std::unique_ptr<SymbolizePrinter> printer, size_t symbolCacheSize)
    : printer_(std::move(printer)),
      symbolizer_(defaultElfCache(), LocationInfoMode::FULL, symbolCacheSize) {}

FastStackTracePrinter::~FastStackTracePrinter() = default;

void FastStackTracePrinter::printStackTrace(bool symbolize) {
  SCOPE_EXIT { printer_->flush(); };

  FrameArray<kMaxStackTraceDepth> addresses;
  auto printStack = [this, &addresses, &symbolize] {
    if (symbolize) {
      symbolizer_.symbolize(addresses);

      // Skip the top 2 frames:
      // getStackTraceSafe
      // FastStackTracePrinter::printStackTrace (here)
      printer_->println(addresses, 2);
    } else {
      printer_->print("(safe mode, symbolizer not available)\n");
      AddressFormatter formatter;
      for (size_t i = 0; i < addresses.frameCount; ++i) {
        printer_->print(formatter.format(addresses.addresses[i]));
        printer_->print("\n");
      }
    }
  };

  if (!getStackTraceSafe(addresses)) {
    printer_->print("(error retrieving stack trace)\n");
  } else {
    printStack();
  }

  addresses.frameCount = 0;
  if (!getAsyncStackTraceSafe(addresses) || addresses.frameCount == 0) {
    return;
  }
  printAsyncStackInfo([this](auto sp) { printer_->print(sp); });
  printStack();
}

void FastStackTracePrinter::flush() {
  printer_->flush();
}

#endif // FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

SafeStackTracePrinter::SafeStackTracePrinter(int fd)
    : fd_(fd),
      printer_(
          fd,
          SymbolizePrinter::COLOR_IF_TTY,
          size_t(64) << 10), // 64KiB
      addresses_(std::make_unique<FrameArray<kMaxStackTraceDepth>>()) {}

void SafeStackTracePrinter::flush() {
  printer_.flush();
  fsyncNoInt(fd_);
}

void SafeStackTracePrinter::printSymbolizedStackTrace() {
#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF
  // This function might run on an alternative stack allocated by
  // UnsafeSelfAllocateStackTracePrinter. Capturing a stack from
  // here is probably wrong.

  // Do our best to populate location info, process is going to terminate,
  // so performance isn't critical.
  SignalSafeElfCache elfCache_;
  Symbolizer symbolizer(&elfCache_, LocationInfoMode::FULL);
  symbolizer.symbolize(*addresses_);

  // Skip the top 2 frames captured by printStackTrace:
  // getStackTraceSafe
  // SafeStackTracePrinter::printStackTrace (captured stack)
  //
  // Leaving signalHandler on the stack for clarity, I think.
  printer_.println(*addresses_, 2);
#else
  printUnsymbolizedStackTrace();
#endif
}

void SafeStackTracePrinter::printUnsymbolizedStackTrace() {
  print("(safe mode, symbolizer not available)\n");
#if FOLLY_HAVE_BACKTRACE
  // `backtrace_symbols_fd` from execinfo.h is not explicitly
  // documented on either macOS or Linux to be async-signal-safe, but
  // the implementation in
  // https://opensource.apple.com/source/Libc/Libc-1353.60.8/ appears
  // safe.
  ::backtrace_symbols_fd(
      reinterpret_cast<void**>(addresses_->addresses),
      addresses_->frameCount,
      fd_);
#else
  AddressFormatter formatter;
  for (size_t i = 0; i < addresses_->frameCount; ++i) {
    print(formatter.format(addresses_->addresses[i]));
    print("\n");
  }
#endif
}

void SafeStackTracePrinter::printStackTrace(bool symbolize) {
  SCOPE_EXIT { flush(); };

  // Skip the getStackTrace frame
  if (!getStackTraceSafe(*addresses_)) {
    print("(error retrieving stack trace)\n");
  } else if (symbolize) {
    printSymbolizedStackTrace();
  } else {
    printUnsymbolizedStackTrace();
  }

  addresses_->frameCount = 0;
  if (!getAsyncStackTraceSafe(*addresses_) || addresses_->frameCount == 0) {
    return;
  }
  printAsyncStackInfo([this](auto sp) { print(sp); });
  if (symbolize) {
    printSymbolizedStackTrace();
  } else {
    printUnsymbolizedStackTrace();
  }
}

std::string getAsyncStackTraceStr() {
#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

  // Get and symbolize stack trace
  constexpr size_t kMaxStackTraceDepth = 100;
  FrameArray<kMaxStackTraceDepth> addresses;

  if (!getAsyncStackTraceSafe(addresses)) {
    return "";
  } else {
    ElfCache elfCache;
    Symbolizer symbolizer(&elfCache);
    symbolizer.symbolize(addresses);

    StringSymbolizePrinter printer;
    printer.println(addresses);
    return printer.str();
  }

#else

  return "";

#endif // FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF
}

#if FOLLY_HAVE_SWAPCONTEXT

// Stack utilities used by UnsafeSelfAllocateStackTracePrinter
namespace {
// Size of mmap-allocated stack. Not to confuse with sigaltstack.
const size_t kMmapStackSize = 1 * 1024 * 1024;

using MmapPtr = std::unique_ptr<char, void (*)(char*)>;

MmapPtr getNull() {
  return MmapPtr(nullptr, [](char*) {});
}

// Assign a mmap-allocated stack to oucp.
// Return a non-empty smart pointer on success.
MmapPtr allocateStack(ucontext_t* oucp, size_t pageSize) {
  MmapPtr p(
      (char*)mmap(
          nullptr,
          kMmapStackSize,
          PROT_WRITE | PROT_READ,
          MAP_ANONYMOUS | MAP_PRIVATE,
          /* fd */ -1,
          /* offset */ 0),
      [](char* addr) {
        // Usually runs inside a fatal signal handler.
        // Error handling is skipped.
        munmap(addr, kMmapStackSize);
      });

  if (!p) {
    return getNull();
  }

  // Prepare read-only guard pages on both ends
  if (pageSize * 2 >= kMmapStackSize) {
    return getNull();
  }
  size_t upperBound = ((kMmapStackSize - 1) / pageSize) * pageSize;
  if (mprotect(p.get(), pageSize, PROT_NONE) != 0) {
    return getNull();
  }
  if (mprotect(p.get() + upperBound, kMmapStackSize - upperBound, PROT_NONE) !=
      0) {
    return getNull();
  }

  oucp->uc_stack.ss_sp = p.get() + pageSize;
  oucp->uc_stack.ss_size = upperBound - pageSize;
  oucp->uc_stack.ss_flags = 0;

  return p;
}

} // namespace

FOLLY_PUSH_WARNING

// On Apple platforms, some ucontext methods that are used here are deprecated.
#ifdef __APPLE__
FOLLY_GNU_DISABLE_WARNING("-Wdeprecated-declarations")
#endif

void UnsafeSelfAllocateStackTracePrinter::printSymbolizedStackTrace() {
  if (pageSizeUnchecked_ <= 0) {
    return;
  }

  ucontext_t cur;
  memset(&cur, 0, sizeof(cur));
  ucontext_t alt;
  memset(&alt, 0, sizeof(alt));

  if (getcontext(&alt) != 0) {
    return;
  }
  alt.uc_link = &cur;

  MmapPtr p = allocateStack(&alt, (size_t)pageSizeUnchecked_);
  if (!p) {
    return;
  }

  auto contextStart = [](UnsafeSelfAllocateStackTracePrinter* that) {
    that->SafeStackTracePrinter::printSymbolizedStackTrace();
  };

  makecontext(
      &alt,
      (void (*)())(void (*)(UnsafeSelfAllocateStackTracePrinter*))(
          contextStart),
      /* argc */ 1,
      /* arg */ this);
  // NOTE: swapcontext is not async-signal-safe
  if (swapcontext(&cur, &alt) != 0) {
    return;
  }
}

FOLLY_POP_WARNING

#endif // FOLLY_HAVE_SWAPCONTEXT

} // namespace symbolizer
} // namespace folly
