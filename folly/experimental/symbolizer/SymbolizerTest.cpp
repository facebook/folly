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



#include "folly/experimental/symbolizer/Symbolizer.h"

#include <glog/logging.h>

using namespace folly;
using namespace folly::symbolizer;

int main(int argc, char *argv[]) {
  google::InitGoogleLogging(argv[0]);
  Symbolizer s;
  StringPiece name;
  Dwarf::LocationInfo location;
  CHECK(s.symbolize(reinterpret_cast<uintptr_t>(main), name, location));
  LOG(INFO) << name << " " << location.file << " " << location.line << " ("
            << location.mainFile << ")";
  return 0;
}

