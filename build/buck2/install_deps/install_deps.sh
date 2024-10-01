#!/bin/sh
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

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
    echo "Unable to determine platform id / install commands";
    return 1;
  fi
fi

if [ -z "${BUCK2_COMMAND}" ]; then
  if command -v buck2 >/dev/null; then
    BUCK2_COMMAND="buck2"
  elif command -v dotslash >/dev/null && [ -f ./buck2 ]; then
    BUCK2_COMMAND="dotslash ./buck2"
  else
    echo "Unable to determine buck2 command";
    return 1;
  fi
fi

__confirm() {
  echo "Press \"y\" to continue"
  read -r REPLY
  expr "X$REPLY" : '^X[Yy]$' >/dev/null
}

PKG_FILE=$(mktemp /tmp/buck2-install-pkgs.XXXXXX)

if ! command -v jq >/dev/null; then
  echo "Failed to find jq command, attempting to install with"
  echo
  echo "$INSTALL_COMMAND" jq
  echo
  if __confirm; then
    eval "$INSTALL_COMMAND jq"
  else
    echo "Not confirmed, exiting";
    exit 1
  fi
fi

eval "$BUCK2_COMMAND cquery 'kind(system_packages, deps(//...))' \\
    --output-attribute=packages --modifier $ID --json 2>/dev/null \\
  | jq -r '.[].packages[]' \\
  | sort \\
  | uniq \\
  > $PKG_FILE"

echo "About to install the project dependencies with the following command:"
echo
eval "cat $PKG_FILE | xargs echo $INSTALL_COMMAND"
echo
if __confirm; then
  eval "cat $PKG_FILE | xargs -r $INSTALL_COMMAND"
else
  echo "Not installing dependencies"
fi

rm "$PKG_FILE"
