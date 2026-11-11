#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
WORKSPACE_ROOT="$SCRIPT_DIR/.."

HOME_DIR="/workspace"
REPO_ROOT="$HOME_DIR/trellis"

# shellcheck disable=SC1091
. "$SCRIPT_DIR/env.sh" # Provides $IMAGE_NAME and $IMAGE_VERSION

docker_args=()
docker_cmd=()

while [ "$1" ]; do
    case "$1" in
        --docker-args)
            docker_args+=("$2")
            shift 2
            ;;
        *)
            docker_cmd+=("$1")
            shift
            ;;
    esac
done

case "$(uname -m)" in
    x86_64) tag=amd64;;
    arm64|aarch64) tag=arm64;;
    *) echo "Unexpected architecture: $ARCHITECTURE"; exit 1;;
esac

# If the image name or version are set by the environment the image must already be available,
# the build script will only build the default tag
docker_image="${TRELLIS_IMAGE_NAME:-$IMAGE_NAME}:${TRELLIS_IMAGE_VERSION:-${tag}_$IMAGE_VERSION}"

if ! docker inspect "$docker_image" >&/dev/null; then
  # Desired image version not found, build it:
  "$SCRIPT_DIR/build.sh"
fi

if ! docker inspect "$docker_image" >&/dev/null; then
  echo "Error: Docker image $docker_image not found after running $SCRIPT_DIR/build.sh"
  exit 1
fi

if [ -t 1 ]; then
  docker_args+=(-it)
fi

bazelrc="$HOME/.bazelrc"
if [ -f "$bazelrc" ]; then
  docker_args+=(-v "$bazelrc:$HOME_DIR/.bazelrc")
fi

docker run --rm \
  --network host \
  --ipc host \
  --pid host \
  -u trellis \
  -v "$WORKSPACE_ROOT:$REPO_ROOT" \
  -v "$HOME/.cache:$HOME_DIR/.cache" \
  -v "/tmp:/tmp" \
  "${docker_args[@]}" \
  "$docker_image" \
  "${docker_cmd[@]}"
