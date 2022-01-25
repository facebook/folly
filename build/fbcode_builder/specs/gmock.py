#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.


def fbcode_builder_spec(builder):
    builder.add_option("google/googletest:git_hash", "release-1.8.1")
    builder.add_option(
        "google/googletest:cmake_defines",
        {
            "BUILD_GTEST": "ON",
            # Avoid problems with MACOSX_RPATH
            "BUILD_SHARED_LIBS": "OFF",
        },
    )
    return {
        "steps": [
            builder.github_project_workdir("google/googletest", "build"),
            builder.cmake_install("google/googletest"),
        ],
    }
