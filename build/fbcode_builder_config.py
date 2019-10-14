#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

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
