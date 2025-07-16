use bindgen::callbacks::{MacroParsingBehavior, ParseCallbacks};
use std::collections::HashSet;
use std::env;
use std::path::PathBuf;

// Workaround for https://github.com/rust-lang/rust-bindgen/issues/687
const IGNORE_MACROS
// : [&str; 20] = [
: [&str; 10] = [
    // "FE_DIVBYZERO",
    // "FE_DOWNWARD",
    // "FE_INEXACT",
    // "FE_INVALID",
    // "FE_OVERFLOW",
    // "FE_TONEAREST",
    // "FE_TOWARDZERO",
    // "FE_UNDERFLOW",
    // "FE_UPWARD",
    "FP_INFINITE",
    "FP_INT_DOWNWARD",
    "FP_INT_TONEAREST",
    "FP_INT_TONEARESTFROMZERO",
    "FP_INT_TOWARDZERO",
    "FP_INT_UPWARD",
    "FP_NAN",
    "FP_NORMAL",
    "FP_SUBNORMAL",
    "FP_ZERO",
    // "IPPORT_RESERVED",
];

#[derive(Debug)]
struct IgnoreMacros(HashSet<String>);

impl ParseCallbacks for IgnoreMacros {
    fn will_parse_macro(&self, name: &str) -> MacroParsingBehavior {
        if self.0.contains(name) {
            MacroParsingBehavior::Ignore
        } else {
            MacroParsingBehavior::Default
        }
    }
}

impl IgnoreMacros {
    fn new() -> Self {
        Self(IGNORE_MACROS
            .into_iter().map(|s| s.to_owned()).collect())
    }
}

fn main() {
    let gflags = pkg_config::probe_library("gflags")
        .expect("Couldn’t find fmt via gflags");
    let libfmt = pkg_config::probe_library("fmt")
        .expect("Couldn’t find fmt via pkg-config");
    let libfolly = pkg_config::probe_library("libfolly")
        .expect("Couldn’t find folly via pkg-config");
    let libglog = pkg_config::probe_library("libglog")
        .expect("Couldn’t find glog via pkg-config");

    let bindings = bindgen::Builder::default()
        .header("iobuf.h")
        .with_codegen_config(
            bindgen::CodegenConfig::TYPES |
            bindgen::CodegenConfig::FUNCTIONS | 
            bindgen::CodegenConfig::VARS
        )
        .clang_arg("-x")
        .clang_arg("c++")
        .clang_arg("-std=c++17")
        .enable_cxx_namespaces()

        .allowlist_function("facebook::rust::.*")
        .allowlist_type("folly::IOBuf")
        .allowlist_type("facebook::rust::.*")
        .allowlist_var("facebook::rust::.*")
        .blocklist_type("folly::fbvector.*")
        .blocklist_type("__type")
        .blocklist_type("type_")
        .opaque_type("(::)?std::.*")
        .opaque_type("folly::fbstring.*")

        .clang_arg("-I../../..")
        .clang_args(gflags.include_paths.iter().map(|p| format!("-I{}", p.display())))
        .clang_args(libfmt.include_paths.iter().map(|p| format!("-I{}", p.display())))
        .clang_args(libfolly.include_paths.iter().map(|p| format!("-I{}", p.display())))
        .clang_args(libglog.include_paths.iter().map(|p| format!("-I{}", p.display())))

        .generate()
        .expect("Unable to generate bindings");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    let out_file = out_path.join("bindings.rs");
    std::fs::create_dir_all(&out_path)
        .expect("Couldn't create output directory for bindings");
    bindings
        .write_to_file(&out_file)
        .expect("Couldn't write bindings!");
}
