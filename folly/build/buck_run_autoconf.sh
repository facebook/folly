#!/bin/sh

get_ldflags() {
  # Convert paths to .so/.dylib/.dll into directories that can be used with
  # -L/path and -Wl,-rpath,/path compiler args. Also strip out -l flags,
  # as autoconf will add them for us for feature detection.
  for i in "$@"; do
    if [ "$i" = "-Wl,-rpath" ]; then continue; fi
    echo "$i" | perl -p -e "s/'/\\\\'/g;" \
      -e 's/-l[\w-]*//g;' \
      -e 's;(.*)[/\\].*\.(so|dylib|dll)$;-L\1 -Wl,-rpath,\1;g;'
  done
}

# This is an extra linker flag that buck appends on OSX that's not valid
# This probably requires a patch to buck
LDFLAGS=$(get_ldflags "$@" | uniq | tr '\n' ' ' | perl -pe 's;-Xlinker \@executable_path\S*;;g')

export LDFLAGS
export BOOST_LDFLAGS="$LDFLAGS"
export BOOST_CPPFLAGS="$CPPFLAGS"

SRCROOT=$(dirname "$(readlink "$SRCDIR"/configure.ac)")
# Temp dir because autoconf doesn't like '#' in the working directory
conf_dir=$(mktemp -d)
trap 'rm -rf "$conf_dir"' EXIT
cd "$conf_dir" && \
autoreconf -iv "$SRCROOT" && \
"$SRCROOT/configure" "$BOOST_FLAG" && \
cp -pvf folly-config.h "$OUT"
