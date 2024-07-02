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

# Install bazelisk for target architecture
RUN curl -L https://github.com/bazelbuild/bazelisk/releases/download/v1.10.1/bazelisk-linux-$TARGETARCH -O && \
  mv bazelisk-linux-$TARGETARCH /usr/local/bin/bazel && \
  chmod +x /usr/local/bin/bazel

# Install dependencies and buildifier (6.4.0 - 2023-11-15)
RUN curl -L https://github.com/bazelbuild/buildtools/releases/download/v6.4.0/buildifier-linux-$TARGETARCH -O && \
  mv buildifier-linux-$TARGETARCH /usr/local/bin/buildifier && \
  chmod +x /usr/local/bin/buildifier

# Get clang-format-18
RUN curl https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add - && echo "deb http://apt.llvm.org/focal/ llvm-toolchain-focal-18 main" >> /etc/apt/sources.list
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
  clang-format-18 && \
  ln -s /usr/bin/clang-format-18 /usr/bin/clang-format && \
  rm -rf /var/lib/apt/lists/*

# Default python binary to python3
RUN ln -s /usr/bin/python3 /usr/bin/python

COPY scripts/* /usr/local/bin/
COPY ecal.ini /etc/ecal/ecal.ini
COPY entrypoint.sh /root/entrypoint.sh
ENTRYPOINT ["/root/entrypoint.sh"]
CMD ["/bin/bash"]
