#!/bin/sh
# Copyright (c) Facebook, Inc. and its affiliates.


# `daemon.json` is normally missing, but let's log it in case that changes.
touch /etc/docker/daemon.json
service docker stop
echo '{"ipv6": true, "fixed-cidr-v6": "2001:db8:1::/64"}' > /etc/docker/daemon.json
service docker start
# Fail early if docker failed on start -- add `- sudo dockerd` to debug.
docker info
# Paranoia log: what if our config got overwritten?
cat /etc/docker/daemon.json
