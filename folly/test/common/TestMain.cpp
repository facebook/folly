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

#include <fstream>
#include <string>
#include <vector>

#include <glog/logging.h>

#include <folly/Portability.h>
#include <folly/init/Init.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/GTest.h>

/**
 * This is the recommended main function for all tests.
 * Before running all the tests, it initializes GoogleTest and folly.
 * By default, this configures Google Logging (glog) to output to stderr.
 *
 * The Makefile links it into all of the test programs so that tests do not need
 * to - and indeed should typically not - define their own main() functions
 * @file
 */
FOLLY_ATTR_WEAK int main(int argc, char** argv);

int main(int argc, char** argv) {
  // Enable glog logging to stderr by default.
  FLAGS_logtostderr = true;

  // Support tpx:supports_argsfiles https://fburl.com/workplace/tjehnnlm
  std::vector<std::string> args;
  std::vector<char*> new_argv;
  if (argc == 2 && *argv[1] == '@') {
    // Parse Tpx argsfile
    std::ifstream argsfile(argv[1] + 1);
    std::string line;
    while (std::getline(argsfile, line)) {
      args.push_back(line);
    }
    new_argv.push_back(argv[0]);
    for (const auto& arg : args) {
      new_argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argc = static_cast<int>(new_argv.size());
    argv = new_argv.data();
  }

  ::testing::InitGoogleTest(&argc, argv);
  folly::Init init(&argc, &argv);

  return RUN_ALL_TESTS();
}
