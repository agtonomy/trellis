#!/bin/bash

Help()
{
  echo "Usage: build.sh [-h] [-b 1/0]"
  echo "h   Print usage"
  echo "b   Use docker build kit"
  exit 1
}

DOCKER_BUILDKIT=1
while getopts "hb:" OPTION; do
  case $OPTION in
    b) DOCKER_BUILDKIT=${OPTARG}; ;;
    h) Help; ;;
    *) Help; ;;
  esac;
done;

set -e

export DOCKER_BUILDKIT

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"

# shellcheck disable=SC1091
. "$SCRIPT_DIR/env.sh" # Provides $IMAGE_NAME and $IMAGE_VERSION

bazel_version="$(cat "$SCRIPT_DIR/../.bazelversion")"

ARCHITECTURE="$(uname -m)"

case $ARCHITECTURE in
    x86_64) arch=amd64 ;;
    arm64|aarch64) arch=arm64 ;;
    *) echo "Failed to build. Unexpected architecture: $ARCHITECTURE"; exit 1 ;;
esac

cd "$SCRIPT_DIR"
docker buildx build --build-arg BAZEL_VERSION="$bazel_version" \
    -t "$IMAGE_NAME:${arch}_$IMAGE_VERSION" --load .
