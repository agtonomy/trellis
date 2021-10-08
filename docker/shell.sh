#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

WORKSPACE_ROOT="$SCRIPT_DIR/.."
HOME_DIR="/root/trellis"

# shellcheck disable=SC2068
docker run -it --rm \
  --network host \
  --ipc host \
  --pid host \
  -v "$WORKSPACE_ROOT:$HOME_DIR" \
  -v "$HOME/.cache:/root/.cache" \
  trellis-docker $@
