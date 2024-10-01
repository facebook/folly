# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

load("@prelude//third-party:pkgconfig.bzl", "external_pkgconfig_library")

HOMEBREW_CONSTRAINT = "//os:macos-homebrew"

def system_library(
        name: str,
        packages = None,
        visibility = ["PUBLIC"],
        deps = [],
        exported_deps = [],
        **kwargs):
    system_packages_target_name = "__{}_system_pkgs".format(name)
    packages = packages or dict()
    packages["DEFAULT"] = []
    system_packages(
        name = system_packages_target_name,
        packages = select(packages),
    )
    deps = deps + [":" + system_packages_target_name]

    brews = packages.get(HOMEBREW_CONSTRAINT)
    if brews != None:
        exported_deps = exported_deps + select({
            HOMEBREW_CONSTRAINT: _system_homebrew_targets(name, brews),
            "DEFAULT": [],
        })

    native.prebuilt_cxx_library(
        name = name,
        visibility = visibility,
        deps = deps,
        exported_deps = exported_deps,
        **kwargs
    )

def pkgconfig_system_library(
        name: str,
        pkgconfig_name = None,
        packages = None,
        visibility = ["PUBLIC"],
        deps = [],
        exported_deps = [],
        unsupported = dict(),
        **kwargs):
    system_packages_target_name = "__{}_system_pkgs".format(name)
    packages = packages or dict()
    packages["DEFAULT"] = []
    system_packages(
        name = system_packages_target_name,
        packages = select(packages),
    )

    deps = exported_deps + deps

    if len(unsupported) == 0:
        external_pkgconfig_library(
            name = name,
            package = pkgconfig_name,
            visibility = visibility,
            deps = deps + [":" + system_packages_target_name],
            **kwargs
        )
    else:
        exported_deps_select_map = {}
        for constraint, constraint_exported_deps in unsupported.items():
            if constraint == HOMEBREW_CONSTRAINT:
                brews = packages.get(constraint, [])
                constraint_exported_deps = constraint_exported_deps + _system_homebrew_targets(name, brews)

            exported_deps_select_map[constraint] = constraint_exported_deps

        pkgconfig_target_name = "__{}_pkgconfig".format(name)
        external_pkgconfig_library(
            name = pkgconfig_target_name,
            package = pkgconfig_name,
            visibility = [],
            deps = deps,
            **kwargs
        )
        exported_deps_select_map["DEFAULT"] = [":" + pkgconfig_target_name]

        native.prebuilt_cxx_library(
            name = name,
            visibility = visibility,
            deps = [":" + system_packages_target_name],
            exported_deps = select(exported_deps_select_map),
        )

def _system_homebrew_targets(
        name: str,
        brews):
    deps = []
    for brew in brews:
        homebrew_target_name = "__{}_homebrew_{}".format(name, brew)
        homebrew_library(
            name = homebrew_target_name,
            brew = brew,
        )
        deps.append(":" + homebrew_target_name)

    return deps

def _system_packages_impl(ctx: AnalysisContext) -> list[Provider]:
    return [DefaultInfo()]

system_packages = rule(
    impl = lambda _ctx: [DefaultInfo()],
    attrs = {
        "deps": attrs.list(attrs.dep(), default = []),
        "packages": attrs.list(attrs.string()),
    },
)

def homebrew_library(
        name: str,
        brew: str,
        homebrew_header_path = "include",
        exported_preprocessor_flags = [],
        exported_linker_flags = [],
        target_compatible_with = ["//os:macos-homebrew"],
        **kwargs):
    preproc_flags_rule_name = "__{}__{}__preproc_flags".format(name, brew)
    native.genrule(
        name = preproc_flags_rule_name,
        type = "homebrew_library_preproc_flags",
        out = "out",
        cmd = "echo \"-I`brew --prefix {}`/{}\" > $OUT".format(brew, homebrew_header_path),
        target_compatible_with = target_compatible_with,
    )

    linker_flags_rule_name = "__{}__{}__linker_flags".format(name, brew)
    native.genrule(
        name = linker_flags_rule_name,
        type = "homebrew_library_linker_flags",
        out = "out",
        cmd = "echo \"-L`brew --prefix {}`/lib\" > $OUT".format(brew),
        target_compatible_with = target_compatible_with,
    )

    native.prebuilt_cxx_library(
        name = name,
        exported_preprocessor_flags = exported_preprocessor_flags + [
            "@$(location :{})/preproc_flags.txt".format(preproc_flags_rule_name),
        ],
        exported_linker_flags = exported_linker_flags + [
            "@$(location :{})/linker_flags.txt".format(linker_flags_rule_name),
        ],
        target_compatible_with = target_compatible_with,
        **kwargs
    )
