load("@prelude//toolchains:cxx.bzl", "system_cxx_toolchain")
load("@prelude//toolchains:genrule.bzl", "system_genrule_toolchain")
load("@prelude//toolchains:go.bzl", "system_go_toolchain")
load("@prelude//toolchains:haskell.bzl", "system_haskell_toolchain")
load("@prelude//toolchains:ocaml.bzl", "system_ocaml_toolchain")
load("@prelude//toolchains:python.bzl", "system_python_bootstrap_toolchain", "system_python_toolchain")
load("@prelude//toolchains:remote_test_execution.bzl", "remote_test_execution_toolchain")
load("@prelude//toolchains:rust.bzl", "system_rust_toolchain")

oncall("open_source")

system_cxx_toolchain(
    name = "cxx",
    cxx_flags = ["-std=c++20"],
    visibility = ["PUBLIC"],
)

system_genrule_toolchain(
    name = "genrule",
    visibility = ["PUBLIC"],
)

system_go_toolchain(
    name = "go",
    visibility = ["PUBLIC"],
)

system_haskell_toolchain(
    name = "haskell",
    visibility = ["PUBLIC"],
)

system_ocaml_toolchain(
    name = "ocaml",
    visibility = ["PUBLIC"],
)

system_python_toolchain(
    name = "python",
    visibility = ["PUBLIC"],
)

system_python_bootstrap_toolchain(
    name = "python_bootstrap",
    visibility = ["PUBLIC"],
)

system_rust_toolchain(
    name = "rust",
    default_edition = "2021",
    visibility = ["PUBLIC"],
)

remote_test_execution_toolchain(
    name = "remote_test_execution",
    visibility = ["PUBLIC"],
)
