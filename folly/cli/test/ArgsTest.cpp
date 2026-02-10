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

#include <folly/cli/Args.h>

#include <fstream>

#include <folly/String.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <folly/testing/TestUtil.h>

using namespace folly;
using testing::Contains;
using testing::ElementsAreArray;

namespace {

// Test receiver that collects events for verification
} // namespace

// Test fixture class using folly::test::TemporaryDirectory
class ArgsTest : public ::testing::Test {
 protected:
  using location = cli_apply_args_files_receiver::location;
  using term_error = cli_apply_args_files_receiver::term_error;

  struct Entry {
    enum Type {
      TERM,
      TERM_ERROR,
      FILE_FOUND,
      FILE_ERROR,
      FILE_ENTER,
      FILE_LEAVE,
      FILE_CYCLE
    };
    Type type{};
    std::string value{};
    term_error error_code{}; // For TERM_ERROR: the error code
    location loc{};
    location error_loc{}; // For TERM_ERROR: where the error begins
    std::error_code err{};
    size_t depth{};
    std::filesystem::path canonical_path{}; // For FILE_CYCLE

    static char const* type_to_s(Type t) noexcept {
      constexpr char const* strings[] = {
          "term",
          "term-error",
          "file-found",
          "file-error",
          "file-enter",
          "file-leave",
          "file-cycle",
      };
      return strings[t];
    }

    static char const* term_error_to_s(term_error e) noexcept {
      switch (e) {
        case term_error::success:
          return "success";
        case term_error::unclosed_single_quote:
          return "unclosed_single_quote";
        case term_error::unclosed_double_quote:
          return "unclosed_double_quote";
        case term_error::unrecognized_escape_sequence:
          return "unrecognized_escape_sequence";
        default:
          return "unknown";
      }
    }

    friend bool operator==(const Entry& lhs, const Entry& rhs) = default;

    template <typename Char, typename Traits>
    friend std::basic_ostream<Char, Traits>& operator<<(
        std::basic_ostream<Char, Traits>& o, Entry const& e) {
      o << "entry{" << "type=" << type_to_s(e.type) << ", " << "value=\""
        << cEscape<std::string>(e.value) << "\", " << "term_loc{"
        << "idx=" << e.loc.idx << ", " << "off=" << e.loc.off << ", "
        << "len=" << e.loc.len << ", " << "b={line=" << e.loc.b.line
        << ", col=" << e.loc.b.col << "}, " << "e={line=" << e.loc.e.line
        << ", col=" << e.loc.e.col << "}" << "}";
      if (e.type == TERM_ERROR) {
        o << ", error_code=" << term_error_to_s(e.error_code);
        o << ", error_loc{" << "idx=" << e.error_loc.idx << ", "
          << "off=" << e.error_loc.off << ", " << "len=" << e.error_loc.len
          << ", " << "b={line=" << e.error_loc.b.line
          << ", col=" << e.error_loc.b.col << "}, "
          << "e={line=" << e.error_loc.e.line << ", col=" << e.error_loc.e.col
          << "}" << "}";
      }
      o << ", depth=" << e.depth << "}";
      return o;
    }
  };

  class TestReceiver : public cli_apply_args_files_receiver {
   public:
    std::vector<Entry> entries;
    std::vector<std::string> strings;
    size_t current_depth = 0;
    std::string stop_string;

    control on_term(std::string arg, location loc) override {
      strings.push_back(arg);
      entries.push_back({
          .type = Entry::TERM,
          .value = std::string(arg),
          .loc = loc,
          .depth = current_depth,
      });

      if (!stop_string.empty() && arg == stop_string) {
        return control::stop;
      }
      return control::pass;
    }

    void on_term_error(
        term_error error, location term_loc, location error_loc) override {
      entries.push_back({
          .type = Entry::TERM_ERROR,
          .error_code = error,
          .loc = term_loc,
          .error_loc = error_loc,
          .depth = current_depth,
      });
    }

    found on_file_found(cstring_view file, location loc) override {
      entries.push_back({
          .type = Entry::FILE_FOUND,
          .value = std::string(file),
          .loc = loc,
          .depth = current_depth,
      });

      return found::dive;
    }

    control on_file_error(
        std::string file, location loc, std::error_code err) override {
      entries.push_back({
          .type = Entry::FILE_ERROR,
          .value = std::move(file),
          .loc = loc,
          .err = err,
          .depth = current_depth,
      });
      return control::pass;
    }

    void on_file_enter(std::string file, location loc) override {
      entries.push_back({
          .type = Entry::FILE_ENTER,
          .value = std::string(file),
          .loc = loc,
          .depth = current_depth,
      });
      current_depth++;
    }

    void on_file_leave() override {
      current_depth--;
      entries.push_back({
          .type = Entry::FILE_LEAVE,
          .depth = current_depth,
      });
    }

    cycle on_file_cycle(
        std::string file,
        location loc,
        std::filesystem::path canonical_path) override {
      entries.push_back({
          .type = Entry::FILE_CYCLE,
          .value = std::move(file),
          .loc = loc,
          .depth = current_depth,
          .canonical_path = std::move(canonical_path),
      });
      return cycle::skip; // Default: skip cycles
    }

    void set_stop_string(std::string const& str) { stop_string = str; }
  };

  // Helper function to write a file relative to the temporary directory
  void write_file(const std::string& filename, const std::string& content) {
    auto path = tempDir_.path() / filename;

    std::ofstream file(path.string());
    file << folly::stripLeftMargin(content);
    file.close();
  }

  // Helper function to get the temporary directory path
  std::filesystem::path temp_dir() const {
    return std::filesystem::path{tempDir_.path().string()};
  }

  // Helper function to create a location for top-level entries
  static location idx(size_t idx_val) {
    return {.idx = idx_val, .off = 0, .len = 0, .b = {}, .e = {}};
  }

  // Helper functions for creating test entries
  Entry entry_term(size_t depth, const std::string& value, location loc = {}) {
    auto type = Entry::TERM;
    return {
        .type = type, .value = value, .loc = loc, .err = {}, .depth = depth};
  }

  Entry entry_file_found(
      size_t depth, const std::string& value, location loc = {}) {
    auto type = Entry::FILE_FOUND;
    return {
        .type = type, .value = value, .loc = loc, .err = {}, .depth = depth};
  }

  Entry entry_file_enter(
      size_t depth, const std::string& value, location loc = {}) {
    auto type = Entry::FILE_ENTER;
    return {
        .type = type, .value = value, .loc = loc, .err = {}, .depth = depth};
  }

  Entry entry_file_leave(size_t depth) {
    auto type = Entry::FILE_LEAVE;
    return {.type = type, .value = "", .loc = {}, .err = {}, .depth = depth};
  }

  Entry entry_file_error(
      size_t depth,
      const std::string& value,
      location loc,
      std::error_code err) {
    auto type = Entry::FILE_ERROR;
    return {
        .type = type, .value = value, .loc = loc, .err = err, .depth = depth};
  }

  Entry entry_file_error_no_such_file(
      size_t depth, const std::string& value, location loc) {
    auto error = std::make_error_code(std::errc::no_such_file_or_directory);
    return entry_file_error(depth, value, loc, error);
  }

  Entry entry_file_error_is_a_directory(
      size_t depth, const std::string& value, location loc) {
    auto error = std::make_error_code(std::errc::is_a_directory);
    return entry_file_error(depth, value, loc, error);
  }

  Entry entry_file_error_max_depth(
      size_t depth, const std::string& value, location loc) {
    return entry_file_error(
        depth,
        value,
        loc,
        make_error_code(cli_apply_args_files_errc::max_depth_exceeded));
  }

  Entry entry_term_error(
      size_t depth, term_error code, location term_loc, location error_loc) {
    return {
        .type = Entry::TERM_ERROR,
        .error_code = code,
        .loc = term_loc,
        .error_loc = error_loc,
        .depth = depth};
  }

  Entry entry_file_cycle(
      size_t depth,
      const std::string& value,
      location loc,
      std::filesystem::path canonical_path) {
    return {
        .type = Entry::FILE_CYCLE,
        .value = value,
        .loc = loc,
        .depth = depth,
        .canonical_path = std::move(canonical_path)};
  }

