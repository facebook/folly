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

# x86 CRC32 implementations - built on all platforms
# Source files provide abort() stubs on non-x86 via #if guards
folly_add_library(
  NAME avx512_crc32c_v8s3x4
  SRCS
    avx512_crc32c_v8s3x4.cpp
  HEADERS
    avx512_crc32c_v8s3x4.h
  EXPORTED_DEPS
    folly_portability
)

# Apply AVX512 flags only on x86 (not needed on Windows)
if (IS_X86_64_ARCH AND NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
  target_compile_options(folly_external_fast-crc32_avx512_crc32c_v8s3x4_obj
    PRIVATE -mavx512f -mavx512vl)
endif()
if (COMPILER_HAS_M_PCLMUL)
  target_compile_options(folly_external_fast-crc32_avx512_crc32c_v8s3x4_obj
    PRIVATE -mpclmul)
endif()

folly_add_library(
  NAME sse_crc32c_v8s3x3
  SRCS
    sse_crc32c_v8s3x3.cpp
  HEADERS
    sse_crc32c_v8s3x3.h
  EXPORTED_DEPS
    folly_portability
)

if (COMPILER_HAS_M_PCLMUL)
  target_compile_options(folly_external_fast-crc32_sse_crc32c_v8s3x3_obj
    PRIVATE -mpclmul)
endif()

# ARM NEON CRC32 implementations - built on all platforms
# Source files provide abort() stubs on non-ARM via #if guards
folly_add_library(
  NAME neon_crc32c_v3s4x2e_v2
  SRCS
    neon_crc32c_v3s4x2e_v2.cpp
  HEADERS
    neon_crc32c_v3s4x2e_v2.h
  DEPS
    folly_portability
    folly_system_aux_vector
)

folly_add_library(
  NAME neon_eor3_crc32c_v8s2x4_s3
  SRCS
    neon_eor3_crc32c_v8s2x4_s3.cpp
  HEADERS
    neon_eor3_crc32c_v8s2x4_s3.h
  DEPS
    folly_portability
    folly_system_aux_vector
)

folly_add_library(
  NAME neon_eor3_crc32_v9s3x2e_s3
  SRCS
    neon_eor3_crc32_v9s3x2e_s3.cpp
  HEADERS
    neon_eor3_crc32_v9s3x2e_s3.h
  DEPS
    folly_portability
    folly_system_aux_vector
)
