# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under both the MIT license found in the
# LICENSE-MIT file in the root directory of this source tree and the Apache
# License, Version 2.0 found in the LICENSE-APACHE file in the root directory
# of this source tree.

def translate_target(target: str) -> str:
    def remove_version(target: str) -> str:
        # When upgrading libraries we either suffix them as `-old` or with a version, e.g. `-1-08`
        # Strip those so we grab the right one in open source.
        if target.endswith(":md-5"):  # md-5 is the one exception
            return target
        xs = target.split("-")
        for i in reversed(range(len(xs))):
            s = xs[i]
            if s == "old" or s.isdigit():
                xs.pop(i)
            else:
                break
        return "-".join(xs)

    if target == "//common/rust/shed/fbinit:fbinit":
        return "fbsource//third-party/rust:fbinit"
    elif target == "//common/rust/shed/sorted_vector_map:sorted_vector_map":
        return "fbsource//third-party/rust:sorted_vector_map"
    elif target == "//watchman/rust/watchman_client:watchman_client":
        return "fbsource//third-party/rust:watchman_client"
    elif target.startswith("fbsource//third-party/rust:"):
        return remove_version(target)
    elif target.startswith(":"):
        return target
    elif target.startswith("//buck2/"):
        return "root//" + target.removeprefix("//buck2/")
    elif target.startswith("fbcode//common/ocaml/interop/"):
        return "root//" + target.removeprefix("fbcode//common/ocaml/interop/")
    elif target.startswith("fbcode//third-party-buck/platform010/build/supercaml"):
        return "shim//third-party/ocaml" + target.removeprefix("fbcode//third-party-buck/platform010/build/supercaml")
    elif target.startswith("fbcode//third-party-buck/platform010/build"):
        return "shim//third-party" + target.removeprefix("fbcode//third-party-buck/platform010/build")
    elif target.startswith("fbsource//third-party"):
        return "shim//third-party" + target.removeprefix("fbsource//third-party")
    elif target.startswith("third-party//"):
        return "shim//third-party/" + target.removeprefix("third-party//")
    elif target.startswith("//folly"):
        oss_depends_on_folly = read_config("oss_depends_on", "folly", False)
        if oss_depends_on_folly:
            return "root//folly/" + target.removeprefix("//")
        return "root//" + target.removeprefix("//")
    elif target.startswith("root//folly"):
        return target
    elif target.startswith("//fizz"):
        return "root//" + target.removeprefix("//")
    elif target.startswith("shim//"):
        return target
    elif target.startswith("prelude//"):
        return target
    else:
        fail("Dependency is unaccounted for `{}`.\n".format(target) +
             "Did you forget 'oss-disable'?")
