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

#include <folly/experimental/NestedCommandLineApp.h>
#include <folly/portability/GFlags.h>

DEFINE_int32(global_foo, 42, "Global foo");
DEFINE_int32(conflict_global, 42, "Global conflict");

namespace po = ::boost::program_options;

namespace {

void init(
    const std::string& cmd,
    const po::variables_map& /* options */,
    const std::vector<std::string>& /* args */) {
  printf("running %s\n", cmd.c_str());
}

void foo(
    const po::variables_map& options, const std::vector<std::string>& args) {
  printf("foo global-foo %d\n", options["global-foo"].as<int32_t>());
  printf("foo local-foo %d\n", options["local-foo"].as<int32_t>());
  printf("foo conflict-global %d\n", options["conflict-global"].as<int32_t>());
  printf("foo conflict %d\n", options["conflict"].as<int32_t>());
  for (auto& arg : args) {
    printf("foo arg %s\n", arg.c_str());
  }
}

void qux(
    const po::variables_map& options, const std::vector<std::string>& args) {
  printf("qux global-foo %d\n", options["global-foo"].as<int32_t>());
  printf("qux conflict-global %d\n", options["conflict-global"].as<int32_t>());
  printf("qux optional-arg %d\n", options["optional-arg"].as<int32_t>());
  printf("qux fred %d\n", options["fred"].as<int32_t>());
  printf("qux thud %d\n", options["thud"].as<int32_t>());
  for (auto& arg : args) {
    printf("qux arg %s\n", arg.c_str());
  }
}
} // namespace

int main(int argc, char* argv[]) {
  folly::NestedCommandLineApp app("", "0.1", "", "", init);

  int style = po::command_line_style::default_style;
  style &= ~po::command_line_style::allow_guessing;
  app.setOptionStyle(static_cast<po::command_line_style::style_t>(style));

  app.addGFlags();
  // clang-format off
  app.addCommand("foo", "[args...]", "Do some foo", "Does foo", foo)
    .add_options()
      ("local-foo", po::value<int32_t>()->default_value(42), "Local foo")
      ("conflict", po::value<int32_t>()->default_value(42), "Local conflict");

  po::positional_options_description positionalArgs;
  positionalArgs.add("fred", /*max_count=*/1);
  positionalArgs.add("thud", /*max_count=*/1);
  app.addCommand("qux", "[args...]", "Do some qux", "Does qux", qux, positionalArgs)
    .add_options()
    ("optional-arg", po::value<int32_t>()->default_value(41), "optional argument")
    ("fred", boost::program_options::value<int32_t>()->required(), "fred positional arg")
    ("thud", boost::program_options::value<int32_t>()->required(), "thud positional arg");
  // clang-format on
  app.addAlias("bar", "foo");
  app.addAlias("baz", "bar");
  return app.run(argc, argv);
}