  folly::test::TemporaryDirectory tempDir_;
};

TEST_F(ArgsTest, BasicArguments) {
  auto const args = std::vector<std::string>{
      "prog",
      "--flag",
      "value",
      "arg",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--flag",
          "value",
          "arg",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_term(0, "--flag", idx(1)),
          entry_term(0, "value", idx(2)),
          entry_term(0, "arg", idx(3)),
      }));
}

TEST_F(ArgsTest, SimpleArgsFile) {
  write_file("simple.args", R"(
    --option1 value1 --option2
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@simple.args",
      "trailing",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--option1",
          "value1",
          "--option2",
          "trailing",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "simple.args", idx(1)),
          entry_file_enter(0, "simple.args", idx(1)),
          entry_term(
              1,
              "--option1",
              {.idx = 0, .off = 0, .len = 9, .b = {1, 1}, .e = {1, 9}}),
          entry_term(
              1,
              "value1",
              {.idx = 1, .off = 10, .len = 6, .b = {1, 11}, .e = {1, 16}}),
          entry_term(
              1,
              "--option2",
              {.idx = 2, .off = 17, .len = 9, .b = {1, 18}, .e = {1, 26}}),
          entry_file_leave(0),
          entry_term(0, "trailing", idx(2)),
      }));
}

TEST_F(ArgsTest, QuotedArguments) {
  write_file("quoted.args", R"(
    --message "hello world" --path '/some/path with spaces'
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@quoted.args",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--message",
          "hello world",
          "--path",
          "/some/path with spaces",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "quoted.args", idx(1)),
          entry_file_enter(0, "quoted.args", idx(1)),
          entry_term(
              1,
              "--message",
              {.idx = 0, .off = 0, .len = 9, .b = {1, 1}, .e = {1, 9}}),
          entry_term(
              1,
              "hello world",
              {.idx = 1, .off = 10, .len = 13, .b = {1, 11}, .e = {1, 23}}),
          entry_term(
              1,
              "--path",
              {.idx = 2, .off = 24, .len = 6, .b = {1, 25}, .e = {1, 30}}),
          entry_term(
              1,
              "/some/path with spaces",
              {.idx = 3, .off = 31, .len = 24, .b = {1, 32}, .e = {1, 55}}),
          entry_file_leave(0),
      }));
}

TEST_F(ArgsTest, EscapeSequences) {
  write_file("escaped.args", R"(
    --text "Line 1\nLine 2" --quote "She said \"hello\""
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@escaped.args",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--text",
          "Line 1\nLine 2",
          "--quote",
          "She said \"hello\"",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "escaped.args", idx(1)),
          entry_file_enter(0, "escaped.args", idx(1)),
          entry_term(
              1,
              "--text",
              {.idx = 0, .off = 0, .len = 6, .b = {1, 1}, .e = {1, 6}}),
          entry_term(
              1,
              "Line 1\nLine 2",
              {.idx = 1, .off = 7, .len = 16, .b = {1, 8}, .e = {1, 23}}),
          entry_term(
              1,
              "--quote",
              {.idx = 2, .off = 24, .len = 7, .b = {1, 25}, .e = {1, 31}}),
          entry_term(
              1,
              "She said \"hello\"",
              {.idx = 3, .off = 32, .len = 20, .b = {1, 33}, .e = {1, 52}}),
          entry_file_leave(0),
      }));
}

TEST_F(ArgsTest, CommentLines) {
  write_file("commented.args", R"(
    # This is a comment
    --verbose
    # Another comment
    --output result.txt
    # Final comment
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@commented.args",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--verbose",
          "--output",
          "result.txt",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "commented.args", idx(1)),
          entry_file_enter(0, "commented.args", idx(1)),
          entry_term(
              1,
              "--verbose",
              {.idx = 0, .off = 20, .len = 9, .b = {2, 1}, .e = {2, 9}}),
          entry_term(
              1,
              "--output",
              {.idx = 1, .off = 48, .len = 8, .b = {4, 1}, .e = {4, 8}}),
          entry_term(
              1,
              "result.txt",
              {.idx = 2, .off = 57, .len = 10, .b = {4, 10}, .e = {4, 19}}),
          entry_file_leave(0),
      }));
}

TEST_F(ArgsTest, EmptyFile) {
  write_file("empty.args", R"(
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@empty.args",
      "--flag",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--flag",
      }));

  // Should have file enter/leave entries even for empty file
  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "empty.args", idx(1)),
          entry_file_enter(0, "empty.args", idx(1)),
          entry_file_leave(0),
          entry_term(0, "--flag", idx(2)),
      }));
}

TEST_F(ArgsTest, NestedArgsFiles) {
  write_file("inner.args", R"(
    --inner-flag inner_value
  )");

  write_file("outer.args", R"(
    --outer-flag @inner.args outer_value
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@outer.args",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--outer-flag",
          "--inner-flag",
          "inner_value",
          "outer_value",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "outer.args", idx(1)),
          entry_file_enter(0, "outer.args", idx(1)),
          entry_term(
              1,
              "--outer-flag",
              {.idx = 0, .off = 0, .len = 12, .b = {1, 1}, .e = {1, 12}}),
          entry_file_found(
              1,
              "inner.args",
              {.idx = 1, .off = 13, .len = 11, .b = {1, 14}, .e = {1, 24}}),
          entry_file_enter(
              1,
              "inner.args",
              {.idx = 1, .off = 13, .len = 11, .b = {1, 14}, .e = {1, 24}}),
          entry_term(
              2,
              "--inner-flag",
              {.idx = 0, .off = 0, .len = 12, .b = {1, 1}, .e = {1, 12}}),
          entry_term(
              2,
              "inner_value",
              {.idx = 1, .off = 13, .len = 11, .b = {1, 14}, .e = {1, 24}}),
          entry_file_leave(1),
          entry_term(
              1,
              "outer_value",
              {.idx = 2, .off = 25, .len = 11, .b = {1, 26}, .e = {1, 36}}),
          entry_file_leave(0),
      }));
}

TEST_F(ArgsTest, MultipleArgsFiles) {
  write_file("file1.args", R"(
    --flag1 value1
  )");

  write_file("file2.args", R"(
    --flag2 value2
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@file1.args",
      "middle",
      "@file2.args",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--flag1",
          "value1",
          "middle",
          "--flag2",
          "value2",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "file1.args", idx(1)),
          entry_file_enter(0, "file1.args", idx(1)),
          entry_term(
              1,
              "--flag1",
              {.idx = 0, .off = 0, .len = 7, .b = {1, 1}, .e = {1, 7}}),
          entry_term(
              1,
              "value1",
              {.idx = 1, .off = 8, .len = 6, .b = {1, 9}, .e = {1, 14}}),
          entry_file_leave(0),
          entry_term(0, "middle", idx(2)),
          entry_file_found(0, "file2.args", idx(3)),
          entry_file_enter(0, "file2.args", idx(3)),
          entry_term(
              1,
              "--flag2",
              {.idx = 0, .off = 0, .len = 7, .b = {1, 1}, .e = {1, 7}}),
          entry_term(
              1,
              "value2",
              {.idx = 1, .off = 8, .len = 6, .b = {1, 9}, .e = {1, 14}}),
          entry_file_leave(0),
      }));
}

TEST_F(ArgsTest, ArgumentStartingWithAt) {
  // An argument that is exactly @ should remain as-is
  auto const args = std::vector<std::string>{
      "prog",
      "@",
      "normal_arg",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "@",
          "normal_arg",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_term(0, "@", idx(1)),
          entry_term(0, "normal_arg", idx(2)),
      }));
}

TEST_F(ArgsTest, NonExistentFile) {
  auto const args = std::vector<std::string>{
      "prog",
      "@/nonexistent/file.txt",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "/nonexistent/file.txt", idx(1)),
          entry_file_error_no_such_file(0, "/nonexistent/file.txt", idx(1)),
      }));
}

