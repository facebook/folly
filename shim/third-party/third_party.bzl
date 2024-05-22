# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

load("@prelude//third-party:pkgconfig.bzl", "external_pkgconfig_library")
load("@shim//build_defs:prebuilt_cpp_library.bzl", "prebuilt_cpp_library")

def homebrew_library(
        package_name,
        name = None,
        default_target_platform = "prelude//platforms:default",
        visibility = ["PUBLIC"],
        deps = None,
        header_path = None,
        linker_flags = None,
        labels = []):
    brew_headers = package_name + "__brew_headers"
    brew_libs = package_name + "__brew_libs"
    if name != None:
        brew_headers = name + "__" + brew_headers
        brew_libs = name + "__" + brew_libs

    # @lint-ignore BUCKLINT
    native.genrule(
        name = brew_headers,
        default_target_platform = default_target_platform,
        out = "out",
        cmd = "echo \"-I`brew --prefix {}`/{}\" > $OUT".format(package_name, header_path or "include"),
    )

    # @lint-ignore BUCKLINT
    native.genrule(
        name = brew_libs,
        default_target_platform = default_target_platform,
        out = "out",
        cmd = "echo \"-L`brew --prefix {}`/lib\" > $OUT".format(package_name),
    )

    linker_flags = linker_flags or []
    linker_flags.append("@$(location :{})".format(brew_libs))

    prebuilt_cpp_library(
        name = name or package_name,
        default_target_platform = default_target_platform,
        visibility = visibility,
        exported_deps = deps,
        exported_preprocessor_flags = ["@$(location :{})".format(brew_headers)],
        linker_flags = linker_flags,
        labels = labels,
    )

def third_party_library(name, visibility = ["PUBLIC"], deps = [], homebrew_package_name = None, pkgconfig_name = None, homebrew_header_path = None, default_target_platform = "prelude//platforms:default", homebrew_linker_flags = None):
    # Labels defined here are used to extract third-party libs so they can be installed:
    labels = []
    if homebrew_package_name != None:
        labels.append("third-party:homebrew:" + homebrew_package_name)

    if pkgconfig_name != None:
        external_pkgconfig_library(name = pkgconfig_name, visibility = visibility if name == pkgconfig_name else [], labels = labels, default_target_platform = default_target_platform, deps = deps)
        if name != pkgconfig_name:
            native.alias(name = name, actual = ":{}".format(pkgconfig_name), visibility = visibility)
        return
    homebrew_library(name = name, package_name = homebrew_package_name or name, visibility = visibility, deps = deps, header_path = homebrew_header_path, linker_flags = homebrew_linker_flags, default_target_platform = default_target_platform, labels = labels)
