load("@fbcode_macros//build_defs:native_rules.bzl", "alias")

def alias_pem(pems: list[str]):
    for pem in pems:
        alias(
            name = pem,
            actual = "//folly/io/async/test/certs:{pem}".format(pem = pem),
        )

def alias_pem_for_xplat(pems: list[str]):
    # in xplat these pem files are exported in //xplat/folly/io/async/test
    for pem in pems:
        alias(
            name = pem,
            actual = "//xplat/folly/io/async/test:certs/{pem}".format(pem = pem),
        )
