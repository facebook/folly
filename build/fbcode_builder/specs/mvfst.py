#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.

import specs.fizz as fizz
import specs.folly as folly
import specs.gmock as gmock


def fbcode_builder_spec(builder):
    # Projects that **depend** on mvfst should don't need to build tests.
    builder.add_option(
        "mvfst/build:cmake_defines",
        {
            # This is set to ON in the mvfst `fbcode_builder_config.py`
            "BUILD_TESTS": "OFF"
        },
    )
    return {
        "depends_on": [gmock, folly, fizz],
        "steps": [
            builder.fb_github_cmake_install(
                "mvfst/build", github_org="facebookincubator"
            )
        ],
    }
