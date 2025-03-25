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

#pragma once

#include <string_view>

#include <folly/settings/Settings.h>
#include <folly/settings/SettingsAccessorProxy.h>

namespace folly::settings {

enum ArgParsingResult { OK, HELP, ERROR };

class CommandLineParser {
 public:
  CommandLineParser(int& argc, char**& argv, SettingsAccessorProxy& flags_info);

  CommandLineParser(CommandLineParser&&) noexcept;
  CommandLineParser& operator=(CommandLineParser&&) noexcept;

  CommandLineParser(const CommandLineParser&) = delete;
  CommandLineParser& operator=(const CommandLineParser&) = delete;

  ~CommandLineParser();

  ArgParsingResult parse();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

/**
 * Main function to parse arguments folly::settings. This function parses
 * command line arguments into folly::settings. It moves all positional, --help,
 * not recognized flags and their values to the end of the argv array. It stops
 * parsing arguments after '--' is encoutered.
 *
 * @param argc number of arguments in command line
 * @param argv vector of command line arguments
 * @param snapshot folly::settings snapshot.
 * @param project default project name for CLI parsing (default: "")
 * @param aliases map of aliases for registered settings
 * @return true if help flag was encountered
 */
ArgParsingResult parseCommandLineArguments(
    int& argc,
    char**& argv,
    std::string_view project = "",
    Snapshot* snapshot = nullptr,
    const SettingsAccessorProxy::SettingAliases& aliases = {});

/**
 * Outputs help message into stderr. May terminate program if error was
 * encoutered during arguments parsing or exit is requested by caller.
 *
 * @param app inforgation about app printed before main help message
 * @param exit_on_help flag to terminate process after printing help
 */
void printHelpIfNeeded(std::string_view app, bool exit_on_help = false);

} // namespace folly::settings
