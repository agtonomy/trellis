#!/bin/bash

export IMAGE_NAME="trellis-docker"

# This version should be bumped every time the Dockerfile (or anything it copies in) is changed.
# The format of the version is Major.Minor
export IMAGE_VERSION="2.0"

# The resulting image name will have the format:
# ${IMAGE_NAME}:${TARGETARCH}_${IMAGE_VERSION}
