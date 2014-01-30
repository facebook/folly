/*
 * Copyright 2014 Facebook, Inc.
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

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <iostream>
#include <stdexcept>
#include <system_error>

#include <gflags/gflags.h>

#include "folly/Format.h"
#include "folly/Portability.h"
#include "folly/Range.h"
#include "folly/ScopeGuard.h"
#include "folly/experimental/io/HugePages.h"

DEFINE_bool(cp, false, "Copy file");

using namespace folly;

namespace {

void usage(const char* name) FOLLY_NORETURN;

void usage(const char* name) {
  std::cerr << folly::format(
      "Usage: {0}\n"
      "         list all huge page sizes and their mount points\n"
      "       {0} -cp <src_file> <dest_nameprefix>\n"
      "         copy src_file to a huge page file\n",
      name);
  exit(1);
}

void copy(const char* srcFile, const char* destPrefix) {
  int srcfd = open(srcFile, O_RDONLY);
  if (srcfd == -1) {
    throw std::system_error(errno, std::system_category(), "open failed");
  }
  SCOPE_EXIT {
    close(srcfd);
  };
  struct stat st;
  if (fstat(srcfd, &st) == -1) {
    throw std::system_error(errno, std::system_category(), "fstat failed");
  }

  void* start = mmap(nullptr, st.st_size, PROT_READ, MAP_SHARED, srcfd, 0);
  if (start == MAP_FAILED) {
    throw std::system_error(errno, std::system_category(), "mmap failed");
  }

  SCOPE_EXIT {
    munmap(start, st.st_size);
  };

  HugePages hp;
  auto f = hp.create(ByteRange(static_cast<const unsigned char*>(start),
                               st.st_size),
                     destPrefix);
  std::cout << f.path << "\n";
}

void list() {
  HugePages hp;
  for (auto& p : hp.sizes()) {
    std::cout << p.size << " " << p.mountPoint << "\n";
  }
}

}  // namespace


int main(int argc, char *argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_cp) {
    if (argc != 3) usage(argv[0]);
    copy(argv[1], argv[2]);
  } else {
    if (argc != 1) usage(argv[0]);
    list();
  }
  return 0;
}

