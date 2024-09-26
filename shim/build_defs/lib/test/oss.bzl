# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

load("@shim//build_defs/lib:oss.bzl", "translate_target")

TEST_CTX = struct(
    cells = struct(
        active = "root",
        root = "root",
        shim = "shim",
        internal = "internal",
    ),
    project_dirs = ["project"],
    stripped_root_dirs = ["root_dir"],
    prefix_mappings = [
        struct(
            match = struct(cell = "internal", root_dir = "dep"),
            replace = struct(cell = "dep", root_dir = "dep_rename"),
        ),
    ],
    implicit_rewrite_rules = {
        "internal": struct(
            exact = {
                "exact:exact": "foo/shimmed:exact",
            },
            dirs = [
                ("third-party", "third_party"),
            ],
            dynamic = [
                ("dynamic", lambda path: path.upper()),
            ],
        ),
    },
)

def _test_target(target: str, expected: str):
    actual = translate_target(target, TEST_CTX)

    if actual != expected:
        fail("Expected {} == {}".format(actual, expected))

def test_translate_target():
    _test_target("//:foo", "//:foo")
    _test_target("root//:foo", "root//:foo")
    _test_target("other//:foo", "other//:foo")

    _test_target("//project/foo:bar", "root//project/foo:bar")
    _test_target("internal//project/foo:bar", "root//project/foo:bar")
    _test_target("internal//project2/foo:bar", "internal//project2/foo:bar")

    _test_target("//root_dir/foo:bar", "root//foo:bar")
    _test_target("//root_dir/with/subdir/foo:bar", "root//with/subdir/foo:bar")
    _test_target("internal//root_dir/foo:bar", "root//foo:bar")

    _test_target("//dep:foo", "dep//dep_rename:foo")
    _test_target("//dep/with/subdir:foo", "dep//dep_rename/with/subdir:foo")
    _test_target("internal//dep:foo", "dep//dep_rename:foo")
    _test_target("other//dep:foo", "other//dep:foo")

    _test_target("//exact:exact", "shim//foo/shimmed:exact")
    _test_target("internal//exact:exact", "shim//foo/shimmed:exact")
    _test_target("other//exact:exact", "other//exact:exact")

    _test_target("//third-party/lib/foo:bar", "shim//third_party/lib/foo:bar")
    _test_target("internal//third-party/lib/foo:bar", "shim//third_party/lib/foo:bar")

    _test_target("//dynamic:foo", "shim//DYNAMIC:FOO")
    _test_target("internal//dynamic:foo", "shim//DYNAMIC:FOO")
    _test_target("other//dynamic:foo", "other//dynamic:foo")
