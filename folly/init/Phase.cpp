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

#include <folly/init/Phase.h>

#include <atomic>
#include <cstdlib>
#include <stdexcept>

namespace folly {

namespace {

static std::atomic<ProcessPhase> process_phase{ProcessPhase::Init};

void set_process_phase(ProcessPhase newphase) {
  ProcessPhase curphase = get_process_phase();
  if (curphase == ProcessPhase::Exit || int(newphase) - int(curphase) != 1) {
    throw std::logic_error("folly-init: unexpected process-phase transition");
  }
  process_phase.store(newphase, std::memory_order_relaxed);
}

} // namespace

void set_process_phases() {
  set_process_phase(ProcessPhase::Regular);
  std::atexit([]() { set_process_phase(ProcessPhase::Exit); });
}

ProcessPhase get_process_phase() noexcept {
  return process_phase.load(std::memory_order_relaxed);
}

} // namespace folly
