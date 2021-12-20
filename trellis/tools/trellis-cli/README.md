# Trellis CLI

## Usage
Usage information can be obtained using:
```
./trellis-cli -h
```

Protobuf schema are automatically obtained from the discovery layer as long as at least one subscriber is already
available on the network.

## Examples
Publish on a topic (`/navigator/state`) a fixed number of messages (10,000) at a certain rate (10 Hz)
```
./docker/shell.sh bazel run //trellis/tools/trellis-cli -- topic publish -t /navigator/state -b '{loaded: false, mission_id: ef3b750c-9027-46ef-bbbb-e3d36103d4d2, completed: false, percentage: 0, enabled: false, health: {state: 1, inputs: 1, controller_state: 0, interlock_tripped: true}}' -c 10000 -r 10
```

## Limitations
The CLI tool currently only supports publishers and subscribers. Future releases will add support for service calls.
