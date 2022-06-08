cc_library(
    name = "core",
    srcs = [
        "timer.cpp",
    ],
    hdrs = [
        "bind.hpp",
        "error_code.hpp",
        "event_loop.hpp",
        "proto_utils.hpp",
        "timer.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":core_config",
        ":core_time",
        "@asio",
    ],
)

cc_library(
    name = "core_config",
    srcs = [
        "config.cpp",
    ],
    hdrs = [
        "config.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@yaml-cpp",
    ],
)

cc_library(
    name = "core_time",
    srcs = [
        "time.cpp",
    ],
    hdrs = [
        "time.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":core_proto",
    ],
)

cc_library(
    name = "core_ecal",
    srcs = [
        "monitor_interface.cpp",
        "node.cpp",
        "transforms.cpp",
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
        "transforms.hpp",
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