TEST_F(ArgsTest, ComplexRealWorldExample) {
  write_file("config.args", R"(
    # Configuration for build system
    --build-type Release
    --target "MyApplication"
    --define "VERSION=\"1.0.0\""
    --include-dir "/usr/local/include"
    --include-dir "/opt/custom/include"
    # Output settings
    --output-dir "build/release"
    --verbose
  )");

  auto const args = std::vector<std::string>{
      "build-tool",
      "--config",
      "debug.cfg",
      "@config.args",
      "--extra-flag",
      "source.cpp",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "build-tool",
          "--config",
          "debug.cfg",
          "--build-type",
          "Release",
          "--target",
          "MyApplication",
          "--define",
          "VERSION=\"1.0.0\"",
          "--include-dir",
          "/usr/local/include",
          "--include-dir",
          "/opt/custom/include",
          "--output-dir",
          "build/release",
          "--verbose",
          "--extra-flag",
          "source.cpp",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "build-tool", idx(0)),
          entry_term(0, "--config", idx(1)),
          entry_term(0, "debug.cfg", idx(2)),
          entry_file_found(0, "config.args", idx(3)),
          entry_file_enter(0, "config.args", idx(3)),
          entry_term(
              1,
              "--build-type",
              {.idx = 0, .off = 33, .len = 12, .b = {2, 1}, .e = {2, 12}}),
          entry_term(
              1,
              "Release",
              {.idx = 1, .off = 46, .len = 7, .b = {2, 14}, .e = {2, 20}}),
          entry_term(
              1,
              "--target",
              {.idx = 2, .off = 54, .len = 8, .b = {3, 1}, .e = {3, 8}}),
          entry_term(
              1,
              "MyApplication",
              {.idx = 3, .off = 63, .len = 15, .b = {3, 10}, .e = {3, 24}}),
          entry_term(
              1,
              "--define",
              {.idx = 4, .off = 79, .len = 8, .b = {4, 1}, .e = {4, 8}}),
          entry_term(
              1,
              "VERSION=\"1.0.0\"",
              {.idx = 5, .off = 88, .len = 19, .b = {4, 10}, .e = {4, 28}}),
          entry_term(
              1,
              "--include-dir",
              {.idx = 6, .off = 108, .len = 13, .b = {5, 1}, .e = {5, 13}}),
          entry_term(
              1,
              "/usr/local/include",
              {.idx = 7, .off = 122, .len = 20, .b = {5, 15}, .e = {5, 34}}),
          entry_term(
              1,
              "--include-dir",
              {.idx = 8, .off = 143, .len = 13, .b = {6, 1}, .e = {6, 13}}),
          entry_term(
              1,
              "/opt/custom/include",
              {.idx = 9, .off = 157, .len = 21, .b = {6, 15}, .e = {6, 35}}),
          entry_term(
              1,
              "--output-dir",
              {.idx = 10, .off = 197, .len = 12, .b = {8, 1}, .e = {8, 12}}),
          entry_term(
              1,
              "build/release",
              {.idx = 11, .off = 210, .len = 15, .b = {8, 14}, .e = {8, 28}}),
          entry_term(
              1,
              "--verbose",
              {.idx = 12, .off = 226, .len = 9, .b = {9, 1}, .e = {9, 9}}),
          entry_file_leave(0),
          entry_term(0, "--extra-flag", idx(4)),
          entry_term(0, "source.cpp", idx(5)),
      }));
}

TEST_F(ArgsTest, TabsAndSpaces) {
  write_file("tabs_spaces.args", R"(
    --flag1		value1    --flag2	   value2
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@tabs_spaces.args",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--flag1",
          "value1",
          "--flag2",
          "value2",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "tabs_spaces.args", idx(1)),
          entry_file_enter(0, "tabs_spaces.args", idx(1)),
          entry_term(
              1,
              "--flag1",
              {.idx = 0, .off = 0, .len = 7, .b = {1, 1}, .e = {1, 7}}),
          entry_term(
              1,
              "value1",
              {.idx = 1, .off = 9, .len = 6, .b = {1, 10}, .e = {1, 15}}),
          entry_term(
              1,
              "--flag2",
              {.idx = 2, .off = 19, .len = 7, .b = {1, 20}, .e = {1, 26}}),
          entry_term(
              1,
              "value2",
              {.idx = 3, .off = 30, .len = 6, .b = {1, 31}, .e = {1, 36}}),
          entry_file_leave(0),
      }));
}

TEST_F(ArgsTest, EscapesOutsideQuotes) {
  write_file("escapes.args", R"(
    --text Line\nWithBackslash --other normal
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@escapes.args",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--text",
          "Line\nWithBackslash",
          "--other",
          "normal",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "escapes.args", idx(1)),
          entry_file_enter(0, "escapes.args", idx(1)),
          entry_term(
              1,
              "--text",
              {.idx = 0, .off = 0, .len = 6, .b = {1, 1}, .e = {1, 6}}),
          entry_term(
              1,
              "Line\nWithBackslash",
              {.idx = 1, .off = 7, .len = 19, .b = {1, 8}, .e = {1, 26}}),
          entry_term(
              1,
              "--other",
              {.idx = 2, .off = 27, .len = 7, .b = {1, 28}, .e = {1, 34}}),
          entry_term(
              1,
              "normal",
              {.idx = 3, .off = 35, .len = 6, .b = {1, 36}, .e = {1, 41}}),
          entry_file_leave(0),
      }));
}

TEST_F(ArgsTest, EmptyInputVector) {
  auto const args = std::vector<std::string>{};

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_TRUE(receiver.strings.empty());
  EXPECT_TRUE(receiver.entries.empty());
}

TEST_F(ArgsTest, PositionTracking) {
  auto const args = std::vector<std::string>{
      "prog",
      "--flag",
      "value",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--flag",
          "value",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_term(0, "--flag", idx(1)),
          entry_term(0, "value", idx(2)),
      }));
}

TEST_F(ArgsTest, FileErrorPosition) {
  auto const args = std::vector<std::string>{
      "prog",
      "--flag",
      "@nonexistent",
      "more",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--flag",
          "more",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_term(0, "--flag", idx(1)),
          entry_file_found(0, "nonexistent", idx(2)),
          entry_file_error_no_such_file(0, "nonexistent", idx(2)),
          entry_term(0, "more", idx(3)),
      }));
}

TEST_F(ArgsTest, MissingFileAtTopLevel) {
  auto const args = std::vector<std::string>{
      "prog",
      "@missing.args",
      "after",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "after",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "missing.args", idx(1)),
          entry_file_error_no_such_file(0, "missing.args", idx(1)),
          entry_term(0, "after", idx(2)),
      }));
}

TEST_F(ArgsTest, MissingFileInNestedArgsFile) {
  write_file("outer.args", R"(
    --start @missing-inner.args --end
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@outer.args",
      "final",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--start",
          "--end",
          "final",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "outer.args", idx(1)),
          entry_file_enter(0, "outer.args", idx(1)),
          entry_term(
              1,
              "--start",
              {.idx = 0, .off = 0, .len = 7, .b = {1, 1}, .e = {1, 7}}),
          entry_file_found(
              1,
              "missing-inner.args",
              {.idx = 1, .off = 8, .len = 19, .b = {1, 9}, .e = {1, 27}}),
          entry_file_error_no_such_file(
              1,
              "missing-inner.args",
              {.idx = 1, .off = 8, .len = 19, .b = {1, 9}, .e = {1, 27}}),
          entry_term(
              1,
              "--end",
              {.idx = 2, .off = 28, .len = 5, .b = {1, 29}, .e = {1, 33}}),
          entry_file_leave(0),
          entry_term(0, "final", idx(2)),
      }));
}

TEST_F(ArgsTest, MultipleMissingFilesAtSameLevel) {
  auto const args = std::vector<std::string>{
      "prog",
      "@missing1.args",
      "middle",
      "@missing2.args",
      "end",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "middle",
          "end",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "missing1.args", idx(1)),
          entry_file_error_no_such_file(0, "missing1.args", idx(1)),
          entry_term(0, "middle", idx(2)),
          entry_file_found(0, "missing2.args", idx(3)),
          entry_file_error_no_such_file(0, "missing2.args", idx(3)),
          entry_term(0, "end", idx(4)),
      }));
}

