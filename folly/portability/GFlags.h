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

#include <folly/portability/Config.h>

#include <cstdint>
#include <string>

#if FOLLY_HAVE_LIBGFLAGS && __has_include(<gflags/gflags.h>)
#include <gflags/gflags.h>
#endif

#define FOLLY_GFLAGS_DECLARE_FALLBACK_(_type, _shortType, _name) \
  namespace fL##_shortType {                                     \
    extern _type FLAGS_##_name;                                  \
  }                                                              \
  using fL##_shortType::FLAGS_##_name

#define FOLLY_GFLAGS_DEFINE_FALLBACK_(                \
    _type, _shortType, _name, _default, _description) \
  namespace fL##_shortType {                          \
    _type FLAGS_##_name = _default;                   \
  }                                                   \
  using fL##_shortType::FLAGS_##_name

#if FOLLY_HAVE_LIBGFLAGS && __has_include(<gflags/gflags.h>)

#define FOLLY_GFLAGS_DECLARE_bool(_name) DECLARE_bool(_name)
#define FOLLY_GFLAGS_DECLARE_double(_name) DECLARE_double(_name)
#define FOLLY_GFLAGS_DECLARE_int32(_name) DECLARE_int32(_name)
#define FOLLY_GFLAGS_DECLARE_int64(_name) DECLARE_int64(_name)
#define FOLLY_GFLAGS_DECLARE_uint64(_name) DECLARE_uint64(_name)
#define FOLLY_GFLAGS_DECLARE_string(_name) DECLARE_string(_name)

#if defined(DECLARE_uint32)
#define FOLLY_GFLAGS_DECLARE_uint32(_name) DECLARE_uint32(_name)
#else
#define FOLLY_GFLAGS_DECLARE_uint32(_name) \
  FOLLY_GFLAGS_DECLARE_FALLBACK_(::std::uint32_t, U32, _name)
#endif

#define FOLLY_GFLAGS_DEFINE_bool(_name, _default, _description) \
  DEFINE_bool(_name, _default, _description)
#define FOLLY_GFLAGS_DEFINE_double(_name, _default, _description) \
  DEFINE_double(_name, _default, _description)
#define FOLLY_GFLAGS_DEFINE_int32(_name, _default, _description) \
  DEFINE_int32(_name, _default, _description)
#define FOLLY_GFLAGS_DEFINE_int64(_name, _default, _description) \
  DEFINE_int64(_name, _default, _description)
#define FOLLY_GFLAGS_DEFINE_uint64(_name, _default, _description) \
  DEFINE_uint64(_name, _default, _description)
#define FOLLY_GFLAGS_DEFINE_string(_name, _default, _description) \
  DEFINE_string(_name, _default, _description)

#if defined(DEFINE_uint32)
#define FOLLY_GFLAGS_DEFINE_uint32(_name, _default, _description) \
  DEFINE_uint32(_name, _default, _description)
#else
#define FOLLY_GFLAGS_DEFINE_uint32(_name, _default, _description) \
  FOLLY_GFLAGS_DEFINE_FALLBACK_(                                  \
      ::std::uint32_t, U32, _name, _default, _description)
#endif

#else

#define FOLLY_GFLAGS_DECLARE_bool(_name) \
  FOLLY_GFLAGS_DECLARE_FALLBACK_(bool, B, _name)
#define FOLLY_GFLAGS_DECLARE_double(_name) \
  FOLLY_GFLAGS_DECLARE_FALLBACK_(double, D, _name)
#define FOLLY_GFLAGS_DECLARE_int32(_name) \
  FOLLY_GFLAGS_DECLARE_FALLBACK_(::std::int32_t, I, _name)
#define FOLLY_GFLAGS_DECLARE_int64(_name) \
  FOLLY_GFLAGS_DECLARE_FALLBACK_(::std::int64_t, I64, _name)
#define FOLLY_GFLAGS_DECLARE_uint32(_name) \
  FOLLY_GFLAGS_DECLARE_FALLBACK_(::std::uint32_t, U32, _name)
#define FOLLY_GFLAGS_DECLARE_uint64(_name) \
  FOLLY_GFLAGS_DECLARE_FALLBACK_(::std::uint64_t, U64, _name)
#define FOLLY_GFLAGS_DECLARE_string(_name) \
  FOLLY_GFLAGS_DECLARE_FALLBACK_(::std::string, S, _name)

#define FOLLY_GFLAGS_DEFINE_bool(_name, _default, _description) \
  FOLLY_GFLAGS_DEFINE_FALLBACK_(bool, B, _name, _default, _description)
#define FOLLY_GFLAGS_DEFINE_double(_name, _default, _description) \
  FOLLY_GFLAGS_DEFINE_FALLBACK_(double, D, _name, _default, _description)
#define FOLLY_GFLAGS_DEFINE_int32(_name, _default, _description) \
  FOLLY_GFLAGS_DEFINE_FALLBACK_(                                 \
      ::std::int32_t, I, _name, _default, _description)
#define FOLLY_GFLAGS_DEFINE_int64(_name, _default, _description) \
  FOLLY_GFLAGS_DEFINE_FALLBACK_(                                 \
      ::std::int64_t, I64, _name, _default, _description)
#define FOLLY_GFLAGS_DEFINE_uint32(_name, _default, _description) \
  FOLLY_GFLAGS_DEFINE_FALLBACK_(                                  \
      ::std::uint32_t, U32, _name, _default, _description)
#define FOLLY_GFLAGS_DEFINE_uint64(_name, _default, _description) \
  FOLLY_GFLAGS_DEFINE_FALLBACK_(                                  \
      ::std::uint64_t, U64, _name, _default, _description)
#define FOLLY_GFLAGS_DEFINE_string(_name, _default, _description) \
  FOLLY_GFLAGS_DEFINE_FALLBACK_(::std::string, S, _name, _default, _description)

#endif

#if FOLLY_UNUSUAL_GFLAGS_NAMESPACE
namespace FOLLY_GFLAGS_NAMESPACE {}
namespace gflags {
using namespace FOLLY_GFLAGS_NAMESPACE;
} // namespace gflags
#endif

namespace folly {

#if FOLLY_HAVE_LIBGFLAGS && __has_include(<gflags/gflags.h>)

using FlagSaver = gflags::FlagSaver;

#else

namespace detail {
struct GflagsFlagSaver {};
} // namespace detail
using FlagSaver = detail::GflagsFlagSaver;

#endif

} // namespace folly
