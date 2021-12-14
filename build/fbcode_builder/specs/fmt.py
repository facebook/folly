#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals


def fbcode_builder_spec(builder):
    builder.add_option("fmtlib/fmt:git_hash", "6.2.1")
    builder.add_option(
        "fmtlib/fmt:cmake_defines",
        {
            # Avoids a bizarred failure to run tests in Bistro:
            #   test_crontab_selector: error while loading shared libraries:
            #   libfmt.so.6: cannot open shared object file:
            #   No such file or directory
            "BUILD_SHARED_LIBS": "OFF",
        },
    )
    return {
        "steps": [
            builder.github_project_workdir("fmtlib/fmt", "build"),
            builder.cmake_install("fmtlib/fmt"),
        ],
    }
