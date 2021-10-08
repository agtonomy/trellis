#!/bin/bash

set -e
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
DOCKER_IMAGE_NAME="trellis-docker"

cd "$SCRIPT_DIR"
DOCKER_BUILDKIT=1 docker build . -t "$DOCKER_IMAGE_NAME"
