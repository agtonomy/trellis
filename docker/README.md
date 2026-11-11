# Docker Build Environment
This directory contains a `Dockerfile` for an environment to invoke `bazel build`.

In an ideal world, we wouldn't need a Docker environment because the goal is to
have our bazel build 100% hermetic, so a `bazel build ...` would work on the host
system without any consideration for system dependencies. Because we're not quite
there, this Dockerfile serves to define the system dependencies needed at build time.

# First Usage
At the time of writing, we're not using an image repository, so the image is built locally.
`./shell.sh` builds the image automatically if the version for the current checkout (`IMAGE_VERSION` in `env.sh`) is
not already present, so switching between branches that use different image versions just works.

Building the image manually:
```bash
./build.sh
```

# Changing the image
Bump `IMAGE_VERSION` in `env.sh` whenever the `Dockerfile` (or anything it copies in) changes, so other checkouts keep
using the image they were written against. Images are tagged `trellis-docker:<arch>_<version>`.

# Building
```bash
./shell.sh bazel build //your/target/here
```

# Dropping into the shell
```bash
# simply invoke without any arguments
./shell.sh
```
