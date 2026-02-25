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

bazel_version="$(cat "$(cd "$SCRIPT_DIR"; git rev-parse --show-toplevel 2>/dev/null)"/.bazelversion)"

ARCHITECTURE="$(uname -m)"
DOCKER_IMAGE_NAME="trellis-docker"

case $ARCHITECTURE in
    x86_64) DOCKER_IMAGE_NAME+=":amd64" ;;
    arm64|aarch64) DOCKER_IMAGE_NAME+=":arm64" ;;
    *) echo "Failed to build. Unexpected architecture: $ARCHITECTURE"; exit 1 ;;
esac

cd "$(dirname "${BASH_SOURCE[0]}")"
docker buildx build --build-arg BAZEL_VERSION="$bazel_version" \
    -t "$DOCKER_IMAGE_NAME" --load .
