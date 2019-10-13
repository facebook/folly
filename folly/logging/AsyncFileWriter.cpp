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

#include <folly/logging/AsyncFileWriter.h>

#include <folly/Exception.h>
#include <folly/FileUtil.h>
#include <folly/logging/LoggerDB.h>

namespace folly {

AsyncFileWriter::AsyncFileWriter(StringPiece path)
    : AsyncFileWriter{File{path.str(), O_WRONLY | O_APPEND | O_CREAT}} {}

AsyncFileWriter::AsyncFileWriter(folly::File&& file) : file_{std::move(file)} {}

AsyncFileWriter::~AsyncFileWriter() {
  cleanup();
}

bool AsyncFileWriter::ttyOutput() const {
  return isatty(file_.fd());
}

void AsyncFileWriter::writeToFile(
    std::vector<std::string>* ioQueue,
    size_t numDiscarded) {
  // kNumIovecs controls the maximum number of strings we write at once in a
  // single writev() call.
  constexpr int kNumIovecs = 64;
  std::array<iovec, kNumIovecs> iovecs;

  size_t idx = 0;
  while (idx < ioQueue->size()) {
    int numIovecs = 0;
    while (numIovecs < kNumIovecs && idx < ioQueue->size()) {
      const auto& str = (*ioQueue)[idx];
      iovecs[numIovecs].iov_base = const_cast<char*>(str.data());
      iovecs[numIovecs].iov_len = str.size();
      ++numIovecs;
      ++idx;
    }

    auto ret = folly::writevFull(file_.fd(), iovecs.data(), numIovecs);
    folly::checkUnixError(ret, "writeFull() failed");
  }

  if (numDiscarded > 0) {
    auto msg = getNumDiscardedMsg(numDiscarded);
    if (!msg.empty()) {
      auto ret = folly::writeFull(file_.fd(), msg.data(), msg.size());
      // We currently ignore errors from writeFull() here.
      // There's not much we can really do.
      (void)ret;
    }
  }
}

void AsyncFileWriter::performIO(
    std::vector<std::string>* ioQueue,
    size_t numDiscarded) {
  try {
    writeToFile(ioQueue, numDiscarded);
  } catch (const std::exception& ex) {
    LoggerDB::internalWarning(
        __FILE__,
        __LINE__,
        "error writing to log file ",
        file_.fd(),
        " in AsyncFileWriter: ",
        folly::exceptionStr(ex));
  }
}

std::string AsyncFileWriter::getNumDiscardedMsg(size_t numDiscarded) {
  // We may want to make this customizable in the future (e.g., to allow it to
  // conform to the LogFormatter style being used).
  // For now just return a simple fixed message.
  return folly::to<std::string>(
      numDiscarded,
      " log messages discarded: logging faster than we can write\n");
}

} // namespace folly
