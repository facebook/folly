# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

load("@//third-party:defs.bzl", "system_library")

HOMEBREW_BREW = "boost"

def boost_libs(libraries, header_only):
    system_library(
        name = "boost",
        packages = {
            "//os:linux-fedora": ["boost-devel"],
            "//os:linux-ubuntu": ["libboost-all-dev"],
            "//os:macos-homebrew": ["boost"],
        },
    )

    for library in libraries:
        boost_library(library, False)

    for library in header_only:
        boost_library(library, True)

def boost_library(library: str, header_only: bool):
    exported_linker_flags = [] if header_only else ["-lboost_{}".format(library)]

    system_library(
        name = "boost_{}".format(library),
        packages = {
            "//os:linux-fedora": ["boost-devel"],
            "//os:linux-ubuntu": ["libboost-{}-dev".format(library)],
            "//os:macos-homebrew": ["boost"],
        },
        exported_linker_flags = exported_linker_flags,
    )
