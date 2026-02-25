FROM ubuntu:22.04

ARG TARGETARCH
ARG TARGETPLATFORM

RUN echo "I'm building for $TARGETARCH, $TARGETPLATFORM"

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
  curl \
  vim \
  git \
  build-essential \
  shellcheck \
  golang \
  && rm -rf /var/lib/apt/lists/*

ARG BAZELISK_VERSION=1.27.0

# Install bazelisk for target architecture
RUN curl -sSL \
        https://github.com/bazelbuild/bazelisk/releases/download/v$BAZELISK_VERSION/bazelisk-linux-$TARGETARCH > \
        /usr/local/bin/bazel && \
    chmod +x /usr/local/bin/bazel

# Install dependencies and buildifier (6.4.0 - 2023-11-15)
RUN curl -L https://github.com/bazelbuild/buildtools/releases/download/v6.4.0/buildifier-linux-$TARGETARCH -O > \
        /usr/local/bin/buildifier && \
    chmod +x /usr/local/bin/buildifier

# Get clang-format-18
RUN curl https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add - && \
    echo "deb http://apt.llvm.org/focal/ llvm-toolchain-focal-18 main" >> /etc/apt/sources.list && \
    apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    clang-format-18 && \
    ln -s /usr/bin/clang-format-18 /usr/bin/clang-format && \
    rm -rf /var/lib/apt/lists/*

# Default python binary to python3
RUN ln -s /usr/bin/python3 /usr/bin/python

ARG BAZEL_VERSION=6.5.0

# Add trellis user for non-root execution
RUN useradd -d /workspace -ms /bin/bash trellis && \
    USE_BAZEL_VERSION="$BAZEL_VERSION" runuser -u trellis /usr/local/bin/bazel version # Now that we have our user, cache the bazel version we need


COPY scripts/* /usr/local/bin/
COPY entrypoint.sh /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
CMD ["/bin/bash"]
