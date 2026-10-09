#!/bin/sh
# Copyright (c) Meta Platforms, Inc.
# 
# Enhanced by <Madieyeee> — Adds caching for repeated dependency installs.
#
# This source code is licensed under the LICENSE file found in the root
# directory of this source tree.

set -e

# === CONFIGURATION ===
CACHE_FILE="$HOME/.folly_deps_cache"
PKG_FILE=$(mktemp /tmp/buck2-install-pkgs.XXXXXX)

# --- Argument parsing ---
AUTO_CONFIRM=false
CLEAR_CACHE=false
for arg in "$@"; do
  case "$arg" in
    --yes|-y) AUTO_CONFIRM=true ;;
    --clear-cache) CLEAR_CACHE=true ;;
  esac
done

if [ "$CLEAR_CACHE" = true ]; then
  echo "🧹 Clearing dependency cache..."
  rm -f "$CACHE_FILE"
  echo "✅ Cache cleared."
  exit 0
fi

# --- Platform detection ---
if [ -z "$INSTALL_COMMAND" ]; then
  if [ -f /etc/os-release ]; then
    . /etc/os-release;
  fi

  if command -v brew >/dev/null; then
    ID="homebrew";
  fi

  if [ -f "$BUCK_DEFAULT_RUNTIME_RESOURCES/repos/$ID" ]; then
    # shellcheck disable=SC1090
    . "$BUCK_DEFAULT_RUNTIME_RESOURCES/repos/$ID";
  else
    echo "❌ Unable to determine platform id / install commands"
    exit 1
  fi
fi

# --- Buck2 detection ---
if [ -z "${BUCK2_COMMAND}" ]; then
  if command -v buck2 >/dev/null; then
    BUCK2_COMMAND="buck2"
  elif command -v dotslash >/dev/null && [ -f ./buck2 ]; then
    BUCK2_COMMAND="dotslash ./buck2"
  else
    echo "❌ Unable to determine buck2 command"
    exit 1
  fi
fi

# --- Confirmation helper ---
__confirm() {
  if [ "$AUTO_CONFIRM" = true ]; then
    return 0
  fi
  echo "Press \"y\" to continue"
  read -r REPLY
  expr "X$REPLY" : '^X[Yy]$' >/dev/null
}

# --- Ensure jq is available ---
if ! command -v jq >/dev/null; then
  echo "Failed to find jq command, attempting to install with"
  echo
  echo "$INSTALL_COMMAND jq"
  echo
  if __confirm; then
    eval "$INSTALL_COMMAND jq"
  else
    echo "Not confirmed, exiting";
    exit 1
  fi
fi

# --- Generate package list ---
eval "$BUCK2_COMMAND cquery 'kind(system_packages, deps(//...))' \
    --output-attribute=packages --modifier $ID --json 2>/dev/null \
  | jq -r '.[].packages[]' \
  | sort \
  | uniq \
  > $PKG_FILE"

# --- Cache logic: skip already installed packages ---
install_pkg() {
  pkg="$1"
  if [ -f "$CACHE_FILE" ] && grep -Fxq "$pkg" "$CACHE_FILE"; then
    echo "[CACHE] Skipping $pkg (already installed)"
    return 0
  fi

  echo "[INSTALL] Installing $pkg..."
  if eval "$INSTALL_COMMAND $pkg"; then
    echo "$pkg" >> "$CACHE_FILE"
  else
    echo "❌ Failed to install $pkg" >&2
    return 1
  fi
}

# --- Summary and confirmation ---
echo
echo "📦 About to install project dependencies:"
echo
eval "cat $PKG_FILE | xargs echo $INSTALL_COMMAND"
echo

if __confirm; then
  echo "🚀 Starting installation..."
  mkdir -p "$(dirname "$CACHE_FILE")"
  touch "$CACHE_FILE"
  while read -r pkg; do
    [ -n "$pkg" ] && install_pkg "$pkg"
  done < "$PKG_FILE"
  echo "✅ All dependencies installed (cached list at $CACHE_FILE)"
else
  echo "❌ Installation cancelled"
fi

rm -f "$PKG_FILE"
