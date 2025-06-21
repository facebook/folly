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

#include <folly/debugging/symbolizer/SignalHandler.h>
#include <folly/portability/SysMman.h>
#include <folly/portability/Unistd.h>

static bool should_install_handlers(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--handle") == 0) {
      return true;
    }
  }
  return false;
}

int main(int argc, char** argv) {
  fprintf(stderr, "%d\n", int(getpid()));
  fflush(stderr);

  if (should_install_handlers(argc, argv)) {
    folly::symbolizer::installFatalSignalHandler();
  }

  auto const flags = MAP_ANONYMOUS | MAP_PRIVATE;
  auto const alloc = mmap(nullptr, 4096, PROT_NONE, flags, -1, 0);
  return *static_cast<int const*>(alloc); // read the first int: segv
}
