# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

load("@//third-party:defs.bzl", "pkgconfig_system_library", "system_library")

REPO_TO_CONSTRAINT_MAP = {
    "fedora": "//os:linux-fedora",
    "homebrew": "//os:macos-homebrew",
    "ubuntu": "//os:linux-ubuntu",
}

def third_party_library(
        name: str,
        visibility = ["PUBLIC"],
        deps = [],
        pkgconfig_name = None,
        repo_package_names = {},
        **kwargs):
    packages = {
        REPO_TO_CONSTRAINT_MAP[repo]: [pkg]
        for repo, pkg in repo_package_names.items()
    }

    if pkgconfig_name == None:
        system_library(
            name = name,
            packages = packages,
            visibility = visibility,
            exported_deps = deps,
            **kwargs
        )
    else:
        pkgconfig_system_library(
            name = name,
            pkgconfig_name = pkgconfig_name,
            packages = packages,
            visibility = visibility,
            exported_deps = deps,
            **kwargs
        )
