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

#include <folly/Exception.h>

#include <cstdio>
#include <memory>

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace folly { namespace test {

#define EXPECT_SYSTEM_ERROR(statement, err, msg) \
  try { \
    statement; \
    ADD_FAILURE() << "Didn't throw"; \
  } catch (const std::system_error& e) { \
    std::system_error expected(err, std::system_category(), msg); \
    EXPECT_STREQ(expected.what(), e.what()); \
  } catch (...) { \
    ADD_FAILURE() << "Threw a different type"; \
  }


TEST(ExceptionTest, Simple) {
  // Make sure errno isn't used when we don't want it to, set it to something
  // else than what we set when we call Explicit functions
  errno = ERANGE;
  EXPECT_SYSTEM_ERROR({throwSystemErrorExplicit(EIO, "hello");},
                      EIO, "hello");
  errno = ERANGE;
  EXPECT_SYSTEM_ERROR({throwSystemErrorExplicit(EIO, "hello", " world");},
                      EIO, "hello world");
  errno = ERANGE;
  EXPECT_SYSTEM_ERROR({throwSystemError("hello", " world");},
                      ERANGE, "hello world");

  EXPECT_NO_THROW({checkPosixError(0, "hello", " world");});
  errno = ERANGE;
  EXPECT_SYSTEM_ERROR({checkPosixError(EIO, "hello", " world");},
                      EIO, "hello world");

  EXPECT_NO_THROW({checkKernelError(0, "hello", " world");});
  EXPECT_NO_THROW({checkKernelError(EIO, "hello", " world");});
  errno = ERANGE;
  EXPECT_SYSTEM_ERROR({checkKernelError(-EIO, "hello", " world");},
                      EIO, "hello world");

  EXPECT_NO_THROW({checkUnixError(0, "hello", " world");});
  EXPECT_NO_THROW({checkUnixError(1, "hello", " world");});
  errno = ERANGE;
  EXPECT_SYSTEM_ERROR({checkUnixError(-1, "hello", " world");},
                      ERANGE, "hello world");

  EXPECT_NO_THROW({checkUnixErrorExplicit(0, EIO, "hello", " world");});
  EXPECT_NO_THROW({checkUnixErrorExplicit(1, EIO, "hello", " world");});
  errno = ERANGE;
  EXPECT_SYSTEM_ERROR({checkUnixErrorExplicit(-1, EIO, "hello", " world");},
                      EIO, "hello world");

  std::shared_ptr<FILE> fp(tmpfile(), fclose);
  ASSERT_TRUE(fp != nullptr);

  EXPECT_NO_THROW({checkFopenError(fp.get(), "hello", " world");});
  errno = ERANGE;
  EXPECT_SYSTEM_ERROR({checkFopenError(nullptr, "hello", " world");},
                      ERANGE, "hello world");

  EXPECT_NO_THROW({checkFopenErrorExplicit(fp.get(), EIO, "hello", " world");});
  errno = ERANGE;
  EXPECT_SYSTEM_ERROR({checkFopenErrorExplicit(nullptr, EIO,
                                               "hello", " world");},
                      EIO, "hello world");
}

}}  // namespaces

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
