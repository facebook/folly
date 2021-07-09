#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import specs.fizz as fizz
import specs.fmt as fmt
import specs.folly as folly
import specs.gmock as gmock
import specs.mvfst as mvfst
import specs.sodium as sodium
import specs.wangle as wangle
import specs.zstd as zstd

# DO NOT USE THIS AS A LIBRARY -- this is currently effectively just part
# ofthe implementation of proxygen's `fbcode_builder_config.py`.  This is
# why this builds tests and sets `BUILD_QUIC`.
def fbcode_builder_spec(builder):
    builder.add_option(
        "proxygen/proxygen:cmake_defines",
        {"BUILD_QUIC": "ON", "BUILD_SHARED_LIBS": "OFF", "BUILD_TESTS": "ON"},
    )
    return {
        "depends_on": [gmock, fmt, folly, wangle, fizz, sodium, zstd, mvfst],
        "steps": [builder.fb_github_cmake_install("proxygen/proxygen", "..")],
    }
