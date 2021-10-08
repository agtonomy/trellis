# Docker Build Environment
This directory contains a `Dockerfile` for an environment to invoke `bazel build`.

In an ideal world, we wouldn't need a Docker environment because the goal is to
have our bazel build 100% hermetic, so a `bazel build ...` would work on the host
system without any consideration for system dependencies. Because we're not quite
there, this Dockerfile serves to define the system dependencies needed at build time.

# First Usage
At the time of writing, we're not using an image repository, so you must build
the image locally.

Building the image:
```bash
./build.sh
```

# Building
```bash
./shell.sh bazel build //your/target/here
```

# Dropping into the shell
```bash
# simply invoke without any arguments
./shell.sh
```
