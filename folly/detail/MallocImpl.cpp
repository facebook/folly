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

#include <folly/detail/Malloc.h>

extern "C" {

#if !FOLLY_HAVE_WEAK_SYMBOLS
void* (*mallocx)(size_t, int) = nullptr;
void* (*rallocx)(void*, size_t, int) = nullptr;
size_t (*xallocx)(void*, size_t, size_t, int) = nullptr;
size_t (*sallocx)(const void*, int) = nullptr;
void (*dallocx)(void*, int) = nullptr;
void (*sdallocx)(void*, size_t, int) = nullptr;
size_t (*nallocx)(size_t, int) = nullptr;
int (*mallctl)(const char*, void*, size_t*, void*, size_t) = nullptr;
int (*mallctlnametomib)(const char*, size_t*, size_t*) = nullptr;
int (*mallctlbymib)(const size_t*, size_t, void*, size_t*, void*, size_t) =
    nullptr;
#endif

}
