#!/usr/bin/env python
# Copyright (c) Facebook, Inc. and its affiliates.

"Demo config, so that `make_docker_context.py --help` works in this directory."

config = {
    "fbcode_builder_spec": lambda _builder: {
        "depends_on": [],
        "steps": [],
    },
    "github_project": "demo/project",
}
