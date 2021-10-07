cc_binary(
    name = "publisher",
    srcs = [
        "example_publisher.cpp",
    ],
    data = [
        "config.yml",
    ],
    deps = [
        ":cc_proto",
        "//trellis",
    ],
)

cc_binary(
    name = "subscriber",
    srcs = [
        "example_subscriber.cpp",
    ],
    data = [
        "config.yml",
    ],
    deps = [
        ":cc_proto",
        "//trellis",
    ],
)

cc_binary(
    name = "service_client",
    srcs = [
        "example_service_client.cpp",
    ],
    deps = [
        ":cc_proto",
        "//trellis",
    ],
)

cc_binary(
    name = "service_server",
    srcs = [
        "example_service_server.cpp",
    ],
    deps = [
        ":cc_proto",
        "//trellis",
    ],
)

proto_library(
    name = "proto",
    srcs = [
        "addition_service.proto",
        "hello_world.proto",
    ],
)

cc_proto_library(
    name = "cc_proto",
    deps = [":proto"],
)
