# Copyright (c) Facebook, Inc. and its affiliates.

# This file applies common compiler settings that are shared across
# a number of Facebook opensource projects.
# Please use caution and your best judgement before making changes
# to these shared compiler settings in order to avoid accidentally
# breaking a build in another project!

add_compile_options(
  /wd4250 # 'class1' : inherits 'class2::member' via dominance
)
