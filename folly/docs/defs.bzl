load("@bazel_skylib//lib:paths.bzl", "paths")
load("@bazel_skylib//lib:shell.bzl", "shell")
load("@fbcode_macros//build_defs:custom_rule.bzl", "custom_rule")

#
# Helper functions to emit fbconfig rules
#

# Helper function to copy a file to the output directory
def copy(path):
    custom_rule(
        name = path,
        srcs = [path],
        build_args = shell.quote(path),
        build_script_dep = "//folly/docs/facebook:copy.py",
        output_gen_files = [path],
        strict = False,  # Remove (https://fburl.com/strict-custom-rules)
    )

# Helper function to define a custom_rule() that will emit the HTML output.
def html(src, support = None, style = "style.css", copy_support = True):
    html = paths.split_extension(src)[0] + ".html"

    if support == None:
        support = []

    custom_rule(
        name = html,
        srcs = [src, style] + support,
        build_args =
            "--style %s %s" % (shell.quote(style), shell.quote(src)) +
            " --pandoc-path $(exe fbsource//third-party/stackage-lts:pandoc)",
        build_script_dep = "//folly/docs/facebook:build_html.py",
        output_gen_files = [html],
        strict = False,  # Remove (https://fburl.com/strict-custom-rules)
    )

    if copy_support:
        for path in support:
            copy(path)
