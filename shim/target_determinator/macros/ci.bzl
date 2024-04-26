# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

def _lbl(*_args):
    return ""

def _package(
        _values,
        # starlark-lint-disable unused-argument
        overwrite = False):  # @unused
    pass

ci = struct(
    package = _package,
    linux = _lbl,
    mac = _lbl,
    windows = _lbl,
    skip_test = _lbl,
    aarch64 = _lbl,
    mode = _lbl,
    opt = _lbl,
)
