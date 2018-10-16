#pragma once
#ifndef PUSHMI_SINGLE_HEADER
#define PUSHMI_SINGLE_HEADER

// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <cstddef>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <exception>
#include <functional>
#include <algorithm>
#include <utility>
#include <memory>
#include <type_traits>
#include <initializer_list>

#include <thread>
#include <future>
#include <tuple>
#include <deque>
#include <vector>

#if __cpp_lib_optional >= 201606
#include <optional>
#endif
