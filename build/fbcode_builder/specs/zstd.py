#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.

from shell_quoting import ShellQuoted


def fbcode_builder_spec(builder):
    # This API should change rarely, so build the latest tag instead of master.
    builder.add_option(
        "facebook/zstd:git_hash",
        ShellQuoted("$(git describe --abbrev=0 --tags origin/master)"),
    )
    return {
        "steps": [
            builder.github_project_workdir("facebook/zstd", "."),
            builder.step(
                "Build and install zstd",
                [
                    builder.make_and_install(
                        make_vars={
                            "PREFIX": builder.option("prefix"),
                        }
                    )
                ],
            ),
        ],
    }
