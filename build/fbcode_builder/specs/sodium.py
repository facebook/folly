#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.

from shell_quoting import ShellQuoted


def fbcode_builder_spec(builder):
    builder.add_option("jedisct1/libsodium:git_hash", "stable")
    return {
        "steps": [
            builder.github_project_workdir("jedisct1/libsodium", "."),
            builder.step(
                "Build and install jedisct1/libsodium",
                [
                    builder.run(ShellQuoted("./autogen.sh")),
                    builder.configure(),
                    builder.make_and_install(),
                ],
            ),
        ],
    }
