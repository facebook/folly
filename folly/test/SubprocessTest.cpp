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

#include "folly/Subprocess.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "folly/Format.h"
#include "folly/experimental/Gen.h"
#include "folly/experimental/FileGen.h"
#include "folly/experimental/StringGen.h"

using namespace folly;

TEST(SimpleSubprocessTest, ExitsSuccessfully) {
  Subprocess proc(std::vector<std::string>{ "/bin/true" });
  EXPECT_EQ(0, proc.wait().exitStatus());
}

TEST(SimpleSubprocessTest, ExitsSuccessfullyChecked) {
  Subprocess proc(std::vector<std::string>{ "/bin/true" });
  proc.waitChecked();
}

TEST(SimpleSubprocessTest, ExitsWithError) {
  Subprocess proc(std::vector<std::string>{ "/bin/false" });
  EXPECT_EQ(1, proc.wait().exitStatus());
}

TEST(SimpleSubprocessTest, ExitsWithErrorChecked) {
  Subprocess proc(std::vector<std::string>{ "/bin/false" });
  EXPECT_THROW(proc.waitChecked(), CalledProcessError);
}

TEST(SimpleSubprocessTest, ShellExitsSuccesssfully) {
  Subprocess proc("true");
  EXPECT_EQ(0, proc.wait().exitStatus());
}

TEST(SimpleSubprocessTest, ShellExitsWithError) {
  Subprocess proc("false");
  EXPECT_EQ(1, proc.wait().exitStatus());
}

TEST(PopenSubprocessTest, PopenRead) {
  Subprocess proc("ls /", Subprocess::pipeStdout());
  int found = 0;
  gen::byLine(proc.stdout()) |
    [&] (StringPiece line) {
      if (line == "etc" || line == "bin" || line == "usr") {
        ++found;
      }
    };
  EXPECT_EQ(3, found);
  proc.waitChecked();
}

TEST(CommunicateSubprocessTest, SimpleRead) {
  Subprocess proc(std::vector<std::string>{ "/bin/echo", "-n", "foo", "bar"},
                  Subprocess::pipeStdout());
  auto p = proc.communicate();
  EXPECT_EQ("foo bar", p.first);
  proc.waitChecked();
}

TEST(CommunicateSubprocessTest, BigWrite) {
  const int numLines = 1 << 20;
  std::string line("hello\n");
  std::string data;
  data.reserve(numLines * line.size());
  for (int i = 0; i < numLines; ++i) {
    data.append(line);
  }

  Subprocess proc("wc -l", Subprocess::pipeStdin() | Subprocess::pipeStdout());
  auto p = proc.communicate(Subprocess::writeStdin() | Subprocess::readStdout(),
                            data);
  EXPECT_EQ(folly::format("{}\n", numLines).str(), p.first);
  proc.waitChecked();
}

TEST(CommunicateSubprocessTest, Duplex) {
  // Take 10MB of data and pass them through a filter.
  // One line, as tr is line-buffered
  const int bytes = 10 << 20;
  std::string line(bytes, 'x');

  Subprocess proc("tr a-z A-Z",
                  Subprocess::pipeStdin() | Subprocess::pipeStdout());
  auto p = proc.communicate(Subprocess::writeStdin() | Subprocess::readStdout(),
                            line);
  EXPECT_EQ(bytes, p.first.size());
  EXPECT_EQ(std::string::npos, p.first.find_first_not_of('X'));
  proc.waitChecked();
}

