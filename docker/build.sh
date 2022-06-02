#!/bin/bash

set -e
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

ARCHITECTURE="$(uname -m)"
DOCKER_IMAGE_NAME="trellis-docker"

case $ARCHITECTURE in
    x86_64)
        DOCKER_IMAGE_NAME+=":amd64";;
    arm64)
        DOCKER_IMAGE_NAME+=":arm64";;
    aarch64)
        DOCKER_IMAGE_NAME+=":arm64";;
    *)
        echo "Failed to build. Unexpected architecture: $ARCHITECTURE"; exit 1;;
esac

cd "$SCRIPT_DIR"
DOCKER_BUILDKIT=1 docker build . -t "$DOCKER_IMAGE_NAME"
