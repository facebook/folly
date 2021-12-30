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

#include <vector>

#include <folly/Benchmark.h>
#include <folly/File.h>
#include <folly/Subprocess.h>

using namespace folly;

BENCHMARK(spawn_with_close_fds, iters) {
  auto options = Subprocess::Options().closeOtherFds();
  std::vector<std::string> cmd{"/bin/true"};
  for (size_t i = 0; i < iters; ++i) {
    Subprocess proc{cmd, options};
    proc.wait();
  }
}

BENCHMARK(spawn_without_close_fds, iters) {
  std::vector<std::string> cmd{"/bin/true"};
  for (size_t i = 0; i < iters; ++i) {
    Subprocess proc{cmd};
    proc.wait();
  }
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  // Create 512 descriptors
  int rootfd = ::open("/", O_RDONLY);
  assert(rootfd != -1);
  std::vector<folly::File> descriptors;
  const bool kTakeOwnership = true;
  for (int i = 0; i < 512; ++i) {
    descriptors.emplace_back(::dup(rootfd), kTakeOwnership);
  }

  folly::runBenchmarks();
}
