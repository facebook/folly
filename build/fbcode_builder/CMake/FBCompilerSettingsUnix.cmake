# Copyright (c) Facebook, Inc. and its affiliates.

# This file applies common compiler settings that are shared across
# a number of Facebook opensource projects.
# Please use caution and your best judgement before making changes
# to these shared compiler settings in order to avoid accidentally
# breaking a build in another project!

set(CMAKE_CXX_FLAGS_COMMON "-g -Wall -Wextra -Wno-deprecated -Wno-deprecated-declarations")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${CMAKE_CXX_FLAGS_COMMON}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_CXX_FLAGS_COMMON} -O3")
