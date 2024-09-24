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

PKG_FILE=$(mktemp /tmp/buck2-install-pkgs.XXXXXX)

eval "$BUCK2_COMMAND \\
    cquery \"attrregexfilter(labels, 'third-party:$ID:', deps(//...))\" \\
    --json --output-attribute=labels 2>/dev/null \\
  | grep -o \"third-party:$ID:[^\\\"]*\" \\
  | sort \\
  | uniq \\
  | sed \"s/third-party:$ID://\" \\
  > $PKG_FILE"

echo "About to install the project dependencies with the following command:"
echo
eval "cat $PKG_FILE | xargs echo $INSTALL_COMMAND"
echo
echo "Press \"y\" to continue"
read -r REPLY
echo

if expr "X$REPLY" : '^X[Yy]$' >/dev/null; then
  eval "cat $PKG_FILE | xargs -r $INSTALL_COMMAND"
else
  echo "Not installing dependencies"
fi

rm "$PKG_FILE"
