#!/usr/bin/env python
from __future__ import absolute_import, division, print_function, unicode_literals

import specs.fmt as fmt
import specs.gmock as gmock
from shell_quoting import ShellQuoted


"fbcode_builder steps to build & test folly"


def fbcode_builder_spec(builder):
    builder.add_option(
        "folly/_build:cmake_defines", {"BUILD_SHARED_LIBS": "OFF", "BUILD_TESTS": "ON"}
    )
    return {
        "depends_on": [fmt, gmock],
        "steps": [
            builder.fb_github_cmake_install("folly/_build"),
            builder.step(
                "Run folly tests",
                [
                    builder.run(
                        ShellQuoted("ctest --output-on-failure -j {n}").format(
                            n=builder.option("make_parallelism")
                        )
                    )
                ],
            ),
        ],
    }


config = {
    "github_project": "facebook/folly",
    "fbcode_builder_spec": fbcode_builder_spec,
}
