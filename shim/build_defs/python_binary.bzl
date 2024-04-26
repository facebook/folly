# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

def python_binary(srcs = [], **kwargs):
    _unused = srcs  # @unused

    # @lint-ignore BUCKLINT: avoid "Direct usage of native rules is not allowed."
    native.python_binary(**kwargs)
