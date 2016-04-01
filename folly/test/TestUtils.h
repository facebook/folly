/*
 * Copyright 2016 Facebook, Inc.
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

#pragma once

#include <gtest/gtest.h>

// We use this to indicate that tests have failed because of timing
// or dependencies that may be flakey. Internally this is used by
// our test runner to retry the test. To gtest this will look like
// a normal test failure; there is only an effect if the test framework
// interprets the message.
#define SKIP() GTEST_FATAL_FAILURE_("Test skipped by client")
