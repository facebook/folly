load("@fbcode_macros//build_defs:native_rules.bzl", "alias")

def alias_pem(pems: list[str]):
    # The folly root is `folly` in fbcode and OSS, and `xplat/folly` in fbsource.
    package = native.package_name()
    folly_root = package[: package.find("folly/") + len("folly")]
    for pem in pems:
        alias(
            name = pem,
            actual = "//{}/io/async/test/certs:{}".format(folly_root, pem),
        )
