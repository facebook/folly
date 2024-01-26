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

/**
 * A standalone shared library that can be `LD_PRELOAD`d to print symbolized
 * native stack traces when the process dies due to a signal. By default, this
 * is enabled for SIGSEGV, SIGILL, SIGBUS, SIGSTKFLT, SIGABRT, and SIGFPE.
 *
 * Based on glibc's `libSegFault.so`:
 * https://github.com/lattera/glibc/blob/master/debug/segfault.c
 *
 * Usage:
 *
 *   $ LD_PRELOAD=libFollySegFault.so <my_prog> ...
 *
 */

#include <folly/experimental/symbolizer/SignalHandler.h>

#include <bitset>
#include <csignal>
#include <cstdlib>
#include <string_view>
#include <unordered_map>

#include <folly/Range.h>

static void __attribute__((constructor)) install_handler(void) {
  static const std::unordered_map<std::string_view, int> signalMap = {
      {"segv", SIGSEGV},
      {"ill", SIGILL},
#ifdef SIGBUS
      {"bus", SIGBUS},
#endif
#ifdef SIGSTKFLT
      {"stkflt", SIGSTKFLT},
#endif
      {"abrt", SIGABRT},
      {"fpe", SIGFPE},
  };

  std::bitset<64> sigMask;

  // The string description of signals to install the handler for.
  folly::StringPiece sigEnv = std::getenv("FOLLY_SEGFAULT_SIGNALS") ?: "all";
  while (!sigEnv.empty()) {
    std::string_view sigName = sigEnv.split_step(" ");

    // If signame of all was given, add in all signals.
    if (sigName == "all") {
      for (const auto& ent : signalMap) {
        sigMask.set(ent.second);
      }
    } else {
      auto sig = signalMap.find(sigName);
      if (sig == signalMap.end()) {
        fprintf(
            stderr,
            "unknown signal: \"%.*s\"\n",
            static_cast<int>(sigName.length()),
            sigName.data());
        std::abort();
      }
      sigMask.set(sig->second);
    }
  }

  folly::symbolizer::installFatalSignalHandler(sigMask);
}
