# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

load("@shim//third-party:third_party.bzl", "third_party_library")

def boost_libs(xs):
    third_party_library(
        name = "boost",
        repo_package_names = {
            "fedora": "boost-devel",
            "homebrew": "boost",
            "ubuntu": "libboost-all-dev",
        },
    )
    for x in xs:
        third_party_library(
            name = "boost_{}".format(x),
            repo_package_names = {
                "fedora": "boost-devel",
                "homebrew": "boost",
                "ubuntu": "libboost-all-dev",
            },
            linker_flags = ["-lboost_{}".format(x)],
        )

def boost_header_only(xs):
    for x in xs:
        third_party_library(
            name = "boost_{}".format(x),
            repo_package_names = {
                "fedora": "boost-devel",
                "homebrew": "boost",
                "ubuntu": "libboost-all-dev",
            },
        )
