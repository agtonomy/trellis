# Trellis CLI

## Usage
Usage information can be obtained using:
```
bazel run //trellis/tools/trellis-cli
```

Protobuf schema are automatically obtained from the discovery layer as long as at least one subscriber is already
available on the network.

## Examples
Publish on a topic (`/navigator/state`) a fixed number of messages (10,000) at a certain rate (10 Hz)
```
bazel run //trellis/tools/trellis-cli -- topic publish -t hello_world -b '{}' -c 10 -r 1
```

## Limitations
The CLI tool currently only supports publishers and subscribers. Future releases will add support for service calls.
