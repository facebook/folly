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

#include <folly/io/IOBuf.h>
#include <folly/portability/GTest.h>

using folly::IOBuf;

TEST(IOBuf, demo) {
  std::unique_ptr<IOBuf> buf(IOBuf::create(100));
  uint32_t cap = buf->capacity();
  EXPECT_LE(100, cap);
  EXPECT_EQ(0, buf->headroom());
  EXPECT_EQ(0, buf->length());
  EXPECT_EQ(cap, buf->tailroom());

  folly::StringPiece str{"world"};
  EXPECT_LE(str.size(), buf->tailroom());
  memcpy(buf->writableData(), str.data(), str.size());
  buf->append(str.size());

  buf->advance(10);
  EXPECT_EQ(10, buf->headroom());
  EXPECT_EQ(5, buf->length());
  EXPECT_EQ(cap - 15, buf->tailroom());
}
