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

#include <exception>
#include <typeinfo>

#include <folly/debugging/exception_tracer/Compatibility.h>

#if FOLLY_HAS_EXCEPTION_TRACER

namespace folly {
namespace exception_tracer {

using CxaThrowSig = void(void*, std::type_info*, void (**)(void*)) noexcept;
using CxaBeginCatchSig = void(void*) noexcept;
using CxaRethrowSig = void() noexcept;
using CxaEndCatchSig = void() noexcept;
using RethrowExceptionSig = void(std::exception_ptr) noexcept;

void registerCxaThrowCallback(CxaThrowSig& callback);
void registerCxaBeginCatchCallback(CxaBeginCatchSig& callback);
void registerCxaRethrowCallback(CxaRethrowSig& callback);
void registerCxaEndCatchCallback(CxaEndCatchSig& callback);
void registerRethrowExceptionCallback(RethrowExceptionSig& callback);
void unregisterCxaThrowCallback(CxaThrowSig& callback);
void unregisterCxaBeginCatchCallback(CxaBeginCatchSig& callback);
void unregisterCxaRethrowCallback(CxaRethrowSig& callback);
void unregisterCxaEndCatchCallback(CxaEndCatchSig& callback);
void unregisterRethrowExceptionCallback(RethrowExceptionSig& callback);

} // namespace exception_tracer
} // namespace folly

#endif //  FOLLY_HAS_EXCEPTION_TRACER
