/*
 * Copyright 2015 Facebook, Inc.
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

#include <cstdlib>
#include <gtest/gtest.h>
#include <folly/MemoryMapping.h>

namespace folly {

TEST(MemoryMapping, Basic) {
  File f = File::temporary();
  {
    MemoryMapping m(File(f.fd()), 0, sizeof(double), MemoryMapping::writable());
    double* d = m.asWritableRange<double>().data();
    *d = 37 * M_PI;
  }
  {
    MemoryMapping m(File(f.fd()), 0, 3);
    EXPECT_EQ(0, m.asRange<int>().size()); // not big enough
  }
  {
    MemoryMapping m(File(f.fd()), 0, sizeof(double));
    const double* d = m.asRange<double>().data();
    EXPECT_EQ(*d, 37 * M_PI);
  }
}

TEST(MemoryMapping, Move) {
  File f = File::temporary();
  {
    MemoryMapping m(
        File(f.fd()), 0, sizeof(double) * 2, MemoryMapping::writable());
    double* d = m.asWritableRange<double>().data();
    d[0] = 37 * M_PI;
    MemoryMapping m2(std::move(m));
    double* d2 = m2.asWritableRange<double>().data();
    d2[1] = 39 * M_PI;
  }
  {
    MemoryMapping m(File(f.fd()), 0, sizeof(double));
    const double* d = m.asRange<double>().data();
    EXPECT_EQ(d[0], 37 * M_PI);
    MemoryMapping m2(std::move(m));
    const double* d2 = m2.asRange<double>().data();
    EXPECT_EQ(d2[1], 39 * M_PI);
  }
}

TEST(MemoryMapping, DoublyMapped) {
  File f = File::temporary();
  // two mappings of the same memory, different addresses.
  MemoryMapping mw(File(f.fd()), 0, sizeof(double), MemoryMapping::writable());
  MemoryMapping mr(File(f.fd()), 0, sizeof(double));

  double* dw = mw.asWritableRange<double>().data();
  const double* dr = mr.asRange<double>().data();

  // Show that it's truly the same value, even though the pointers differ
  EXPECT_NE(dw, dr);
  *dw = 42 * M_PI;
  EXPECT_EQ(*dr, 42 * M_PI);
  *dw = 43 * M_PI;
  EXPECT_EQ(*dr, 43 * M_PI);
}

namespace {

void writeStringToFileOrDie(const std::string& str, int fd) {
  const char* b = str.c_str();
  size_t count = str.size();
  ssize_t total_bytes = 0;
  ssize_t r;
  do {
    r = write(fd, b, count);
    if (r == -1) {
      if (errno == EINTR) {
        continue;
      }
      PCHECK(r) << "write";
    }

    total_bytes += r;
    b += r;
    count -= r;
  } while (r != 0 && count);
}

}  // anonymous namespace

TEST(MemoryMapping, Simple) {
  File f = File::temporary();
  writeStringToFileOrDie("hello", f.fd());

  {
    MemoryMapping m(File(f.fd()));
    EXPECT_EQ("hello", m.data());
  }
  {
    MemoryMapping m(File(f.fd()), 1, 2);
    EXPECT_EQ("el", m.data());
  }
}

TEST(MemoryMapping, LargeFile) {
  std::string fileData;
  size_t fileSize = sysconf(_SC_PAGESIZE) * 3 + 10;
  fileData.reserve(fileSize);
  for (size_t i = 0; i < fileSize; i++) {
    fileData.push_back(0xff & random());
  }

  File f = File::temporary();
  writeStringToFileOrDie(fileData, f.fd());

  {
    MemoryMapping m(File(f.fd()));
    EXPECT_EQ(fileData, m.data());
  }
  {
    size_t size = sysconf(_SC_PAGESIZE) * 2;
    StringPiece s(fileData.data() + 9, size - 9);
    MemoryMapping m(File(f.fd()), 9, size - 9);
    EXPECT_EQ(s.toString(), m.data());
  }
}

TEST(MemoryMapping, ZeroLength) {
  File f = File::temporary();
  MemoryMapping m(File(f.fd()));
  EXPECT_TRUE(m.mlock(MemoryMapping::LockMode::MUST_LOCK));
  EXPECT_TRUE(m.mlocked());
  EXPECT_EQ(0, m.data().size());
}

} // namespace folly
