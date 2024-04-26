load("@fbcode_macros//build_defs:native_rules.bzl", "alias")

def alias_pem(pems: list[str]):
    for pem in pems:
        alias(
            name = pem,
            actual = "//folly/io/async/test/certs:{pem}".format(pem = pem),
        )
