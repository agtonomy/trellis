# Trellis

`/ˈtrelis/`
_noun_

1. a frame or structure of latticework; lattice.
2. a framework of this kind used as a support for growing vines or plants.
3. a middleware framework for distributed applications using the actor model.

Trellis is primarily developed for use in robotics in an embedded Linux environment, however the project aims to be suited for general purpose.

**NOTE**: This project is in its early stages and is being actively developed.

![main](https://github.com/agtonomy/trellis/actions/workflows/main.yaml/badge.svg)

## Language & OS Support
Trellis is C++-only at the time of writing although additional language support is anticipated in the future.

Additionally, Trellis is only supported on Linux. At the moment there are no plans of supporting other platforms.

## Actor Model
Trellis is based on the actor model, in which actors can only effect each other's
state indirectly through the passing of messages.

See: https://en.wikipedia.org/wiki/Actor_model

## Asynchronous, event-driven architecture
Trellis applications are intended to be purely event-driven with callbacks firing in response to events. Most commonly due to inbound messages and/or the passage of time in the case of timers.

Trellis uses [Asio](https://think-async.com/Asio/) under the hood to run an event loop.

## eCAL Core
At the core, Trellis is built on top of [Continental's enhanced Communication Abstraction Layer](https://github.com/continental/ecal) (eCAL) library. This brings in a core set of functionality:

1. Dynamic service discovery
1. Inter-process messaging patterns (pubsub and rpc)
1. Protobuf-based messaging
1. Shared memory and UDP transport
1. System introspection tools
1. Data recording and replay tools

## Additional Features
In addition to the functionality provided by eCAL, Trellis aims to provide additional functionality including but not limited to:

1. Configuration and parameter management framework
1. Deterministic replay of messages
1. A framework for integration tests that span across applications
1. An abstraction layer for common forms of I/O
1. A framework for broadcasting reference frame transformations

Note: Some of these features are either not yet implemented or partially implemented.

## Middleware Primitives
Trellis provides the following core primitives

1. Node - for defining an application and constructing other primitives
1. Publisher - for sending messages
1. Subscriber - for receiving messages
1. Service Client - for initiating remote procedures
1. Service Server - for providing remotely callable procedures
1. Timer - for invoking callbacks at fixed time intervals
1. MessageConsumer - for receiving messages from many publishers in a thread-safe way with minimal boilerplate
1. Transforms - for caching reference frame transformations and broadcasting updates

### Services (RPC)
Services are implemented using Protobuf's RPC syntax, which declares a method
name, input message type, and output message type. See `examples` for more detail.

## Threading
eCAL's threading model is documented here: https://continental.github.io/ecal/advanced/threading_model.html

### Single-threaded approach
Trellis aims to hide eCAL's threading from the user by invoking all user callbacks from a single event-loop thread, which is managed by [Asio](https://think-async.com/Asio/).

This means that user callbacks provided to Trellis should all get called on the
same thread. This removes the burden on the application developer from dealing
with thread-safety.

At the time of writing the `MessageConsumer` and `Timer` callbacks are invoked
on the event loop.

## Bazel
Trellis is built on Google's [Bazel](https://bazel.build/) build system.

### Depending on trellis

Add to your WORKSPACE file:

```
TRELLIS_COMMIT = "XXXX"

http_archive(
    name = "com_github_agtonomy_trellis",
    strip_prefix = "trellis-" + TRELLIS_COMMIT,
    url = "https://github.com/agtonomy/trellis/archive/" + TRELLIS_COMMIT + ".tar.gz",
    # Make sure to add the correct sha256 corresponding to this commit.
    # sha256 = "blah",
)

load("@com_github_agtonomy_trellis//third_party:repositories.bzl", "trellis_deps")

trellis_deps()

# Required transitive loader for protobuf dependencies.
load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")

protobuf_deps()
```

## Examples
See `examples` directory for some code examples for publishing, subscribing, calling
a service, and hosting a service.

### Quick Start

#### With Docker
You can use the provided Docker image, which has all the build/runtime dependencies
```bash
# Build the image (only needs to be done once)
./docker/build.sh

# Run the examples in the docker environment
./docker/shell.sh bazel run //trellis/examples/publisher
./docker/shell.sh bazel run //trellis/examples/subscriber
./docker/shell.sh bazel run //trellis/examples/service_server
./docker/shell.sh bazel run //trellis/examples/service_client
```

Alternatively, you can run `./docker/shell.sh` without any arguments to drop into a bash shell within the Docker environment.

#### Without Docker
You can simply run bazel natively, assuming all system dependencies are met. See the `Dockerfile` to understand the system dependencies.
```bash
bazel run //trellis/examples/publisher
bazel run //trellis/examples/subscriber
bazel run //trellis/examples/service_server
bazel run //trellis/examples/service_client
```