TEST_F(ArgsTest, MissingFileInDeeplyNestedStructure) {
  write_file("level1.args", R"(
    --l1-start @level2.args --l1-end
  )");
  write_file("level2.args", R"(
    --l2-start @missing-level3.args --l2-end
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@level1.args",
      "final",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--l1-start",
          "--l2-start",
          "--l2-end",
          "--l1-end",
          "final",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "level1.args", idx(1)),
          entry_file_enter(0, "level1.args", idx(1)),
          entry_term(
              1,
              "--l1-start",
              {.idx = 0, .off = 0, .len = 10, .b = {1, 1}, .e = {1, 10}}),
          entry_file_found(
              1,
              "level2.args",
              {.idx = 1, .off = 11, .len = 12, .b = {1, 12}, .e = {1, 23}}),
          entry_file_enter(
              1,
              "level2.args",
              {.idx = 1, .off = 11, .len = 12, .b = {1, 12}, .e = {1, 23}}),
          entry_term(
              2,
              "--l2-start",
              {.idx = 0, .off = 0, .len = 10, .b = {1, 1}, .e = {1, 10}}),
          entry_file_found(
              2,
              "missing-level3.args",
              {.idx = 1, .off = 11, .len = 20, .b = {1, 12}, .e = {1, 31}}),
          entry_file_error_no_such_file(
              2,
              "missing-level3.args",
              {.idx = 1, .off = 11, .len = 20, .b = {1, 12}, .e = {1, 31}}),
          entry_term(
              2,
              "--l2-end",
              {.idx = 2, .off = 32, .len = 8, .b = {1, 33}, .e = {1, 40}}),
          entry_file_leave(1),
          entry_term(
              1,
              "--l1-end",
              {.idx = 2, .off = 24, .len = 8, .b = {1, 25}, .e = {1, 32}}),
          entry_file_leave(0),
          entry_term(0, "final", idx(2)),
      }));
}

TEST_F(ArgsTest, MixOfValidAndMissingFiles) {
  write_file("valid1.args", R"(
    --valid1-flag valid1-value
  )");
  write_file("valid2.args", R"(
    --valid2-flag valid2-value
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@valid1.args",
      "@missing.args",
      "@valid2.args",
      "end",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--valid1-flag",
          "valid1-value",
          "--valid2-flag",
          "valid2-value",
          "end",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "valid1.args", idx(1)),
          entry_file_enter(0, "valid1.args", idx(1)),
          entry_term(
              1,
              "--valid1-flag",
              {.idx = 0, .off = 0, .len = 13, .b = {1, 1}, .e = {1, 13}}),
          entry_term(
              1,
              "valid1-value",
              {.idx = 1, .off = 14, .len = 12, .b = {1, 15}, .e = {1, 26}}),
          entry_file_leave(0),
          entry_file_found(0, "missing.args", idx(2)),
          entry_file_error_no_such_file(0, "missing.args", idx(2)),
          entry_file_found(0, "valid2.args", idx(3)),
          entry_file_enter(0, "valid2.args", idx(3)),
          entry_term(
              1,
              "--valid2-flag",
              {.idx = 0, .off = 0, .len = 13, .b = {1, 1}, .e = {1, 13}}),
          entry_term(
              1,
              "valid2-value",
              {.idx = 1, .off = 14, .len = 12, .b = {1, 15}, .e = {1, 26}}),
          entry_file_leave(0),
          entry_term(0, "end", idx(4)),
      }));
}

TEST_F(ArgsTest, MissingFileWithSpecialCharactersInName) {
  auto const args = std::vector<std::string>{
      "prog",
      "@missing-file_with.special-chars.args",
      "end",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "end",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "missing-file_with.special-chars.args", idx(1)),
          entry_file_error_no_such_file(
              0, "missing-file_with.special-chars.args", idx(1)),
          entry_term(0, "end", idx(2)),
      }));
}

TEST_F(ArgsTest, TopLevelArgumentsNotProcessedForQuotes) {
  // Top-level arguments should be passed through as-is, not unquoted/unescaped
  auto const args = std::vector<std::string>{
      "prog",
      "\"quoted string\"",
      "'single quoted'",
      "escaped\\nstring",
      "arg with spaces",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "\"quoted string\"", // quotes preserved
          "'single quoted'", // quotes preserved
          "escaped\\nstring", // escape preserved
          "arg with spaces", // spaces preserved (already handled by shell)
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_term(0, "\"quoted string\"", idx(1)),
          entry_term(0, "'single quoted'", idx(2)),
          entry_term(0, "escaped\\nstring", idx(3)),
          entry_term(0, "arg with spaces", idx(4)),
      }));
}

TEST_F(ArgsTest, ArgsFileArgumentsProcessedForQuotes) {
  // Args-file arguments should be unquoted/unescaped
  write_file("processing.args", R"(
    "quoted string"
    'single quoted'
    escaped\nstring
    "arg with spaces"
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@processing.args",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "quoted string", // quotes removed
          "single quoted", // quotes removed
          "escaped\nstring", // escape processed
          "arg with spaces", // quotes removed, spaces preserved
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "processing.args", idx(1)),
          entry_file_enter(0, "processing.args", idx(1)),
          entry_term(
              1,
              "quoted string",
              {.idx = 0, .off = 0, .len = 15, .b = {1, 1}, .e = {1, 15}}),
          entry_term(
              1,
              "single quoted",
              {.idx = 1, .off = 16, .len = 15, .b = {2, 1}, .e = {2, 15}}),
          entry_term(
              1,
              "escaped\nstring",
              {.idx = 2, .off = 32, .len = 15, .b = {3, 1}, .e = {3, 15}}),
          entry_term(
              1,
              "arg with spaces",
              {.idx = 3, .off = 48, .len = 17, .b = {4, 1}, .e = {4, 17}}),
          entry_file_leave(0),
      }));
}

// Test cases for stop strings
TEST_F(ArgsTest, StopStringInTopLevelArgs) {
  write_file("after_stop.args", R"(
    --should-not-appear
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "--start",
      "STOP",
      "@after_stop.args",
      "should-not-appear",
  };

  TestReceiver receiver;
  receiver.set_stop_string("STOP");

  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--start",
          "STOP", // Processing stops after this
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_term(0, "--start", idx(1)),
          // Processing stops here
          entry_term(0, "STOP", idx(2)),
      }));
}

TEST_F(ArgsTest, StopStringInArgsFile) {
  write_file("with_stop.args", R"(
    --before-stop STOP --after-stop
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@with_stop.args",
      "final",
  };

  TestReceiver receiver;
  receiver.set_stop_string("STOP");

  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--before-stop",
          "STOP", // processing stops after this within the args file
          "final", // continues at parent level
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "with_stop.args", idx(1)),
          entry_file_enter(0, "with_stop.args", idx(1)),
          entry_term(
              1,
              "--before-stop",
              {.idx = 0, .off = 0, .len = 13, .b = {1, 1}, .e = {1, 13}}),
          // processing stops here in this file
          entry_term(
              1,
              "STOP",
              {.idx = 1, .off = 14, .len = 4, .b = {1, 15}, .e = {1, 18}}),
          entry_file_leave(0),
          entry_term(0, "final", idx(2)),
      }));
}

TEST_F(ArgsTest, StopStringInNestedArgsFile) {
  write_file("outer_with_nested.args", R"(
    --outer-start @inner_with_stop.args --outer-end
  )");
  write_file("inner_with_stop.args", R"(
    --inner-start STOP --inner-end
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@outer_with_nested.args",
      "final",
  };

  TestReceiver receiver;
  receiver.set_stop_string("STOP");

  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--outer-start",
          "--inner-start",
          "STOP", // processing stops here in inner file
          "--outer-end", // continues at outer level
          "final",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "outer_with_nested.args", idx(1)),
          entry_file_enter(0, "outer_with_nested.args", idx(1)),
          entry_term(
              1,
              "--outer-start",
              {.idx = 0, .off = 0, .len = 13, .b = {1, 1}, .e = {1, 13}}),
          entry_file_found(
              1,
              "inner_with_stop.args",
              {.idx = 1, .off = 14, .len = 21, .b = {1, 15}, .e = {1, 35}}),
          entry_file_enter(
              1,
              "inner_with_stop.args",
              {.idx = 1, .off = 14, .len = 21, .b = {1, 15}, .e = {1, 35}}),
          entry_term(
              2,
              "--inner-start",
              {.idx = 0, .off = 0, .len = 13, .b = {1, 1}, .e = {1, 13}}),
          // stops processing in inner file
          entry_term(
              2,
              "STOP",
              {.idx = 1, .off = 14, .len = 4, .b = {1, 15}, .e = {1, 18}}),
          entry_file_leave(1),
          // continues at parent level
          entry_term(
              1,
              "--outer-end",
              {.idx = 2, .off = 36, .len = 11, .b = {1, 37}, .e = {1, 47}}),
          entry_file_leave(0),
          entry_term(0, "final", idx(2)),
      }));
}

