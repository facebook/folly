# Easy builds for Facebook projects

This directory contains tools designed to simplify continuous-integration
(and other builds) of Facebook open source projects.  In particular, this helps
manage builds for cross-project dependencies.

The main entry point is the `getdeps.py` script.  This script has several
subcommands, but the most notable is the `build` command.  This will download
and build all dependencies for a project, and then build the project itself.

## Deployment

This directory is copied literally into a number of different Facebook open
source repositories.  Any change made to code in this directory will be
automatically be replicated by our open source tooling into all GitHub hosted
repositories that use `fbcode_builder`.  Typically this directory is copied
into the open source repositories as `build/fbcode_builder/`.


# Project Configuration Files

The `manifests` subdirectory contains configuration files for many different
projects, describing how to build each project.  These files also list
dependencies between projects, enabling `getdeps.py` to build all dependencies
for a project before building the project itself.


# Shared CMake utilities

Since this directory is copied into many Facebook open source repositories,
it is also used to help share some CMake utility files across projects.  The
`CMake/` subdirectory contains a number of `.cmake` files that are shared by
the CMake-based build systems across several different projects.


# Older Build Scripts

This directory also still contains a handful of older build scripts that
pre-date the current `getdeps.py` build system.  Most of the other `.py` files
in this top directory, apart from `getdeps.py` itself, are from this older
build system.  This older system is only used by a few remaining projects, and
new projects should generally use the newer `getdeps.py` script, by adding a
new configuration file in the `manifests/` subdirectory.
