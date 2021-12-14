#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import specs.fmt as fmt
import specs.folly as folly
import specs.gmock as gmock
import specs.sodium as sodium
import specs.zstd as zstd


def fbcode_builder_spec(builder):
    builder.add_option(
        "fizz/fizz/build:cmake_defines",
        {
            # Fizz's build is kind of broken, in the sense that both `mvfst`
            # and `proxygen` depend on files that are only installed with
            # `BUILD_TESTS` enabled, e.g. `fizz/crypto/test/TestUtil.h`.
            "BUILD_TESTS": "ON"
        },
    )
    return {
        "depends_on": [gmock, fmt, folly, sodium, zstd],
        "steps": [
            builder.fb_github_cmake_install(
                "fizz/fizz/build", github_org="facebookincubator"
            )
        ],
    }
