#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.


def fbcode_builder_spec(builder):
    return {
        "steps": [
            builder.github_project_workdir("google/re2", "build"),
            builder.cmake_install("google/re2"),
        ],
    }
