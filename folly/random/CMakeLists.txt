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

folly_add_library(
  NAME hash
  HEADERS
    hash.h
  EXPORTED_DEPS
    folly_functional_invoke
    folly_utility
)

folly_add_library(
  NAME xoshiro256pp
  HEADERS
    xoshiro256pp.h
  EXPORTED_DEPS
    folly_likely
    folly_portability
)
