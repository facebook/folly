# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

load("@fbsource//tools/build_defs:buckconfig.bzl", "read_bool")

def roar_no_jit():
    use_roar_jit = read_bool("fbcode", "use_roar_jit", required = False)
    if use_roar_jit:
        return ["-fforce-no-jit"]
    return []