TEST_F(ArgsTest, MixedQuotesInSingleToken) {
  // Test shell-style quote parsing where quotes can appear in the middle of
  // tokens and multiple quoted sections can be concatenated
  write_file("mixed_quotes.args", R"(
      foo'b'a"r"''''baz
      pre'fix_'mid"dle_"suffix
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@mixed_quotes.args",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "foobarbaz",
          "prefix_middle_suffix",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "mixed_quotes.args", idx(1)),
          entry_file_enter(0, "mixed_quotes.args", idx(1)),
          entry_term(
              1,
              "foobarbaz",
              {.idx = 0, .off = 0, .len = 17, .b = {1, 1}, .e = {1, 17}}),
          entry_term(
              1,
              "prefix_middle_suffix",
              {.idx = 1, .off = 18, .len = 24, .b = {2, 1}, .e = {2, 24}}),
          entry_file_leave(0),
      }));
}

TEST_F(ArgsTest, UnclosedSingleQuoteCallsOnTermError) {
  // Unclosed single quotes should call on_term_error and continue with parent
  write_file("unclosed_single.args", R"(
      --flag 'unclosed
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@unclosed_single.args",
      "after",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--flag",
          "after", // continues after error
      }));

  // File content after stripLeftMargin: "--flag 'unclosed\n"
  // --flag is at offset 0, len 6
  // 'unclosed starts at offset 7 (the quote)
  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "unclosed_single.args", idx(1)),
          entry_file_enter(0, "unclosed_single.args", idx(1)),
          entry_term(
              1,
              "--flag",
              {.idx = 0, .off = 0, .len = 6, .b = {1, 1}, .e = {1, 6}}),
          entry_term_error(
              1,
              term_error::unclosed_single_quote,
              // term_loc: where the problematic term begins (the quote)
              {.idx = 1, .off = 7, .len = 10, .b = {1, 8}, .e = {0, 0}},
              // error_loc: where the error begins (also the quote)
              {.idx = 1, .off = 7, .len = 10, .b = {1, 8}, .e = {0, 0}}),
          entry_file_leave(0),
          entry_term(0, "after", idx(2)),
      }));
}

TEST_F(ArgsTest, UnclosedDoubleQuoteCallsOnTermError) {
  // Unclosed double quotes should call on_term_error and continue with parent
  write_file("unclosed_double.args", R"(
      --flag "unclosed
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@unclosed_double.args",
      "after",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--flag",
          "after", // continues after error
      }));

  // File content after stripLeftMargin: "--flag \"unclosed\n"
  // --flag is at offset 0, len 6
  // "unclosed starts at offset 7 (the quote)
  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "unclosed_double.args", idx(1)),
          entry_file_enter(0, "unclosed_double.args", idx(1)),
          entry_term(
              1,
              "--flag",
              {.idx = 0, .off = 0, .len = 6, .b = {1, 1}, .e = {1, 6}}),
          entry_term_error(
              1,
              term_error::unclosed_double_quote,
              // term_loc: where the problematic term begins (the quote)
              {.idx = 1, .off = 7, .len = 10, .b = {1, 8}, .e = {0, 0}},
              // error_loc: where the error begins (also the quote)
              {.idx = 1, .off = 7, .len = 10, .b = {1, 8}, .e = {0, 0}}),
          entry_file_leave(0),
          entry_term(0, "after", idx(2)),
      }));
}

TEST_F(ArgsTest, UnclosedQuoteInNestedFileContinuesParent) {
  // Unclosed quotes in nested file should continue processing parent file
  write_file("outer.args", R"(
      --outer-start @inner_bad.args --outer-end
  )");
  write_file("inner_bad.args", R"(
      --inner-start 'unclosed
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@outer.args",
      "final",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--outer-start",
          "--inner-start",
          "--outer-end", // continues after inner file error
          "final",
      }));

  // outer.args content: "--outer-start @inner_bad.args --outer-end\n"
  // inner_bad.args content: "--inner-start 'unclosed\n"
  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "outer.args", idx(1)),
          entry_file_enter(0, "outer.args", idx(1)),
          entry_term(
              1,
              "--outer-start",
              {.idx = 0, .off = 0, .len = 13, .b = {1, 1}, .e = {1, 13}}),
          entry_file_found(
              1,
              "inner_bad.args",
              {.idx = 1, .off = 14, .len = 15, .b = {1, 15}, .e = {1, 29}}),
          entry_file_enter(
              1,
              "inner_bad.args",
              {.idx = 1, .off = 14, .len = 15, .b = {1, 15}, .e = {1, 29}}),
          entry_term(
              2,
              "--inner-start",
              {.idx = 0, .off = 0, .len = 13, .b = {1, 1}, .e = {1, 13}}),
          entry_term_error(
              2,
              term_error::unclosed_single_quote,
              // term_loc: where the problematic term begins (the quote)
              {.idx = 1, .off = 14, .len = 10, .b = {1, 15}, .e = {0, 0}},
              // error_loc: where the error begins (also the quote)
              {.idx = 1, .off = 14, .len = 10, .b = {1, 15}, .e = {0, 0}}),
          entry_file_leave(1),
          entry_term(
              1,
              "--outer-end",
              {.idx = 2, .off = 30, .len = 11, .b = {1, 31}, .e = {1, 41}}),
          entry_file_leave(0),
          entry_term(0, "final", idx(2)),
      }));
}

TEST_F(ArgsTest, SingleQuotesAreLiteral) {
  // Single quotes should preserve everything literally, including backslashes
  write_file("single_literal.args", R"(
      'hello\nworld'
      'with\\backslash'
      'with"double'
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@single_literal.args",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "hello\\nworld", // backslash-n preserved literally
          "with\\\\backslash", // double backslash preserved
          "with\"double", // double quote preserved
      }));
}

TEST_F(ArgsTest, EscapedHash) {
  // Test that \# escape sequence works correctly
  // \# prevents the # from starting a comment
  write_file("escaped_special.args", R"(
      \#not-a-comment
      prefix\#suffix
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@escaped_special.args",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "#not-a-comment", // \# becomes literal #
          "prefix#suffix", // \# in middle of token
      }));
}

TEST_F(ArgsTest, EscapedHashInDoubleQuotes) {
  // Test that \# works inside double quotes
  write_file("escaped_in_quotes.args", R"(
      "\#hash" "prefix@suffix"
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@escaped_in_quotes.args",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  // \# produces literal #
  // The @ in the middle of a term doesn't trigger args-file processing
  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "#hash",
          "prefix@suffix",
      }));
}

TEST_F(ArgsTest, UnknownEscapeCallsOnTermError) {
  // Unknown escape sequences should call on_term_error
  write_file("unknown_escape.args", R"(
      \x \y \z
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@unknown_escape.args",
      "after",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "after", // continues after error
      }));

  // File content: "\x \y \z\n"
  // \x starts at offset 0
  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "unknown_escape.args", idx(1)),
          entry_file_enter(0, "unknown_escape.args", idx(1)),
          entry_term_error(
              1,
              term_error::unrecognized_escape_sequence,
              // term_loc: where the problematic term begins (the backslash)
              {.idx = 0, .off = 0, .len = 9, .b = {1, 1}, .e = {0, 0}},
              // error_loc: where the error begins (also the backslash)
              {.idx = 0, .off = 0, .len = 9, .b = {1, 1}, .e = {0, 0}}),
          entry_file_leave(0),
          entry_term(0, "after", idx(2)),
      }));
}

TEST_F(ArgsTest, DoubleAtEscapeAtTopLevel) {
  // Test that @@ at the start of a top-level argument produces a literal @
  // followed by the rest of the argument
  auto const args = std::vector<std::string>{
      "prog",
      "@@literal",
      "@@file.args",
      "@@",
      "normal",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "@literal", // @@ becomes @
          "@file.args", // @@ becomes @, not treated as args-file
          "@", // @@ becomes @
          "normal",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_term(0, "@literal", idx(1)),
          entry_term(0, "@file.args", idx(2)),
          entry_term(0, "@", idx(3)),
          entry_term(0, "normal", idx(4)),
      }));
}

TEST_F(ArgsTest, DoubleAtEscapeInArgsFile) {
  // Test that @@ at the start of a term in an args-file produces a literal @
  write_file("double_at.args", R"(
      @@literal
      @@file.args
      @@
      normal
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@double_at.args",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "@literal", // @@ becomes @
          "@file.args", // @@ becomes @, not treated as args-file
          "@", // @@ becomes @
          "normal",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "double_at.args", idx(1)),
          entry_file_enter(0, "double_at.args", idx(1)),
          entry_term(
              1,
              "@literal",
              {.idx = 0, .off = 0, .len = 9, .b = {1, 1}, .e = {1, 9}}),
          entry_term(
              1,
              "@file.args",
              {.idx = 1, .off = 10, .len = 11, .b = {2, 1}, .e = {2, 11}}),
          entry_term(
              1,
              "@",
              {.idx = 2, .off = 22, .len = 2, .b = {3, 1}, .e = {3, 2}}),
          entry_term(
              1,
              "normal",
              {.idx = 3, .off = 25, .len = 6, .b = {4, 1}, .e = {4, 6}}),
          entry_file_leave(0),
      }));
}

TEST_F(ArgsTest, AtInsideQuotesNotArgsFile) {
  // Test that @ inside quotes is not treated as args-file reference
  // "@foo" => term "@foo" (not args-file)
  // '@foo' => term "@foo" (not args-file)
  write_file("at_in_quotes.args", R"(
      "@foo"
      '@bar'
      prefix"@middle"suffix
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@at_in_quotes.args",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "@foo", // @ inside double quotes, not args-file
          "@bar", // @ inside single quotes, not args-file
          "prefix@middlesuffix", // @ inside quotes in middle of term
      }));
}

TEST_F(ArgsTest, AtOutsideQuotesThenQuotedFilename) {
  // Test that @"foo" treats @ as args-file prefix and "foo" as quoted filename
  // @"foo" => recursive args-file named "foo"
  // @'bar' => recursive args-file named "bar"
  write_file("foo", "from_foo");
  write_file("bar", "from_bar");
  write_file("at_then_quoted.args", R"(
      @"foo"
      @'bar'
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@at_then_quoted.args",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "from_foo", // @"foo" => args-file "foo"
          "from_bar", // @'bar' => args-file "bar"
      }));
}

TEST_F(ArgsTest, DoubleAtInsideQuotesNotEscaped) {
  // Test that @@ inside quotes is NOT escaped - it stays as @@
  // "@@foo" => term "@@foo"
  // '@@bar' => term "@@bar"
  write_file("double_at_in_quotes.args", R"(
      "@@foo"
      '@@bar'
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@double_at_in_quotes.args",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "@@foo", // @@ inside double quotes, not escaped
          "@@bar", // @@ inside single quotes, not escaped
      }));
}

TEST_F(ArgsTest, DoubleAtOutsideQuotesThenQuotedContent) {
  // Test that @@"foo" has @@ outside quotes (escaped to @) then quoted content
  // @@"foo" => term "@foo"
  // @@'bar' => term "@bar"
  write_file("double_at_then_quoted.args", R"(
      @@"foo"
      @@'bar'
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@double_at_then_quoted.args",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "@foo", // @@"foo" => @@ escaped to @, then "foo" quoted
          "@bar", // @@'bar' => @@ escaped to @, then 'bar' quoted
      }));
}

TEST_F(ArgsTest, QuotedAtThenUnquoted) {
  // Test that "@"foo has @ inside quotes (not special) then unquoted foo
  // "@"foo => term "@foo"
  write_file("quoted_at_then_unquoted.args", R"(
      "@"foo
      '@'bar
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@quoted_at_then_unquoted.args",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "@foo", // "@"foo => @ inside quotes, then foo unquoted
          "@bar", // '@'bar => @ inside quotes, then bar unquoted
      }));
}

TEST_F(ArgsTest, BareAtInArgsFileTriggersFileError) {
  // Test that bare @ in an args-file is treated as args-file with empty name
  // This should trigger on_file_found and on_file_error (empty filename)
  write_file("bare_at.args", R"(
      --before
      @
      --after
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@bare_at.args",
      "final",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--before",
          "--after", // continues after file error
          "final",
      }));

  // Empty filename resolves to current directory, which fails with
  // is_a_directory
  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "bare_at.args", idx(1)),
          entry_file_enter(0, "bare_at.args", idx(1)),
          entry_term(
              1,
              "--before",
              {.idx = 0, .off = 0, .len = 8, .b = {1, 1}, .e = {1, 8}}),
          entry_file_found(
              1, "", {.idx = 1, .off = 9, .len = 1, .b = {2, 1}, .e = {2, 1}}),
          entry_file_error_is_a_directory(
              1, "", {.idx = 1, .off = 9, .len = 1, .b = {2, 1}, .e = {2, 1}}),
          entry_term(
              1,
              "--after",
              {.idx = 2, .off = 11, .len = 7, .b = {3, 1}, .e = {3, 7}}),
          entry_file_leave(0),
          entry_term(0, "final", idx(2)),
      }));
}

TEST_F(ArgsTest, DirectCyclicalReference) {
  // Test that a file referencing itself is detected as cyclical
  write_file("self.args", R"(
    --before @self.args --after
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@self.args",
      "final",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  // Should process --before, detect cyclical reference, skip it, then --after
  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--before",
          "--after",
          "final",
      }));

  // File content: "--before @self.args --after\n"
  // --before at offset 0, len 8
  // @self.args at offset 9, len 10
  // --after at offset 20, len 7
  auto canonical = std::filesystem::canonical(temp_dir() / "self.args");
  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "self.args", idx(1)),
          entry_file_enter(0, "self.args", idx(1)),
          entry_term(
              1,
              "--before",
              {.idx = 0, .off = 0, .len = 8, .b = {1, 1}, .e = {1, 8}}),
          entry_file_found(
              1,
              "self.args",
              {.idx = 1, .off = 9, .len = 10, .b = {1, 10}, .e = {1, 19}}),
          entry_file_cycle(
              1,
              "self.args",
              {.idx = 1, .off = 9, .len = 10, .b = {1, 10}, .e = {1, 19}},
              canonical),
          entry_term(
              1,
              "--after",
              {.idx = 2, .off = 20, .len = 7, .b = {1, 21}, .e = {1, 27}}),
          entry_file_leave(0),
          entry_term(0, "final", idx(2)),
      }));
}

TEST_F(ArgsTest, IndirectCyclicalReference) {
  // Test that A -> B -> A is detected as cyclical
  write_file("a.args", R"(
    --from-a @b.args --end-a
  )");
  write_file("b.args", R"(
    --from-b @a.args --end-b
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@a.args",
      "final",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  // Should process a.args, then b.args, detect cyclical back to a.args
  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--from-a",
          "--from-b",
          "--end-b",
          "--end-a",
          "final",
      }));

  // a.args content: "--from-a @b.args --end-a\n"
  // --from-a at offset 0, len 8
  // @b.args at offset 9, len 7
  // --end-a at offset 17, len 7
  // b.args content: "--from-b @a.args --end-b\n"
  // --from-b at offset 0, len 8
  // @a.args at offset 9, len 7
  // --end-b at offset 17, len 7
  auto canonical_a = std::filesystem::canonical(temp_dir() / "a.args");
  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "a.args", idx(1)),
          entry_file_enter(0, "a.args", idx(1)),
          entry_term(
              1,
              "--from-a",
              {.idx = 0, .off = 0, .len = 8, .b = {1, 1}, .e = {1, 8}}),
          entry_file_found(
              1,
              "b.args",
              {.idx = 1, .off = 9, .len = 7, .b = {1, 10}, .e = {1, 16}}),
          entry_file_enter(
              1,
              "b.args",
              {.idx = 1, .off = 9, .len = 7, .b = {1, 10}, .e = {1, 16}}),
          entry_term(
              2,
              "--from-b",
              {.idx = 0, .off = 0, .len = 8, .b = {1, 1}, .e = {1, 8}}),
          entry_file_found(
              2,
              "a.args",
              {.idx = 1, .off = 9, .len = 7, .b = {1, 10}, .e = {1, 16}}),
          entry_file_cycle(
              2,
              "a.args",
              {.idx = 1, .off = 9, .len = 7, .b = {1, 10}, .e = {1, 16}},
              canonical_a),
          entry_term(
              2,
              "--end-b",
              {.idx = 2, .off = 17, .len = 7, .b = {1, 18}, .e = {1, 24}}),
          entry_file_leave(1),
          entry_term(
              1,
              "--end-a",
              {.idx = 2, .off = 17, .len = 7, .b = {1, 18}, .e = {1, 24}}),
          entry_file_leave(0),
          entry_term(0, "final", idx(2)),
      }));
}

TEST_F(ArgsTest, LongCyclicalChain) {
  // Test that A -> B -> C -> A is detected as cyclical
  write_file("chain_a.args", R"(
    --from-a @chain_b.args --end-a
  )");
  write_file("chain_b.args", R"(
    --from-b @chain_c.args --end-b
  )");
  write_file("chain_c.args", R"(
    --from-c @chain_a.args --end-c
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@chain_a.args",
      "final",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--from-a",
          "--from-b",
          "--from-c",
          "--end-c",
          "--end-b",
          "--end-a",
          "final",
      }));

  // chain_a.args content: "--from-a @chain_b.args --end-a\n"
  // chain_b.args content: "--from-b @chain_c.args --end-b\n"
  // chain_c.args content: "--from-c @chain_a.args --end-c\n"
  auto canonical_a = std::filesystem::canonical(temp_dir() / "chain_a.args");
  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "chain_a.args", idx(1)),
          entry_file_enter(0, "chain_a.args", idx(1)),
          entry_term(
              1,
              "--from-a",
              {.idx = 0, .off = 0, .len = 8, .b = {1, 1}, .e = {1, 8}}),
          entry_file_found(
              1,
              "chain_b.args",
              {.idx = 1, .off = 9, .len = 13, .b = {1, 10}, .e = {1, 22}}),
          entry_file_enter(
              1,
              "chain_b.args",
              {.idx = 1, .off = 9, .len = 13, .b = {1, 10}, .e = {1, 22}}),
          entry_term(
              2,
              "--from-b",
              {.idx = 0, .off = 0, .len = 8, .b = {1, 1}, .e = {1, 8}}),
          entry_file_found(
              2,
              "chain_c.args",
              {.idx = 1, .off = 9, .len = 13, .b = {1, 10}, .e = {1, 22}}),
          entry_file_enter(
              2,
              "chain_c.args",
              {.idx = 1, .off = 9, .len = 13, .b = {1, 10}, .e = {1, 22}}),
          entry_term(
              3,
              "--from-c",
              {.idx = 0, .off = 0, .len = 8, .b = {1, 1}, .e = {1, 8}}),
          entry_file_found(
              3,
              "chain_a.args",
              {.idx = 1, .off = 9, .len = 13, .b = {1, 10}, .e = {1, 22}}),
          entry_file_cycle(
              3,
              "chain_a.args",
              {.idx = 1, .off = 9, .len = 13, .b = {1, 10}, .e = {1, 22}},
              canonical_a),
          entry_term(
              3,
              "--end-c",
              {.idx = 2, .off = 23, .len = 7, .b = {1, 24}, .e = {1, 30}}),
          entry_file_leave(2),
          entry_term(
              2,
              "--end-b",
              {.idx = 2, .off = 23, .len = 7, .b = {1, 24}, .e = {1, 30}}),
          entry_file_leave(1),
          entry_term(
              1,
              "--end-a",
              {.idx = 2, .off = 23, .len = 7, .b = {1, 24}, .e = {1, 30}}),
          entry_file_leave(0),
          entry_term(0, "final", idx(2)),
      }));
}

TEST_F(ArgsTest, SameFileReferencedTwiceNotCyclical) {
  // Test that referencing the same file twice (not nested) is NOT cyclical
  // A -> B, then A -> B again (sequentially, not nested)
  write_file("shared.args", R"(
    --shared-content
  )");
  write_file("uses_shared.args", R"(
    --first @shared.args --second @shared.args --third
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@uses_shared.args",
      "final",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args);

  // shared.args should be processed twice (not circular since not nested)
  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--first",
          "--shared-content",
          "--second",
          "--shared-content",
          "--third",
          "final",
      }));

  // uses_shared.args content: "--first @shared.args --second @shared.args
  // --third\n"
  // --first at offset 0, len 7
  // @shared.args at offset 8, len 12
  // --second at offset 21, len 8
  // @shared.args at offset 30, len 12
  // --third at offset 43, len 7
  // shared.args content: "--shared-content\n"
  // --shared-content at offset 0, len 16
  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "uses_shared.args", idx(1)),
          entry_file_enter(0, "uses_shared.args", idx(1)),
          entry_term(
              1,
              "--first",
              {.idx = 0, .off = 0, .len = 7, .b = {1, 1}, .e = {1, 7}}),
          entry_file_found(
              1,
              "shared.args",
              {.idx = 1, .off = 8, .len = 12, .b = {1, 9}, .e = {1, 20}}),
          entry_file_enter(
              1,
              "shared.args",
              {.idx = 1, .off = 8, .len = 12, .b = {1, 9}, .e = {1, 20}}),
          entry_term(
              2,
              "--shared-content",
              {.idx = 0, .off = 0, .len = 16, .b = {1, 1}, .e = {1, 16}}),
          entry_file_leave(1),
          entry_term(
              1,
              "--second",
              {.idx = 2, .off = 21, .len = 8, .b = {1, 22}, .e = {1, 29}}),
          entry_file_found(
              1,
              "shared.args",
              {.idx = 3, .off = 30, .len = 12, .b = {1, 31}, .e = {1, 42}}),
          entry_file_enter(
              1,
              "shared.args",
              {.idx = 3, .off = 30, .len = 12, .b = {1, 31}, .e = {1, 42}}),
          entry_term(
              2,
              "--shared-content",
              {.idx = 0, .off = 0, .len = 16, .b = {1, 1}, .e = {1, 16}}),
          entry_file_leave(1),
          entry_term(
              1,
              "--third",
              {.idx = 4, .off = 43, .len = 7, .b = {1, 44}, .e = {1, 50}}),
          entry_file_leave(0),
          entry_term(0, "final", idx(2)),
      }));
}

// Tests for the simple cli_apply_args_files overload that returns vector and
// throws

TEST_F(ArgsTest, SimpleOverloadBasicArguments) {
  // Test basic arguments without any @file references
  auto const args = std::vector<std::string>{
      "prog",
      "--flag",
      "value",
      "arg",
  };

  auto result = cli_apply_args_files(temp_dir(), args);

  EXPECT_THAT(
      result,
      ElementsAreArray({
          "prog",
          "--flag",
          "value",
          "arg",
      }));
}

TEST_F(ArgsTest, SimpleOverloadArgsFileExpansion) {
  // Test that @file references are expanded
  write_file("simple.args", R"(
    --option1 value1 --option2
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@simple.args",
      "trailing",
  };

  auto result = cli_apply_args_files(temp_dir(), args);

  EXPECT_THAT(
      result,
      ElementsAreArray({
          "prog",
          "--option1",
          "value1",
          "--option2",
          "trailing",
      }));
}

TEST_F(ArgsTest, SimpleOverloadNestedArgsFiles) {
  // Test nested @file expansion
  write_file("inner.args", R"(
    --inner-flag inner_value
  )");

  write_file("outer.args", R"(
    --outer-flag @inner.args outer_value
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@outer.args",
  };

  auto result = cli_apply_args_files(temp_dir(), args);

  EXPECT_THAT(
      result,
      ElementsAreArray({
          "prog",
          "--outer-flag",
          "--inner-flag",
          "inner_value",
          "outer_value",
      }));
}

TEST_F(ArgsTest, SimpleOverloadDoubleAtEscape) {
  // Test that @@ is escaped to @
  auto const args = std::vector<std::string>{
      "prog",
      "@@literal",
      "@@file.args",
  };

  auto result = cli_apply_args_files(temp_dir(), args);

  EXPECT_THAT(
      result,
      ElementsAreArray({
          "prog",
          "@literal",
          "@file.args",
      }));
}

TEST_F(ArgsTest, SimpleOverloadThrowsOnMissingFile) {
  // Test that missing file throws cli_apply_args_files_error
  auto const args = std::vector<std::string>{
      "prog",
      "@nonexistent.args",
  };

  EXPECT_THROW(
      cli_apply_args_files(temp_dir(), args), cli_apply_args_files_error);
}

TEST_F(ArgsTest, SimpleOverloadThrowsOnUnclosedQuote) {
  // Test that unclosed quote in args file throws cli_apply_args_files_error
  write_file("unclosed.args", R"(
    --flag 'unclosed
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@unclosed.args",
  };

  EXPECT_THROW(
      cli_apply_args_files(temp_dir(), args), cli_apply_args_files_error);
}

TEST_F(ArgsTest, SimpleOverloadThrowsOnUnknownEscape) {
  // Test that unknown escape sequence throws cli_apply_args_files_error
  write_file("bad_escape.args", R"(
    \x
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@bad_escape.args",
  };

  EXPECT_THROW(
      cli_apply_args_files(temp_dir(), args), cli_apply_args_files_error);
}

TEST_F(ArgsTest, SimpleOverloadErrorMessageContainsFilename) {
  // Test that error message contains the filename
  write_file("error_file.args", R"(
    'unclosed
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@error_file.args",
  };

  try {
    cli_apply_args_files(temp_dir(), args);
    FAIL() << "Expected cli_apply_args_files_error to be thrown";
  } catch (const cli_apply_args_files_error& e) {
    std::string what = e.what();
    EXPECT_THAT(what, testing::HasSubstr("error_file.args"));
    EXPECT_THAT(what, testing::HasSubstr("unclosed single quote"));
  }
}

TEST_F(ArgsTest, SimpleOverloadErrorMessageContainsLineCol) {
  // Test that error message contains line and column info
  write_file("line_col.args", R"(
    --flag1 value1
    --flag2 "unclosed
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@line_col.args",
  };

  try {
    cli_apply_args_files(temp_dir(), args);
    FAIL() << "Expected cli_apply_args_files_error to be thrown";
  } catch (const cli_apply_args_files_error& e) {
    std::string what = e.what();
    // The unclosed quote is on line 2
    EXPECT_THAT(what, testing::HasSubstr("line 2"));
  }
}

// Tests for circular reference detection

TEST_F(ArgsTest, SimpleOverloadThrowsOnCircularReference) {
  // Test that the simple overload throws on circular reference
  write_file("circular.args", R"(
    --before @circular.args --after
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@circular.args",
  };

  EXPECT_THROW(
      cli_apply_args_files(temp_dir(), args), cli_apply_args_files_error);
}

TEST_F(ArgsTest, SimpleOverloadCircularErrorMessage) {
  // Test that circular reference error message is informative
  write_file("loop.args", R"(
    @loop.args
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@loop.args",
  };

  try {
    cli_apply_args_files(temp_dir(), args);
    FAIL() << "Expected cli_apply_args_files_error to be thrown";
  } catch (const cli_apply_args_files_error& e) {
    std::string what = e.what();
    EXPECT_THAT(what, testing::HasSubstr("cycle"));
    EXPECT_THAT(what, testing::HasSubstr("loop.args"));
  }
}

// Test cases for cli_apply_args_files_options max_depth

TEST_F(ArgsTest, OptionsMaxDepthZeroSkipsAllFiles) {
  write_file("file1.args", R"(
    --from-file1
  )");
  write_file("file2.args", R"(
    --from-file2
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@file1.args",
      "@file2.args",
      "final",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args, {.max_depth = 0});

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "final",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          // on_file_found called, receiver returns dive, but library calls
          // on_file_error due to max_depth
          entry_file_found(0, "file1.args", idx(1)),
          entry_file_error_max_depth(0, "file1.args", idx(1)),
          entry_file_found(0, "file2.args", idx(2)),
          entry_file_error_max_depth(0, "file2.args", idx(2)),
          entry_term(0, "final", idx(3)),
      }));
}

TEST_F(ArgsTest, OptionsMaxDepthOneAllowsTopLevelFiles) {
  write_file("level1.args", R"(
    --l1-start @level2.args --l1-end
  )");
  write_file("level2.args", R"(
    --l2-content
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@level1.args",
      "final",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args, {.max_depth = 1});

  // level1.args expanded (depth 0 -> 1), but level2.args skipped (depth 1 -> 2)
  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--l1-start",
          "--l1-end",
          "final",
      }));

  EXPECT_THAT(
      receiver.entries,
      ElementsAreArray({
          entry_term(0, "prog", idx(0)),
          entry_file_found(0, "level1.args", idx(1)),
          entry_file_enter(0, "level1.args", idx(1)),
          entry_term(
              1,
              "--l1-start",
              {.idx = 0, .off = 0, .len = 10, .b = {1, 1}, .e = {1, 10}}),
          // on_file_found called at depth 1, but library calls on_file_error
          // due to max_depth
          entry_file_found(
              1,
              "level2.args",
              {.idx = 1, .off = 11, .len = 12, .b = {1, 12}, .e = {1, 23}}),
          entry_file_error_max_depth(
              1,
              "level2.args",
              {.idx = 1, .off = 11, .len = 12, .b = {1, 12}, .e = {1, 23}}),
          entry_term(
              1,
              "--l1-end",
              {.idx = 2, .off = 24, .len = 8, .b = {1, 25}, .e = {1, 32}}),
          entry_file_leave(0),
          entry_term(0, "final", idx(2)),
      }));
}

TEST_F(ArgsTest, OptionsMaxDepthTwoAllowsTwoLevels) {
  write_file("d1.args", R"(
    --d1 @d2.args
  )");
  write_file("d2.args", R"(
    --d2 @d3.args
  )");
  write_file("d3.args", R"(
    --d3
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@d1.args",
      "final",
  };

  TestReceiver receiver;
  cli_apply_args_files(receiver, temp_dir(), args, {.max_depth = 2});

  // d1.args expanded (depth 0 -> 1), d2.args expanded (depth 1 -> 2),
  // d3.args skipped (depth 2 -> 3)
  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--d1",
          "--d2",
          "final",
      }));
}

TEST_F(ArgsTest, OptionsDefaultMaxDepthAllowsDeepNesting) {
  // Default max_depth is 64, which should allow several levels of nesting
  write_file("deep1.args", R"(
    --d1 @deep2.args
  )");
  write_file("deep2.args", R"(
    --d2 @deep3.args
  )");
  write_file("deep3.args", R"(
    --d3 @deep4.args
  )");
  write_file("deep4.args", R"(
    --d4
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@deep1.args",
  };

  TestReceiver receiver;
  // Default options (max_depth = 64) — all 4 levels should expand
  cli_apply_args_files(receiver, temp_dir(), args);

  EXPECT_THAT(
      receiver.strings,
      ElementsAreArray({
          "prog",
          "--d1",
          "--d2",
          "--d3",
          "--d4",
      }));
}

TEST_F(ArgsTest, SimpleOverloadRespectsDefaultMaxDepth) {
  // The simple overload uses default options (max_depth = 64)
  // Create a chain of 3 levels — well within limit
  write_file("s1.args", R"(
    --s1 @s2.args
  )");
  write_file("s2.args", R"(
    --s2 @s3.args
  )");
  write_file("s3.args", R"(
    --s3
  )");

  auto const args = std::vector<std::string>{
      "prog",
      "@s1.args",
      "end",
  };

  auto result = cli_apply_args_files(temp_dir(), args);

  EXPECT_THAT(
      result,
      ElementsAreArray({
          "prog",
          "--s1",
          "--s2",
          "--s3",
          "end",
      }));
}
