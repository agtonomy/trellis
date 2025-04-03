#!/bin/bash

Help()
{
  echo "Usage: build.sh [-h] [-b 1/0]"
  echo "h   Print usage"
  echo "b   Use docker build kit"
  exit 1
}

USE_DOCKER_BUILD_KIT=1
while getopts "hb:" OPTION; do
  case $OPTION in
    b) USE_DOCKER_BUILD_KIT=${OPTARG}; ;;
    h) Help; ;;
    *) Help; ;;
  esac;
done;

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
DOCKER_BUILDKIT=$USE_DOCKER_BUILD_KIT docker build . -t "$DOCKER_IMAGE_NAME"
