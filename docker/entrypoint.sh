#!/bin/bash

set -e

WORKSPACE_DIR="/root/trellis"
BAZEL_CACH_SECRET_FILE="/tmp/gcs.json"

# This is a volume mounted directory and thus may not always exist, such as
# in CI contexts.
if [ -d "$WORKSPACE_DIR" ]; then
  cd "$WORKSPACE_DIR"
fi

# This is the secret for accessing the bazel build cache, which is available
# in CI contexts.
if [ -n "$BAZEL_CACHE_SECRET" ]; then
  echo "$BAZEL_CACHE_SECRET" > "$BAZEL_CACH_SECRET_FILE"
fi

# `$*` expands the `args` supplied in an `array` individually
# or splits `args` in a string separated by whitespace.
sh -c "$*"
