load("@fbcode_macros//build_defs:native_rules.bzl", "buck_genrule")

oncall("fbcode_entropy_wardens_folly")

buck_genrule(
    name = "folly-config.h",
    srcs = {file: file for file in glob([
               "CMake/*",
               "build/fbcode_builder/CMake/*",
           ])} |
           {"CMakeLists.txt": "CMakeListsForBuck2.txt"},
    out = "folly-config.h",
    cmd = "cmake . && mv folly/folly-config.h $OUT",
    default_target_platform = "prelude//platforms:default",
    labels = [
        "third-party:fedora:cmake",
        "third-party:homebrew:cmake",
        "third-party:ubuntu:cmake",
    ],
    remote = False,
)
