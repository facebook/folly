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

#include <tuple>

namespace folly {
namespace channels {
namespace detail {

template <typename Func>
struct FunctionTraits {};

template <typename ClassType, typename RetType, typename... ArgTypes>
struct FunctionTraits<RetType (ClassType::*)(ArgTypes...) const> {
  using Args = std::tuple<ArgTypes...>;
  using Return = RetType;
  using Class = ClassType;
  using Self = const ClassType&;
};

template <typename ClassType, typename RetType, typename... ArgTypes>
struct FunctionTraits<RetType (ClassType::*)(ArgTypes...)> {
  using Args = std::tuple<ArgTypes...>;
  using Return = RetType;
  using Class = ClassType;
  using Self = ClassType&;
};

template <typename ClassType, typename RetType, typename... ArgTypes>
struct FunctionTraits<RetType (ClassType::*)(ArgTypes...) const&> {
  using Args = std::tuple<ArgTypes...>;
  using Return = RetType;
  using Class = ClassType;
  using Self = const ClassType&;
};

template <typename ClassType, typename RetType, typename... ArgTypes>
struct FunctionTraits<RetType (ClassType::*)(ArgTypes...)&> {
  using Args = std::tuple<ArgTypes...>;
  using Return = RetType;
  using Class = ClassType;
  using Self = ClassType&;
};

template <typename ClassType, typename RetType, typename... ArgTypes>
struct FunctionTraits<RetType (ClassType::*)(ArgTypes...) const&&> {
  using Args = std::tuple<ArgTypes...>;
  using Return = RetType;
  using Class = ClassType;
  using Self = const ClassType&&;
};

template <typename ClassType, typename RetType, typename... ArgTypes>
struct FunctionTraits<RetType (ClassType::*)(ArgTypes...) &&> {
  using Args = std::tuple<ArgTypes...>;
  using Return = RetType;
  using Class = ClassType;
  using Self = ClassType&&;
};

template <typename RetType, typename... ArgTypes>
struct FunctionTraits<RetType (*)(ArgTypes...)> {
  using Args = std::tuple<ArgTypes...>;
  using Return = RetType;
};
} // namespace detail
} // namespace channels
} // namespace folly
