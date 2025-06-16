#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

WORKSPACE_ROOT="$SCRIPT_DIR/.."
HOME_DIR="/root/trellis"

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
        echo "Unexpected architecture: $ARCHITECTURE"; exit 1;;
esac

# shellcheck disable=SC2068
docker run -it --rm \
  --network host \
  --ipc host \
  --pid host \
  -v "$WORKSPACE_ROOT:$HOME_DIR" \
  -v "$HOME/.cache:/root/.cache" \
  -v "/tmp:/tmp" \
  $DOCKER_IMAGE_NAME $@
