# Copyright (c) Facebook, Inc. and its affiliates.
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

# Find the ibverbs libraries
#
# The following variables are optionally searched for defaults
#  IBVERBS_ROOT_DIR: Base directory where all ibverbs components are found
#  IBVERBS_INCLUDE_DIR: Directory where ibverbs headers are found
#  IBVERBS_LIB_DIR: Directory where ibverbs libraries are found

# The following are set after configuration is done:
#  IBVERBS_FOUND
#  IBVERBS_INCLUDE_DIRS
#  IBVERBS_LIBRARIES

find_path(IBVERBS_INCLUDE_DIRS
  NAMES infiniband/verbs.h
  HINTS
  ${IBVERBS_INCLUDE_DIR}
  ${IBVERBS_ROOT_DIR}
  ${IBVERBS_ROOT_DIR}/include)

find_library(IBVERBS_LIBRARIES
  NAMES ibverbs
  HINTS
  ${IBVERBS_LIB_DIR}
  ${IBVERBS_ROOT_DIR}
  ${IBVERBS_ROOT_DIR}/lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ibverbs DEFAULT_MSG IBVERBS_INCLUDE_DIRS IBVERBS_LIBRARIES)
mark_as_advanced(IBVERBS_INCLUDE_DIRS IBVERBS_LIBRARIES)
