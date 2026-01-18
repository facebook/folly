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

# NOTE: This file is manually maintained, not auto-generated.
# Update this file when the corresponding BUCK file changes.

# ARM-only optimized memory operations (aarch64 assembly)
# Only build on Linux aarch64, not Darwin arm64 - these assembly files use
# Linux ELF directives (.size, etc.) that Darwin's assembler doesn't support
if(IS_AARCH64_ARCH)

folly_add_library(
  NAME memcpy_aarch64
  SRCS
    memcpy-advsimd.S
    memcpy-armv8.S
    memcpy-mops.S
    memcpy_sve.S
    memmove-mops.S
  HEADERS
    asmdefs.h
)

folly_add_library(
  NAME memcpy_aarch64-use
  SRCS
    memcpy-advsimd.S
    memcpy-armv8.S
    memcpy-mops.S
    memcpy_sve.S
    memmove-mops.S
  HEADERS
    asmdefs.h
  COMPILE_OPTIONS
    -DFOLLY_MEMCPY_IS_MEMCPY
)

folly_add_library(
  NAME memset_aarch64
  SRCS
    memset-advsimd.S
    memset-mops.S
  HEADERS
    asmdefs.h
)

folly_add_library(
  NAME memset_aarch64-use
  SRCS
    memset-advsimd.S
    memset-mops.S
  HEADERS
    asmdefs.h
  COMPILE_OPTIONS
    -DFOLLY_MEMSET_IS_MEMSET
)

endif()
