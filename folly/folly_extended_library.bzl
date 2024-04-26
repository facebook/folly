load("@fbsource//tools/build_defs:default_platform_defs.bzl", "ANDROID", "APPLE", "CXX", "FBCODE", "WINDOWS")
load("@fbsource//xplat/folly:defs.bzl", "folly_library")

def folly_extended_library(name, **kwargs):
    folly_library(
        name,
        force_static = False,
        enable_static_variant = True,
        compiler_flags = [
            "-Wno-shadow",
        ],
        fbandroid_deps = [
            "fbsource//xplat/third-party/linker_lib:atomic",
            "fbsource//third-party/toolchains:log",
        ],
        linker_flags = select({
            "DEFAULT": [],
            "ovr_config//os:android": [
                "-Wl,--no-undefined",
            ],
        }),
        platforms = (CXX, ANDROID, APPLE, FBCODE, WINDOWS),
        xcode_public_headers_symlinks = True,
        deps = [
            "fbsource//xplat/folly:memory",
            "fbsource//third-party/glog:glog",
        ],
        **kwargs
    )
