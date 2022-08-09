FROM ubuntu:22.04

ARG TARGETARCH
ARG TARGETPLATFORM

RUN echo "I'm building for $TARGETARCH, $TARGETPLATFORM"

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
  curl \
  vim \
  git \
  build-essential \
  clang-format \
  shellcheck \
  golang \
  && rm -rf /var/lib/apt/lists/*

# Install bazelisk for target architecture
RUN curl -L https://github.com/bazelbuild/bazelisk/releases/download/v1.10.1/bazelisk-linux-$TARGETARCH -O && \
    mv bazelisk-linux-$TARGETARCH /usr/local/bin/bazel && \
    chmod +x /usr/local/bin/bazel

RUN go install github.com/bazelbuild/buildtools/buildifier@latest && \
    cp ~/go/bin/buildifier /usr/local/bin/

# Default python binary to python3
RUN ln -s /usr/bin/python3 /usr/bin/python

COPY scripts/* /usr/local/bin/
COPY ecal.ini /etc/ecal/ecal.ini
COPY entrypoint.sh /root/entrypoint.sh
ENTRYPOINT ["/root/entrypoint.sh"]
CMD ["/bin/bash"]
