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

#include <stdlib.h>

#include <folly/Portability.h>

extern "C" {

#if FOLLY_HAVE_WEAK_SYMBOLS
#if !defined(__FreeBSD__)
void* mallocx(size_t, int) __attribute__((__nothrow__, __weak__));
void* rallocx(void*, size_t, int) __attribute__((__nothrow__, __weak__));
size_t xallocx(void*, size_t, size_t, int)
    __attribute__((__nothrow__, __weak__));
size_t sallocx(const void*, int) __attribute__((__nothrow__, __weak__));
void dallocx(void*, int) __attribute__((__nothrow__, __weak__));
void sdallocx(void*, size_t, int) __attribute__((__nothrow__, __weak__));
size_t nallocx(size_t, int) __attribute__((__nothrow__, __weak__));
int mallctl(const char*, void*, size_t*, void*, size_t)
    __attribute__((__nothrow__, __weak__));
int mallctlnametomib(const char*, size_t*, size_t*)
    __attribute__((__nothrow__, __weak__));
int mallctlbymib(const size_t*, size_t, void*, size_t*, void*, size_t)
    __attribute__((__nothrow__, __weak__));
#endif
bool MallocExtension_Internal_GetNumericProperty(const char*, size_t, size_t*)
    __attribute__((__weak__));
#else
#if (!(defined(USE_JEMALLOC) && USE_JEMALLOC != 0) && !defined(FOLLY_USE_JEMALLOC)) || FOLLY_SANITIZE
// we do not want to declare these if we have jemalloc support
// to avoid redefinition errors
extern void* (*mallocx)(size_t, int);
extern void* (*rallocx)(void*, size_t, int);
extern size_t (*xallocx)(void*, size_t, size_t, int);
extern size_t (*sallocx)(const void*, int);
extern void (*dallocx)(void*, int);
extern void (*sdallocx)(void*, size_t, int);
extern size_t (*nallocx)(size_t, int);
extern int (*mallctl)(const char*, void*, size_t*, void*, size_t);
extern int (*mallctlnametomib)(const char*, size_t*, size_t*);
extern int (*mallctlbymib)(
    const size_t*, size_t, void*, size_t*, void*, size_t);
#endif
extern bool (*MallocExtension_Internal_GetNumericProperty)(
    const char*, size_t, size_t*);
#ifdef _MSC_VER
// We emulate weak linkage for MSVC. The symbols we're
// aliasing to are hiding in MallocImpl.cpp
#if defined(_M_IX86)
#pragma comment(linker, "/alternatename:_mallocx=_mallocxWeak")
#pragma comment(linker, "/alternatename:_rallocx=_rallocxWeak")
#pragma comment(linker, "/alternatename:_xallocx=_xallocxWeak")
#pragma comment(linker, "/alternatename:_sallocx=_sallocxWeak")
#pragma comment(linker, "/alternatename:_dallocx=_dallocxWeak")
#pragma comment(linker, "/alternatename:_sdallocx=_sdallocxWeak")
#pragma comment(linker, "/alternatename:_nallocx=_nallocxWeak")
#pragma comment(linker, "/alternatename:_mallctl=_mallctlWeak")
#pragma comment( \
    linker, "/alternatename:_mallctlnametomib=_mallctlnametomibWeak")
#pragma comment(linker, "/alternatename:_mallctlbymib=_mallctlbymibWeak")
#pragma comment( \
    linker,      \
    "/alternatename:_MallocExtension_Internal_GetNumericProperty=_MallocExtension_Internal_GetNumericPropertyWeak")
#else
#pragma comment(linker, "/alternatename:mallocx=mallocxWeak")
#pragma comment(linker, "/alternatename:rallocx=rallocxWeak")
#pragma comment(linker, "/alternatename:xallocx=xallocxWeak")
#pragma comment(linker, "/alternatename:sallocx=sallocxWeak")
#pragma comment(linker, "/alternatename:dallocx=dallocxWeak")
#pragma comment(linker, "/alternatename:sdallocx=sdallocxWeak")
#pragma comment(linker, "/alternatename:nallocx=nallocxWeak")
#pragma comment(linker, "/alternatename:mallctl=mallctlWeak")
#pragma comment(linker, "/alternatename:mallctlnametomib=mallctlnametomibWeak")
#pragma comment(linker, "/alternatename:mallctlbymib=mallctlbymibWeak")
#pragma comment( \
    linker,      \
    "/alternatename:MallocExtension_Internal_GetNumericProperty=MallocExtension_Internal_GetNumericPropertyWeak")
#endif
#endif
#endif
}
