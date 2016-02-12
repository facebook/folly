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

#ifndef FOLLY_DETAIL_MALLOC_H
#define FOLLY_DETAIL_MALLOC_H

#include <stdlib.h>

#include <folly/Portability.h>

extern "C" {

#if FOLLY_HAVE_WEAK_SYMBOLS
void* mallocx(size_t, int) __attribute__((__weak__));
void* rallocx(void*, size_t, int) __attribute__((__weak__));
size_t xallocx(void*, size_t, size_t, int) __attribute__((__weak__));
size_t sallocx(const void*, int) __attribute__((__weak__));
void dallocx(void*, int) __attribute__((__weak__));
void sdallocx(void*, size_t, int) __attribute__((__weak__));
size_t nallocx(size_t, int) __attribute__((__weak__));
int mallctl(const char*, void*, size_t*, void*, size_t)
      __attribute__((__weak__));
int mallctlnametomib(const char*, size_t*, size_t*) __attribute__((__weak__));
int mallctlbymib(const size_t*, size_t, void*, size_t*, void*, size_t)
      __attribute__((__weak__));
#elif defined(_MSC_VER)
// MSVC doesn't have weak symbols, so do some linker magic
// to emulate them.
extern void* (*mallocx)(size_t, int);
extern const char* mallocxWeak = nullptr;
#pragma comment(linker, "/alternatename:_mallocx=_mallocxWeak")
extern void* (*rallocx)(void*, size_t, int);
extern const char* rallocxWeak = nullptr;
#pragma comment(linker, "/alternatename:_rallocx=_rallocxWeak")
extern size_t(*xallocx)(void*, size_t, size_t, int);
extern const char* xallocxWeak = nullptr;
#pragma comment(linker, "/alternatename:_xallocx=_xallocxWeak")
extern size_t(*sallocx)(const void*, int);
extern const char* sallocxWeak = nullptr;
#pragma comment(linker, "/alternatename:_sallocx=_sallocxWeak")
extern void(*dallocx)(void*, int);
extern const char* dallocxWeak = nullptr;
#pragma comment(linker, "/alternatename:_dallocx=_dallocxWeak")
extern void(*sdallocx)(void*, size_t, int);
extern const char* sdallocxWeak = nullptr;
#pragma comment(linker, "/alternatename:_sdallocx=_sdallocxWeak")
extern size_t(*nallocx)(size_t, int);
extern const char* nallocxWeak = nullptr;
#pragma comment(linker, "/alternatename:_nallocx=_nallocxWeak")
extern int(*mallctl)(const char*, void*, size_t*, void*, size_t);
extern const char* mallctlWeak = nullptr;
#pragma comment(linker, "/alternatename:_mallctl=_mallctlWeak")
extern int(*mallctlnametomib)(const char*, size_t*, size_t*);
extern const char* mallctlnametomibWeak = nullptr;
#pragma comment(linker, "/alternatename:_mallctlnametomib=_mallctlnametomibWeak")
extern int(*mallctlbymib)(const size_t*, size_t, void*, size_t*, void*, size_t);
extern const char* mallctlbymibWeak = nullptr;
#pragma comment(linker, "/alternatename:_mallctlbymib=_mallctlbymibWeak")
#else
extern void* (*mallocx)(size_t, int);
extern void* (*rallocx)(void*, size_t, int);
extern size_t (*xallocx)(void*, size_t, size_t, int);
extern size_t (*sallocx)(const void*, int);
extern void (*dallocx)(void*, int);
extern void (*sdallocx)(void*, size_t, int);
extern size_t (*nallocx)(size_t, int);
extern int (*mallctl)(const char*, void*, size_t*, void*, size_t);
extern int (*mallctlnametomib)(const char*, size_t*, size_t*);
extern int (*mallctlbymib)(const size_t*, size_t, void*, size_t*, void*,
                           size_t);
#endif

}

#endif
