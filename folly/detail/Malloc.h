/*
 * Copyright 2014 Facebook, Inc.
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

#ifndef FOLLY_DETAIL_MALLOC_H
#define FOLLY_DETAIL_MALLOC_H

#include <stdlib.h>

#include <folly/Portability.h>

extern "C" {

#if FOLLY_HAVE_WEAK_SYMBOLS
int rallocm(void**, size_t*, size_t, size_t, int) __attribute__((weak));
int allocm(void**, size_t*, size_t, int) __attribute__((weak));
int mallctl(const char*, void*, size_t*, void*, size_t) __attribute__((weak));
#else
extern int (*rallocm)(void**, size_t*, size_t, size_t, int);
extern int (*allocm)(void**, size_t*, size_t, int);
extern int (*mallctl)(const char*, void*, size_t*, void*, size_t);
#endif

}

#endif
