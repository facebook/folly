#!/usr/bin/env python3
"""Query recent Github release artifacts for protobuf.

Use this script to update the releases.bzl file that contains metadata about
protoc releases.

This script is not executed during the regular Buck2 build.
"""

import aiohttp
import asyncio
from contextlib import asynccontextmanager
from copy import deepcopy
from gql import gql, Client
from gql.transport.aiohttp import AIOHTTPTransport
import hashlib
import json
import os

GITHUB_GRAPHQL_URI = "https://api.github.com/graphql"
GITHUB_TOKEN = os.environ.get("GITHUB_TOKEN")
GITHUB_QUERY = """\
query {
  repository(owner: "protocolbuffers", name: "protobuf") {
    releases(last: 1) {
      nodes {
        tagName
        releaseAssets(first: 100) {
          nodes {
            name
            downloadUrl
          }
        }
      }
    }
  }
}
"""

async def query_releases():
    async with aiohttp.ClientSession(raise_for_status=True) as session:
        assert GITHUB_TOKEN is not None, "Provide a Github API token in $GITHUB_TOKEN"
        headers = {'Authorization': f'bearer {GITHUB_TOKEN}'}
        body = {"query": GITHUB_QUERY}
        async with session.post(GITHUB_GRAPHQL_URI, headers=headers, json=body) as resp:
            response = await resp.json()
            return response["data"]


def format_releases(releases):
    return {
        release["tagName"].strip("v"): {
            asset["name"]: {
                "url": asset["downloadUrl"],
            }
            for asset in release["releaseAssets"]["nodes"]
            if asset["name"].startswith("protoc-")
        }
        for release in releases["repository"]["releases"]["nodes"]
    }


async def fetch_sha256(session, url):
    async with session.get(url) as resp:
        hasher = hashlib.sha256()
        async for chunk, _ in resp.content.iter_chunks():
            hasher.update(chunk)
    return hasher.hexdigest()


async def hash_releases(releases):
    async def hash_asset(session, version, name, url):
        sha256 = await fetch_sha256(session, url)
        return (version, name, sha256)

    tasks = []
    async with aiohttp.ClientSession() as session:
        for version, assets in releases.items():
            for name, asset in assets.items():
                tasks.append(hash_asset(session, version, name, asset["url"]))

        result = deepcopy(releases)
        hashes = await asyncio.gather(*tasks)
        for version, name, sha256 in hashes:
            result[version][name]["sha256"] = sha256

    return result


async def main():
    releases = await query_releases()
    formatted = format_releases(releases)
    with_sha256 = await hash_releases(formatted)
    print("# @" + "generated")
    print("# Update with ./update.py > releases.bzl")
    print("releases = ", json.dumps(with_sha256, indent=4))


asyncio.run(main())
