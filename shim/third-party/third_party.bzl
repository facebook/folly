# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

load("@prelude//third-party:pkgconfig.bzl", "external_pkgconfig_library")
load("@shim//build_defs:prebuilt_cpp_library.bzl", "prebuilt_cpp_library")

def third_party_library(
        name,
        visibility = ["PUBLIC"],
        deps = [],
        pkgconfig_name = None,
        repo_package_names = {},
        linker_flags = None,
        homebrew_header_path = None,
        pkgconfig_fallback = None):
    # Labels defined here are used to extract third-party libs so they can be installed
    labels = []
    for repo, package_name in repo_package_names.items():
        labels.append("third-party:{}:{}".format(repo, package_name))

    # Prefer pkgconfig
    if pkgconfig_name != None:
        for repo in repo_package_names.keys():
            labels.append("third-party:{}:pkg-config".format(repo))

        external_pkgconfig_library(
            name = pkgconfig_name,
            visibility = visibility if name == pkgconfig_name else [],
            labels = labels,
            deps = deps,
            fallback = pkgconfig_fallback,
        )
        if name != pkgconfig_name:
            native.alias(
                name = name,
                actual = ":{}".format(pkgconfig_name),
                visibility = visibility,
            )
    else:
        linker_flags = linker_flags or []
        exported_preprocessor_flags = []

        os = host_info().os
        if os.is_macos and "homebrew" in repo_package_names:
            # Add brew lookup paths
            homebrew_package_name = repo_package_names["homebrew"]
            linker_flags += _homebrew_linker_flags(
                name = name,
                homebrew_package_name = homebrew_package_name,
            )
            exported_preprocessor_flags += _homebrew_preprocessor_flags(
                name = name,
                homebrew_package_name = homebrew_package_name,
                homebrew_header_path = homebrew_header_path,
            )

        prebuilt_cpp_library(
            name = name,
            visibility = visibility,
            exported_deps = deps,
            exported_preprocessor_flags = exported_preprocessor_flags,
            linker_flags = linker_flags,
            labels = labels,
        )

def _homebrew_linker_flags(name, homebrew_package_name):
    homebrew_libs = "{}__{}__homebrew_libs".format(name, homebrew_package_name)

    # @lint-ignore BUCKLINT
    native.genrule(
        name = homebrew_libs,
        out = "out",
        cmd = "echo \"-L`brew --prefix {}`/lib\" > $OUT".format(homebrew_package_name),
    )

    return ["@$(location :{})".format(homebrew_libs)]

def _homebrew_preprocessor_flags(name, homebrew_package_name, homebrew_header_path):
    homebrew_headers = "{}__{}__homebrew_headers".format(name, homebrew_package_name)

    # @lint-ignore BUCKLINT
    native.genrule(
        name = homebrew_headers,
        out = "out",
        cmd = "echo \"-I`brew --prefix {}`/{}\" > $OUT".format(
            homebrew_package_name,
            homebrew_header_path or "include",
        ),
    )

    return ["@$(location :{})".format(homebrew_headers)]
