cc_library(
    name = "core",
    srcs = [
        "config.cpp",
        "time.cpp",
        "timer.cpp",
        "transforms.cpp",
    ],
    hdrs = [
        "bind.hpp",
        "config.hpp",
        "error_code.hpp",
        "event_loop.hpp",
        "proto_utils.hpp",
        "time.hpp",
        "timer.hpp",
        "transforms.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":core_proto",
        "@asio",
        "@yaml-cpp",
    ],
)

cc_library(
    name = "core_ecal",
    srcs = [
        "monitor_interface.cpp",
        "node.cpp",
    ],
    hdrs = [
        "logging.hpp",
        "message_consumer.hpp",
        "monitor_interface.hpp",
        "node.hpp",
        "publisher.hpp",
        "service_client.hpp",
        "service_server.hpp",
        "subscriber.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":core",
        "//trellis/containers",
        "@ecal",
        "@fmt",
    ],
)

proto_library(
    name = "core_proto_lib",
    srcs = [
        "quaternion.proto",
        "rigid_transform.proto",
        "timestamped_message.proto",
        "vector3.proto",
    ],
    deps = [
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

cc_proto_library(
    name = "core_proto",
    deps = [":core_proto_lib"],
)
