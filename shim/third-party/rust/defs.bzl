def rust_library_from_crates(name):
    # @lint-ignore BUCKLINT: avoid "Direct usage of native rules is not allowed."
    native.export_file(name = name, src = "BUCK", visibility = ["PUBLIC"])

def rust_binary_from_crates(name):
    # @lint-ignore BUCKLINT: avoid "Direct usage of native rules is not allowed."
    native.genrule(name = name, cmd = "exit 1", executable = True, out = "out", visibility = ["PUBLIC"])
