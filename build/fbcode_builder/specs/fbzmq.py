#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import specs.fbthrift as fbthrift
import specs.fmt as fmt
import specs.folly as folly
import specs.gmock as gmock
import specs.sodium as sodium
from shell_quoting import ShellQuoted


def fbcode_builder_spec(builder):
    builder.add_option("zeromq/libzmq:git_hash", "v4.2.2")
    return {
        "depends_on": [fmt, folly, fbthrift, gmock, sodium],
        "steps": [
            builder.github_project_workdir("zeromq/libzmq", "."),
            builder.step(
                "Build and install zeromq/libzmq",
                [
                    builder.run(ShellQuoted("./autogen.sh")),
                    builder.configure(),
                    builder.make_and_install(),
                ],
            ),
            builder.fb_github_project_workdir("fbzmq/_build", "facebook"),
            builder.step(
                "Build and install fbzmq/",
                [
                    builder.cmake_configure("fbzmq/_build"),
                    # we need the pythonpath to find the thrift compiler
                    builder.run(
                        ShellQuoted(
                            'PYTHONPATH="$PYTHONPATH:"{p}/lib/python2.7/site-packages '
                            "make -j {n}"
                        ).format(
                            p=builder.option("prefix"),
                            n=builder.option("make_parallelism"),
                        )
                    ),
                    builder.run(ShellQuoted("make install")),
                ],
            ),
        ],
    }
