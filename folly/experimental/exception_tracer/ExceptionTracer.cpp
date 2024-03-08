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

#include <folly/experimental/exception_tracer/ExceptionTracer.h>

#include <cstdlib>
#include <exception>
#include <iostream>

#include <glog/logging.h>

#include <folly/CppAttributes.h>
#include <folly/Portability.h>
#include <folly/String.h>
#include <folly/experimental/exception_tracer/ExceptionAbi.h>
#include <folly/experimental/exception_tracer/StackTrace.h>
#include <folly/experimental/symbolizer/Symbolizer.h>

#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

#if defined(__GLIBCXX__)

#include <dlfcn.h>

namespace {

using namespace ::folly::exception_tracer;
using namespace ::folly::symbolizer;
using namespace __cxxabiv1;

extern "C" {
const StackTraceStack* getCaughtExceptionStackTraceStack(void)
    __attribute__((__weak__));
typedef const StackTraceStack* (*GetCaughtExceptionStackTraceStackType)();
GetCaughtExceptionStackTraceStackType getCaughtExceptionStackTraceStackFn;
}

} // namespace

namespace folly {
namespace exception_tracer {

std::ostream& operator<<(std::ostream& out, const ExceptionInfo& info) {
  printExceptionInfo(out, info, SymbolizePrinter::COLOR_IF_TTY);
  return out;
}

void printExceptionInfo(
    std::ostream& out, const ExceptionInfo& info, int options) {
  out << "Exception type: ";
  if (info.type) {
    out << folly::demangle(*info.type);
  } else {
    out << "(unknown type)";
  }
  static constexpr size_t kInternalFramesNumber = 3;

  // Skip our own internal frames.
  size_t frameCount = info.frames.size();
  if (frameCount <= kInternalFramesNumber) {
    out << "\n";
    return;
  }
  auto addresses = info.frames.data() + kInternalFramesNumber;
  frameCount -= kInternalFramesNumber;

  out << " (" << frameCount << (frameCount == 1 ? " frame" : " frames")
      << ")\n";
  try {
    std::vector<SymbolizedFrame> frames;
    frames.resize(frameCount);

    Symbolizer symbolizer(
        (options & SymbolizePrinter::NO_FILE_AND_LINE)
            ? LocationInfoMode::DISABLED
            : Symbolizer::kDefaultLocationInfoMode);
    symbolizer.symbolize(addresses, frames.data(), frameCount);

    OStreamSymbolizePrinter osp(out, options);
    osp.println(frames.data(), frameCount);
  } catch (const std::exception& e) {
    out << "\n !! caught " << folly::exceptionStr(e) << "\n";
  } catch (...) {
    out << "\n !!! caught unexpected exception\n";
  }
}

namespace {

/**
 * Is this a standard C++ ABI exception?
 *
 * Dependent exceptions (thrown via std::rethrow_exception) aren't --
 * exc doesn't actually point to a __cxa_exception structure, but
 * the offset of unwindHeader is correct, so exc->unwindHeader actually
 * returns a _Unwind_Exception object.  Yeah, it's ugly like that.
 *
 * Type of exception_class depends on ABI: on some it is defined as a
 * native endian uint64_t, on others a big endian char[8].
 */
struct ArmAbiTag {};
struct AnyAbiTag {};

[[maybe_unused]] bool isAbiCppException(ArmAbiTag, const char (&klazz)[8]) {
  return klazz[4] == 'C' && klazz[5] == '+' && klazz[6] == '+' &&
      klazz[7] == '\0';
}

[[maybe_unused]] bool isAbiCppException(AnyAbiTag, const uint64_t& klazz) {
  // The least significant four bytes must be "C++\0"
  static const uint64_t cppClass =
      ((uint64_t)'C' << 24) | ((uint64_t)'+' << 16) | ((uint64_t)'+' << 8);
  return (klazz & 0xffffffff) == cppClass;
}

bool isAbiCppException(const __cxa_exception* exc) {
  using tag = std::conditional_t<kIsArchArm, ArmAbiTag, AnyAbiTag>;
  return isAbiCppException(tag{}, exc->unwindHeader.exception_class);
}

} // namespace

std::vector<ExceptionInfo> getCurrentExceptions() {
  struct Once {
    Once() {
      // See if linked in with us (getCaughtExceptionStackTraceStack is weak)
      getCaughtExceptionStackTraceStackFn = getCaughtExceptionStackTraceStack;

      if (!getCaughtExceptionStackTraceStackFn) {
        // Nope, see if it's in a shared library
        getCaughtExceptionStackTraceStackFn =
            (GetCaughtExceptionStackTraceStackType)dlsym(
                RTLD_NEXT, "getCaughtExceptionStackTraceStack");
      }
    }
  };
  static Once once;

  std::vector<ExceptionInfo> exceptions;
  auto currentException = __cxa_get_globals()->caughtExceptions;
  if (!currentException) {
    return exceptions;
  }

  const StackTraceStack* traceStack = nullptr;
  if (!getCaughtExceptionStackTraceStackFn) {
    static bool logged = false;
    if (!logged) {
      LOG(WARNING)
          << "Exception tracer library not linked, stack traces not available";
      logged = true;
    }
  } else if ((traceStack = getCaughtExceptionStackTraceStackFn()) == nullptr) {
    static bool logged = false;
    if (!logged) {
      LOG(WARNING)
          << "Exception stack trace invalid, stack traces not available";
      logged = true;
    }
  }

  const StackTrace* trace = traceStack ? traceStack->top() : nullptr;
  while (currentException) {
    ExceptionInfo info;
    // Dependent exceptions (thrown via std::rethrow_exception) aren't
    // standard ABI __cxa_exception objects, and are correctly labeled as
    // such in the exception_class field.  We could try to extract the
    // primary exception type in horribly hacky ways, but, for now, nullptr.
    info.type = isAbiCppException(currentException)
        ? currentException->exceptionType
        : nullptr;

    if (traceStack) {
      LOG_IF(WARNING, !trace)
          << "Invalid trace stack for exception of type: "
          << (info.type ? folly::demangle(*info.type) : "null");

      if (!trace) {
        return {};
      }

      info.frames.assign(
          trace->addresses, trace->addresses + trace->frameCount);
      trace = traceStack->next(trace);
    }
    currentException = currentException->nextException;
    exceptions.push_back(std::move(info));
  }

  LOG_IF(WARNING, trace) << "Invalid trace stack!";

  return exceptions;
}

namespace {

std::terminate_handler origTerminate = abort;
std::unexpected_handler origUnexpected = abort;

void dumpExceptionStack(const char* prefix) {
  auto exceptions = getCurrentExceptions();
  if (exceptions.empty()) {
    return;
  }
  LOG(ERROR) << prefix << ", exception stack follows";
  for (auto& exc : exceptions) {
    LOG(ERROR) << exc << "\n";
  }
  LOG(ERROR) << "exception stack complete";
}

void terminateHandler() {
  dumpExceptionStack("terminate() called");
  origTerminate();
}

void unexpectedHandler() {
  dumpExceptionStack("Unexpected exception");
  origUnexpected();
}

} // namespace

void installHandlers() {
  struct Once {
    Once() {
      origTerminate = std::set_terminate(terminateHandler);
      origUnexpected = std::set_unexpected(unexpectedHandler);
    }
  };
  static Once once;
}

} // namespace exception_tracer
} // namespace folly

#endif // defined(__GLIBCXX__)

#endif // FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF
