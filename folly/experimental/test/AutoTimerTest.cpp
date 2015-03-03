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
#include <gtest/gtest.h>

#include <folly/experimental/AutoTimer.h>

using namespace folly;
using namespace std;

struct StubLogger {
  void operator()(StringPiece msg, double sec) {
    m = msg;
    t = sec;
  }
  static StringPiece m;
  static double t;
};

StringPiece StubLogger::m = "";
double StubLogger::t = 0;

struct StubClock {
  typedef std::chrono::seconds duration;

  static std::chrono::time_point<StubClock> now() {
    return std::chrono::time_point<StubClock>(std::chrono::duration<int>(t));
  }
  static int t;
};

int StubClock::t = 0;

TEST(TestAutoTimer, HandleBasic) {
  StubClock::t = 1;
  AutoTimer<StubLogger, StubClock> timer;
  StubClock::t = 3;
  timer.log("foo");
  ASSERT_EQ("foo", StubLogger::m);
  ASSERT_EQ(2, StubLogger::t);
}

TEST(TestAutoTimer, HandleLogOnDestruct) {
  {
    StubClock::t = 0;
    AutoTimer<StubLogger, StubClock> timer("message");
    StubClock::t = 3;
    timer.log("foo");
    EXPECT_EQ("foo", StubLogger::m);
    EXPECT_EQ(3, StubLogger::t);
    StubClock::t = 5;
  }
  ASSERT_EQ("message", StubLogger::m);
  ASSERT_EQ(2, StubLogger::t);
}

TEST(TestAutoTimer, HandleRealTimer) {
  AutoTimer<> t("Third message on destruction");
  t.log("First message");
  t.log("Second message");
}
