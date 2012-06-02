/*
 * Copyright 2012 Facebook, Inc.
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

#include "folly/experimental/io/IOBufQueue.h"

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <iostream>
#include <stdexcept>
#include <string.h>

using folly::IOBuf;
using folly::IOBufQueue;
using std::pair;
using std::string;
using std::unique_ptr;

// String Comma Length macro for string literals
#define SCL(x) (x), sizeof(x) - 1

namespace {

IOBufQueue::Options clOptions;
struct Initializer {
  Initializer() {
    clOptions.cacheChainLength = true;
  }
};
Initializer initializer;

unique_ptr<IOBuf>
stringToIOBuf(const char* s, uint32_t len) {
  unique_ptr<IOBuf> buf = IOBuf::create(len);
  memcpy(buf->writableTail(), s, len);
  buf->append(len);
  return std::move(buf);
}

void checkConsistency(const IOBufQueue& queue) {
  if (queue.options().cacheChainLength) {
    size_t len = queue.front() ? queue.front()->computeChainDataLength() : 0;
    EXPECT_EQ(len, queue.chainLength());
  }
}

}

TEST(IOBufQueue, Simple) {
  IOBufQueue queue(clOptions);
  EXPECT_EQ(NULL, queue.front());
  queue.append(SCL(""));
  EXPECT_EQ(NULL, queue.front());
  queue.append(unique_ptr<IOBuf>());
  EXPECT_EQ(NULL, queue.front());
  string emptyString;
  queue.append(emptyString);
  EXPECT_EQ(NULL, queue.front());
}

TEST(IOBufQueue, Append) {
  IOBufQueue queue(clOptions);
  queue.append(SCL("Hello"));
  IOBufQueue queue2(clOptions);
  queue2.append(SCL(", "));
  queue2.append(SCL("World"));
  checkConsistency(queue);
  checkConsistency(queue2);
  queue.append(queue2.move());
  checkConsistency(queue);
  checkConsistency(queue2);
  const IOBuf* chain = queue.front();
  EXPECT_NE((IOBuf*)NULL, chain);
  EXPECT_EQ(12, chain->computeChainDataLength());
  EXPECT_EQ(NULL, queue2.front());
}

TEST(IOBufQueue, Append2) {
  IOBufQueue queue(clOptions);
  queue.append(SCL("Hello"));
  IOBufQueue queue2(clOptions);
  queue2.append(SCL(", "));
  queue2.append(SCL("World"));
  checkConsistency(queue);
  checkConsistency(queue2);
  queue.append(queue2);
  checkConsistency(queue);
  checkConsistency(queue2);
  const IOBuf* chain = queue.front();
  EXPECT_NE((IOBuf*)NULL, chain);
  EXPECT_EQ(12, chain->computeChainDataLength());
  EXPECT_EQ(NULL, queue2.front());
}

TEST(IOBufQueue, Split) {
  IOBufQueue queue(clOptions);
  queue.append(stringToIOBuf(SCL("Hello")));
  queue.append(stringToIOBuf(SCL(",")));
  queue.append(stringToIOBuf(SCL(" ")));
  queue.append(stringToIOBuf(SCL("")));
  queue.append(stringToIOBuf(SCL("World")));
  checkConsistency(queue);
  EXPECT_EQ(12, queue.front()->computeChainDataLength());

  unique_ptr<IOBuf> prefix(queue.split(1));
  checkConsistency(queue);
  EXPECT_EQ(1, prefix->computeChainDataLength());
  EXPECT_EQ(11, queue.front()->computeChainDataLength());
  prefix = queue.split(2);
  checkConsistency(queue);
  EXPECT_EQ(2, prefix->computeChainDataLength());
  EXPECT_EQ(9, queue.front()->computeChainDataLength());
  prefix = queue.split(3);
  checkConsistency(queue);
  EXPECT_EQ(3, prefix->computeChainDataLength());
  EXPECT_EQ(6, queue.front()->computeChainDataLength());
  prefix = queue.split(1);
  checkConsistency(queue);
  EXPECT_EQ(1, prefix->computeChainDataLength());
  EXPECT_EQ(5, queue.front()->computeChainDataLength());
  prefix = queue.split(5);
  checkConsistency(queue);
  EXPECT_EQ(5, prefix->computeChainDataLength());
  EXPECT_EQ((IOBuf*)NULL, queue.front());

  queue.append(stringToIOBuf(SCL("Hello,")));
  queue.append(stringToIOBuf(SCL(" World")));
  checkConsistency(queue);
  bool exceptionFired = false;
  EXPECT_THROW({prefix = queue.split(13);}, std::underflow_error);
  checkConsistency(queue);
}

TEST(IOBufQueue, Preallocate) {
  IOBufQueue queue(clOptions);
  queue.append(string("Hello"));
  pair<void*,uint32_t> writable = queue.preallocate(2, 64);
  checkConsistency(queue);
  EXPECT_NE((void*)NULL, writable.first);
  EXPECT_LE(2, writable.second);
  EXPECT_GE(64, writable.second);
  memcpy(writable.first, SCL(", "));
  queue.postallocate(2);
  checkConsistency(queue);
  EXPECT_EQ(7, queue.front()->computeChainDataLength());
  queue.append(SCL("World"));
  checkConsistency(queue);
  EXPECT_EQ(12, queue.front()->computeChainDataLength());
  writable = queue.preallocate(1024, 4096);
  checkConsistency(queue);
  EXPECT_LE(1024, writable.second);
  EXPECT_GE(4096, writable.second);
}

TEST(IOBufQueue, trim) {
  IOBufQueue queue(clOptions);
  unique_ptr<IOBuf> a = IOBuf::create(4);
  a->append(4);
  queue.append(std::move(a));
  checkConsistency(queue);
  a = IOBuf::create(6);
  a->append(6);
  queue.append(std::move(a));
  checkConsistency(queue);
  a = IOBuf::create(8);
  a->append(8);
  queue.append(std::move(a));
  checkConsistency(queue);
  a = IOBuf::create(10);
  a->append(10);
  queue.append(std::move(a));
  checkConsistency(queue);

  EXPECT_EQ(4, queue.front()->countChainElements());
  EXPECT_EQ(28, queue.front()->computeChainDataLength());
  EXPECT_EQ(4, queue.front()->length());

  queue.trimStart(1);
  checkConsistency(queue);
  EXPECT_EQ(4, queue.front()->countChainElements());
  EXPECT_EQ(27, queue.front()->computeChainDataLength());
  EXPECT_EQ(3, queue.front()->length());

  queue.trimStart(5);
  checkConsistency(queue);
  EXPECT_EQ(3, queue.front()->countChainElements());
  EXPECT_EQ(22, queue.front()->computeChainDataLength());
  EXPECT_EQ(4, queue.front()->length());

  queue.trimEnd(1);
  checkConsistency(queue);
  EXPECT_EQ(3, queue.front()->countChainElements());
  EXPECT_EQ(21, queue.front()->computeChainDataLength());
  EXPECT_EQ(9, queue.front()->prev()->length());

  queue.trimEnd(20);
  checkConsistency(queue);
  EXPECT_EQ(1, queue.front()->countChainElements());
  EXPECT_EQ(1, queue.front()->computeChainDataLength());
  EXPECT_EQ(1, queue.front()->prev()->length());

  queue.trimEnd(1);
  checkConsistency(queue);
  EXPECT_EQ(NULL, queue.front());

  EXPECT_THROW(queue.trimStart(2), std::underflow_error);
  checkConsistency(queue);

  EXPECT_THROW(queue.trimEnd(30), std::underflow_error);
  checkConsistency(queue);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);

  return RUN_ALL_TESTS();
}
