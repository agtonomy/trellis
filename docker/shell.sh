#!/bin/bash

WORKSPACE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." &> /dev/null && pwd)"

HOME_DIR="/workspace"
REPO_ROOT="$HOME_DIR/trellis"

DOCKER_IMAGE="trellis-docker"

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
  "$DOCKER_IMAGE:$tag" \
  "${docker_cmd[@]}"
