/*
 * Copyright 2017 Facebook, Inc.
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

#include <folly/portability/Config.h>

#if FOLLY_HAVE_BITS_FUNCTEXCEPT_H
#include <bits/functexcept.h>
#else
#include <new> // Some platforms define __throw_bad_alloc() here.
#include <folly/Portability.h>

FOLLY_NAMESPACE_STD_BEGIN

#if (defined(_LIBCPP_VERSION) && (_LIBCPP_VERSION >= 4000)) ||                 \
    defined(FOLLY_SKIP_LIBCPP_4000_THROW_BACKPORTS)
[[noreturn]] void __throw_length_error(const char* msg);
[[noreturn]] void __throw_logic_error(const char* msg);
[[noreturn]] void __throw_out_of_range(const char* msg);
#else
void __throw_length_error(const char *) __attribute__((__noreturn__));
void __throw_logic_error(const char *) __attribute__((__noreturn__));
void __throw_out_of_range(const char *) __attribute__((__noreturn__));
#endif

#ifdef _MSC_VER
[[noreturn]] void __throw_bad_alloc();
#endif

FOLLY_NAMESPACE_STD_END
#endif
