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

#if FOLLY_HAVE_LIBGFLAGS
#include <gflags/gflags.h>
#else
#define FOLLY_DECLARE_FLAG(_type, _name) extern _type FLAGS_##_name
#define DECLARE_bool(_name) FOLLY_DECLARE_FLAG(bool, _name)
#define DECLARE_double(_name) FOLLY_DECLARE_FLAG(double, _name)
#define DECLARE_int32(_name) FOLLY_DECLARE_FLAG(int, _name)
#define DECLARE_int64(_name) FOLLY_DECLARE_FLAG(long long, _name)
#define DECLARE_uint64(_name) FOLLY_DECLARE_FLAG(unsigned long long, _name)
#define DECLARE_string(_name) FOLLY_DECLARE_FLAG(std::string, _name)

#define FOLLY_DEFINE_FLAG(_type, _name, _default) _type FLAGS_##_name = _default
#define DEFINE_bool(_name, _default, _description) \
  FOLLY_DEFINE_FLAG(bool, _name, _default)
#define DEFINE_double(_name, _default, _description) \
  FOLLY_DEFINE_FLAG(double, _name, _default)
#define DEFINE_int32(_name, _default, _description) \
  FOLLY_DEFINE_FLAG(int, _name, _default)
#define DEFINE_int64(_name, _default, _description) \
  FOLLY_DEFINE_FLAG(long long, _name, _default)
#define DEFINE_uint64(_name, _default, _description) \
  FOLLY_DEFINE_FLAG(unsigned long long, _name, _default)
#define DEFINE_string(_name, _default, _description) \
  FOLLY_DEFINE_FLAG(std::string, _name, _default)
#endif
