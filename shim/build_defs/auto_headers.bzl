load("@prelude//utils:buckconfig.bzl", "read_choice")

AutoHeaders = struct(
    NONE = "none",
    # Uses a recursive glob to resolve all transitive headers under the given
    # directory.
    RECURSIVE_GLOB = "recursive_glob",
    # Infer headers from sources of the rule.
    SOURCES = "sources",
)

_VALUES = [
    AutoHeaders.NONE,
    AutoHeaders.RECURSIVE_GLOB,
    AutoHeaders.SOURCES,
]

def get_auto_headers(auto_headers):
    """
    Returns the level of auto-headers to apply to a rule.

    Args:
        auto_headers: One of the values in `AutoHeaders`

    Returns:
        The value passed in as auto_headers, or the value from configuration if
        `auto_headers` is None
    """
    if auto_headers != None:
        if auto_headers not in _VALUES:
            fail("unexpected `auto_headers` value: {!r}".format(auto_headers))
        return auto_headers
    return read_choice("cxx", "auto_headers", _VALUES, AutoHeaders.SOURCES)
