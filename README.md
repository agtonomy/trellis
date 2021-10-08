# Trellis
Trellis is a general-purpose middleware library developed by Agtonomy. It is
being designed for embedded robotics & autonomy software, but it can be used
for other applications as well.

## Actor Model
Trellis is based on the actor model, in which actors can only effect each other's
state indirectly through the passing of messages.

See: https://en.wikipedia.org/wiki/Actor_model

## eCAL Core
At the core, Trellis is built on top of [https://github.com/continental/ecal](Continental's enhanced Communication Abstraction Layer) (eCAL) library. This brings in a core set of functionality:

1. Dynamic service discovery
1. Inter-process messaging patterns (pubsub and rpc)
1. Protobuf-based messaging
1. Shared memory and UDP transport
1. System introspection tools
1. Data recording and replay tools

## Additional Features
In addition to the functionality provided by eCAL, Trellis aims to provide
the following:

1. Configuration and parameter management framework
1. A framework for developing applications which behave deterministically
1. Data visualization tools
1. An integration testing framework
1. An abstraction layer for common forms of I/O

## Middleware Primitives
Trellis provides the following core primitives

1. Node - for defining an application and constructing other primitives
1. Publisher - for sending messages
1. Subscriber - for receiving messages
1. Service Client - for initiating remote procedures
1. Service Server - for providing remotely callable procedures

### Services (RPC)
Services are implemented using protobuf's RPC syntax, which declares a method
name, input message type, and output message type. See `examples` for more detail.

## Threading
eCAL's threading model is documented here: https://continental.github.io/ecal/advanced/threading_model.html

### Single-threaded approach
Trellis aims to be as single-threaded as possible with respect to user callbacks.

This means that user callbacks provided to Trellis should all get called on the
same thread. This removes the burden on the application developer from dealing
with threading issues such as data access synchronization.

#### ASIO
Trellis creates an event loop managed by `asio` to eventually handle all user
callbacks.

At the time of writing the `MessageConsumer` callbacks are invoked on this single
thread. In the near future, timer callbacks will also be invoked on this thread.

## Examples
See `examples` directory for some code examples for publishing, subscribing, calling
a service, and hosting a service.
