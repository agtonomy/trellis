cc_library(
    name = "core",
    visibility = ["//visibility:public"],
    deps = [
        ":core_bind",
        ":core_config",
        ":core_error_code",
        ":core_event_loop",
        ":core_logging",
        ":core_message_consumer",
        ":core_monitor_interface",
        ":core_node",
        ":core_proto",
        ":core_proto_utils",
        ":core_publisher",
        ":core_service_client",
        ":core_service_server",
        ":core_subscriber",
        ":core_time",
        ":core_transforms",
    ],
)

cc_library(
    name = "core_bind",
    hdrs = [
        "bind.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
    ],
)

cc_library(
    name = "core_error_code",
    hdrs = [
        "error_code.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@asio",
    ],
)

cc_library(
    name = "core_event_loop",
    hdrs = [
        "event_loop.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
    ],
)

cc_library(
    name = "core_proto_utils",
    hdrs = [
        "proto_utils.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
    ],
)

cc_library(
    name = "core_timer",
    srcs = [
        "timer.cpp",
    ],
    hdrs = [
        "timer.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":core_error_code",
        ":core_event_loop",
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
        ":core_logging",
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
    name = "core_logging",
    srcs = [
        "logging.cpp",
    ],
    hdrs = [
        "logging.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@ecal",
        "@fmt",
    ],
)

cc_library(
    name = "core_node",
    srcs = [
        "node.cpp",
    ],
    hdrs = [
        "node.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":core_bind",
        ":core_config",
        ":core_event_loop",
        ":core_proto_utils",
        ":core_publisher",
        ":core_service_client",
        ":core_service_server",
        ":core_subscriber",
        "@ecal",
    ],
)

cc_library(
    name = "core_monitor_interface",
    srcs = [
        "monitor_interface.cpp",
    ],
    hdrs = [
        "monitor_interface.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":core_proto_utils",
        "@ecal",
    ],
)

cc_library(
    name = "core_transforms",
    srcs = [
        "transforms.cpp",
    ],
    hdrs = [
        "transforms.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":core_config",
        ":core_message_consumer",
        ":core_node",
        "//trellis/containers",
    ],
)

cc_library(
    name = "core_message_consumer",
    hdrs = [
        "message_consumer.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//trellis/containers",
    ],
)

cc_library(
    name = "core_publisher",
    hdrs = [
        "publisher.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":core_proto_utils",
        ":core_time",
        "@ecal",
    ],
)

cc_library(
    name = "core_service_client",
    hdrs = [
        "service_client.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":core_timer",
        "@ecal",
    ],
)

cc_library(
    name = "core_service_server",
    hdrs = [
        "service_server.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@ecal",
    ],
)

cc_library(
    name = "core_subscriber",
    hdrs = [
        "subscriber.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":core_monitor_interface",
        "@ecal",
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
