fn main() {
    let libfmt = pkg_config::probe_library("fmt")
        .expect("Couldn’t find fmt via pkg-config");
    let libfolly = pkg_config::probe_library("libfolly")
        .expect("Couldn’t find folly via pkg-config");

    cxx_build::bridge("src/lib.rs")
        .file("../iobuf_sys/iobuf.cpp")
        .include("../../..")
        .includes(&libfmt.include_paths)
        .includes(&libfolly.include_paths)
        .compile("iobuf");
}
