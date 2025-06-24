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

#include <cstdlib>
#include <cstring>

#ifndef _WIN32
#include <signal.h>
#endif

#include <folly/debugging/symbolizer/SignalHandler.h>
#include <folly/portability/SysMman.h>
#include <folly/portability/Unistd.h>

static bool has_opt(
    int const argc, char const* const* const argv, char const* const name) {
  for (int i = 1; i < argc; ++i) {
    auto const arg = argv[i];
    if (arg[0] == '-' && arg[1] == '-' && strcmp(arg + 2, name) == 0) {
      return true;
    }
  }
  return false;
}

[[maybe_unused]] static constexpr size_t kPageSize = 4096;

int main(int const argc, char const* const* const argv) {
  fprintf(stderr, "%d\n", int(getpid()));
  fflush(stderr);

#ifndef _WIN32
  if (has_opt(argc, argv, "altstack")) {
    auto const perms = PROT_READ | PROT_WRITE;
    auto const flags = MAP_ANONYMOUS | MAP_PRIVATE;
    auto const alloc = mmap(nullptr, kPageSize * 4, perms, flags, -1, 0);
    stack_t ss;
    ss.ss_sp = alloc;
    ss.ss_size = kPageSize * 4;
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);
  }
#endif

  if (has_opt(argc, argv, "handle")) {
    folly::symbolizer::installFatalSignalHandler();
  }

  auto const perms = PROT_NONE;
  auto const flags = MAP_ANONYMOUS | MAP_PRIVATE;
  auto const alloc = mmap(nullptr, kPageSize, perms, flags, -1, 0);
  return *static_cast<int const*>(alloc); // read the first int: segv
}
