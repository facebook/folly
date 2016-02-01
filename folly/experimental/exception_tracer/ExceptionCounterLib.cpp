/*
 * Copyright 2016 Facebook, Inc.
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

#include <folly/experimental/exception_tracer/ExceptionCounterLib.h>

#include <iosfwd>
#include <unordered_map>

#include <folly/RWSpinLock.h>
#include <folly/Synchronized.h>
#include <folly/ThreadLocal.h>

#include <folly/experimental/exception_tracer/ExceptionTracerLib.h>
#include <folly/experimental/exception_tracer/StackTrace.h>
#include <folly/experimental/symbolizer/Symbolizer.h>

using namespace folly::exception_tracer;

namespace {

// We are using hash of the stack trace to uniquely identify the exception
using ExceptionId = uintptr_t;

using ExceptionStatsHolderType =
    std::unordered_map<ExceptionId, ExceptionStats>;

struct ExceptionStatsStorage {
  void appendTo(ExceptionStatsHolderType& data) {
    ExceptionStatsHolderType tempHolder;
    SYNCHRONIZED(statsHolder) {
      using std::swap;
      swap(statsHolder, tempHolder);
    }

    for (const auto& myData : tempHolder) {
      const auto& myStat = myData.second;

      auto it = data.find(myData.first);
      if (it != data.end()) {
        it->second.count += myStat.count;
      } else {
        data.insert(myData);
      }
    }
  }

  folly::Synchronized<ExceptionStatsHolderType, folly::RWSpinLock> statsHolder;
};

class Tag {};

folly::ThreadLocal<ExceptionStatsStorage, Tag> gExceptionStats;

} // namespace

namespace folly {
namespace exception_tracer {

std::vector<ExceptionStats> getExceptionStatistics() {
  ExceptionStatsHolderType accumulator;
  for (auto& threadStats : gExceptionStats.accessAllThreads()) {
    threadStats.appendTo(accumulator);
  }

  std::vector<ExceptionStats> result;
  result.reserve(accumulator.size());
  for (const auto& item : accumulator) {
    result.push_back(item.second);
  }

  std::sort(result.begin(),
            result.end(),
            [](const ExceptionStats& lhs, const ExceptionStats& rhs) {
              return (lhs.count > rhs.count);
            });

  return result;
}

std::ostream& operator<<(std::ostream& out, const ExceptionStats& stats) {
  out << "Exception report: " << std::endl;
  out << "Exception count: " << stats.count << std::endl;
  out << stats.info;

  return out;
}

} // namespace exception_tracer
} // namespace folly

namespace {

/*
 * This handler gathers statistics on all exceptions thrown by the program
 * Information is being stored in thread local storage.
 */
void throwHandler(void*, std::type_info* exType, void (*)(void*)) noexcept {
  ExceptionInfo info;
  info.type = exType;
  auto& frames = info.frames;

  frames.resize(kMaxFrames);
  auto n = folly::symbolizer::getStackTrace(frames.data(), kMaxFrames);

  if (n == -1) {
    LOG(ERROR) << "Invalid stack frame";
    return;
  }

  frames.resize(n);
  auto exceptionId = folly::hash::hash_range(frames.begin(), frames.end());

  SYNCHRONIZED(holder, gExceptionStats->statsHolder) {
    auto it = holder.find(exceptionId);
    if (it != holder.end()) {
      ++it->second.count;
    } else {
      holder.emplace(exceptionId, ExceptionStats{1, std::move(info)});
    }
  }
}

struct Initializer {
  Initializer() { registerCxaThrowCallback(throwHandler); }
};

Initializer initializer;

} // namespace
