#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.


def fbcode_builder_spec(builder):
    builder.add_option("fmtlib/fmt:git_hash", "6.2.1")
    builder.add_option(
        "fmtlib/fmt:cmake_defines",
        {
            # NB: May no longer be needed since Bistro is gone.
            "BUILD_SHARED_LIBS": "OFF",
        },
    )
    return {
        "steps": [
            builder.github_project_workdir("fmtlib/fmt", "build"),
            builder.cmake_install("fmtlib/fmt"),
        ],
    }
