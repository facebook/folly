#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

from shell_quoting import path_join
import specs.fbthrift as fbthrift


def fbcode_builder_spec(builder):
    builder.enable_rust_toolchain()
    return {
        "depends_on": [fbthrift],
        "steps": [
            builder.set_env(
                "THRIFT", path_join(builder.option("prefix"), "bin", "thrift1")
            ),
            builder.fb_github_cargo_build(
                "rust-shed/", github_org="facebookexperimental"
            ),
        ],
    }
