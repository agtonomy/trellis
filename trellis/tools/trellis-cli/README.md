# Trellis CLI

## Usage
Usage information can be obtained using:
```
> bazel run //trellis/tools/trellis-cli

Available trellis-cli subcommands:
	service: analyze rpc services
	node: analyze nodes (processes)
	topic: analyze pub/sub topics
```

Protobuf schema are automatically obtained from the discovery layer as long as at least one subscriber is already
available on the network.

## Examples
View running nodes
```
> bazel run //trellis/tools/trellis-cli -- node list

=============================================================
rclock                 : 1
hname                  : docker-desktop
pid                    : 87208
pname                  : /root/.cache/bazel/_bazel_root/d38006889cfbefac685079a4f89c15b3/execroot/trellis/bazel-out/k8-fastbuild/bin/trellis/examples/subscriber/subscriber
uname                  : subscriber_example
pparam                 : /root/.cache/bazel/_bazel_root/d38006889cfbefac685079a4f89c15b3/execroot/trellis/bazel-out/k8-fastbuild/bin/trellis/examples/subscriber/subscriber
pmemory                : 389156864
pcpu                   : 0
usrptime               : -1
datawrite              : 0
dataread               : 0
component_init_info    :pub|sub|srv|log|time

=============================================================
rclock                 : 1
hname                  : docker-desktop
pid                    : 88288
pname                  : /root/.cache/bazel/_bazel_root/d38006889cfbefac685079a4f89c15b3/execroot/trellis/bazel-out/k8-fastbuild/bin/trellis/tools/trellis-cli/trellis-cli
uname                  : trellis-cli
pparam                 : /root/.cache/bazel/_bazel_root/d38006889cfbefac685079a4f89c15b3/execroot/trellis/bazel-out/k8-fastbuild/bin/trellis/tools/trellis-cli/trellis-cli node list
pmemory                : 253177856
pcpu                   : 0
usrptime               : -1
datawrite              : 0
dataread               : 0
component_init_info    :
=============================================================
Displayed 2 entries.
```

List available active topics in running nodes
```
> bazel run //trellis/tools/trellis-cli -- topic list

=============================================================
tname        : hello_world
ttype        : proto:trellis.examples.proto.HelloWorld
direction    : subscriber
hname        : docker-desktop
pid          : 87208
tid          : 130541740013253
=============================================================
Displayed 1 entries.
```

Print messages being published on a specific topic
```
> bazel run //trellis/tools/trellis-cli -- topic echo -t hello_world

{
 "name": "Publisher Example",
 "id": 16,
 "msg": "Hello World!"
}

{
 "name": "Publisher Example",
 "id": 17,
 "msg": "Hello World!"
}
```

Publish on a topic (`hello_world`) a fixed number of messages (10) at a certain rate (1 Hz)
```
> bazel run //trellis/tools/trellis-cli -- topic publish -t hello_world -b '{}' -c 10 -r 1
```

List available running services
```
> bazel run //trellis/tools/trellis-cli -- service list

=============================================================
rclock     : 1
hname      : docker-desktop
pname      : /root/.cache/bazel/_bazel_root/d38006889cfbefac685079a4f89c15b3/execroot/trellis/bazel-out/k8-fastbuild/bin/trellis/examples/service_server/service_server
uname      : service_server_example
pid        : 86092
sname      : trellis.examples.proto.AdditionService
tcp_port   : 59579
=============================================================
Displayed 1 entries.
```

Display detailed information about a specific service
```
> bazel run //trellis/tools/trellis-cli -- service info -s trellis.examples.proto.AdditionService

=============================================================
rclock     : 1
hname      : docker-desktop
pname      : /root/.cache/bazel/_bazel_root/d38006889cfbefac685079a4f89c15b3/execroot/trellis/bazel-out/k8-fastbuild/bin/trellis/examples/service_server/service_server
uname      : service_server_example
pid        : 86092
sname      : trellis.examples.proto.AdditionService
tcp_port   : 59579


Methods:
mname          : Add
req_type       : AdditionRequest
resp_type      : AdditionResponse
call_count     : 0
```

## Limitations
Future releases will add support for sending service calls.
