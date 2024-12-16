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

#include <folly/ext/test_ext.h>

#include <fstream>
#include <sstream>

#include <boost/filesystem.hpp>

#include <folly/experimental/io/FsUtil.h>
#include <folly/json/dynamic.h>
#include <folly/json/json.h>

namespace folly {
namespace ext {

static std::string test_find_resource_buck2(std::string const& resource) {
  auto const exe = fs::executable_path();
  auto const bfn = exe.filename().string() + ".resources.json";
  auto const res = exe.parent_path() / bfn;
  std::stringstream buf;
  buf << std::ifstream(res.string()).rdbuf();
  auto const dyn = folly::parseJson(buf.str());
  auto const tgt = dyn[resource].asString();
  auto const ret = exe.parent_path() / tgt;
  return ret.string();
}

static const auto& test_find_resource_ctor_ = //
    test_find_resource = test_find_resource_buck2;

} // namespace ext
} // namespace folly
