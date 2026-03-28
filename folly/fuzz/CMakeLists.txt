# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# CMake build file for Folly fuzz targets.
#
# To build locally (requires a Clang toolchain):
#
#   cmake -S . -B _build \
#       -DCMAKE_C_COMPILER=clang \
#       -DCMAKE_CXX_COMPILER=clang++ \
#       -DBUILD_FUZZ_TARGETS=ON \
#       -DLIB_FUZZING_ENGINE="-fsanitize=fuzzer" \
#       -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
#   cmake --build _build --target json_fuzzer uri_fuzzer ipaddress_fuzzer conv_fuzzer
#
# For OSS-Fuzz the build is driven by projects/folly/build.sh, which sets
# LIB_FUZZING_ENGINE to the engine library provided by the OSS-Fuzz harness
# (e.g. -fsanitize=fuzzer, or a path to a static driver library).

# Default to the libFuzzer engine when building without the OSS-Fuzz harness.
if(NOT DEFINED LIB_FUZZING_ENGINE)
  set(LIB_FUZZING_ENGINE
    "-fsanitize=fuzzer"
    CACHE STRING
    "Fuzzing-engine flags or library path passed to the compiler and linker. \
Common values: '-fsanitize=fuzzer' (libFuzzer) or a path like \
'/usr/lib/libFuzzingEngine.a' (AFL++ driver).")
endif()

# ---------------------------------------------------------------------------
# Helper to register a single fuzz target.
# ---------------------------------------------------------------------------
function(folly_fuzz_target name)
  add_executable(${name} ${name}.cc)

  # Folly requires C++20.
  target_compile_features(${name} PRIVATE cxx_std_20)

  # Link against the monolithic folly library.
  target_link_libraries(${name} PRIVATE folly)

  # Apply the same compiler flags as the rest of folly (warnings, defines …).
  apply_folly_compile_options_to_target(${name})

  # Wire in the fuzzing engine.
  # LIB_FUZZING_ENGINE may be:
  #   a) a compiler/linker flag (e.g. -fsanitize=fuzzer) — must be passed as a
  #      compile option AND a link option so that clang instruments the binary.
  #   b) a path to a static library (e.g. /usr/lib/libFuzzingEngine.a) — link
  #      as a plain library.
  string(STRIP "${LIB_FUZZING_ENGINE}" _lfe_stripped)
  if(_lfe_stripped MATCHES "^-")
    target_compile_options(${name} PRIVATE ${_lfe_stripped})
    target_link_options(${name} PRIVATE ${_lfe_stripped})
  else()
    target_link_libraries(${name} PRIVATE ${_lfe_stripped})
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Fuzz targets
# ---------------------------------------------------------------------------

# JSON parser / serializer (folly::parseJson, folly::toJson)
folly_fuzz_target(json_fuzzer)

# URI parser (folly::Uri)
folly_fuzz_target(uri_fuzzer)

# IP address parser (folly::IPAddress, CIDR notation)
folly_fuzz_target(ipaddress_fuzzer)

# String-to-scalar conversions (folly::tryTo<T>)
folly_fuzz_target(conv_fuzzer)
