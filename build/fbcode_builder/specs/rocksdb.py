#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals


def fbcode_builder_spec(builder):
    builder.add_option(
        "rocksdb/_build:cmake_defines",
        {
            "USE_RTTI": "1",
            "PORTABLE": "ON",
        },
    )
    return {
        "steps": [
            builder.fb_github_cmake_install("rocksdb/_build"),
        ],
    }
