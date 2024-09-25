# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

load("@shim//build_defs/lib:oss.bzl", "translate_target")

def _assert_eq(x, y):
    if x != y:
        fail("Expected {} == {}".format(x, y))

def test_translate_target():
    _assert_eq(translate_target("fbsource//third-party/rust:derive_more-1"), "fbsource//third-party/rust:derive_more")
