
cc_library(
    name = "core",
    visibility = ["//visibility:public"],
    srcs = [
        "config.cpp",
        "time.cpp",
        "timer.cpp",
    ],
    hdrs = [
        "bind.hpp",
        "config.hpp",
        "error_code.hpp",
        "event_loop.hpp",
        "proto_utils.hpp",
        "time.hpp",
        "timer.hpp",
    ],
    deps = [
        ":core_proto",
        "@asio",
        "@yaml-cpp",
    ],
)

cc_library(
    name = "core_ecal",
    visibility = ["//visibility:public"],
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
    deps = [
        "//trellis/containers",
        ":core",
        "@ecal",
        "@fmt",
    ],
)

proto_library(
    name = "core_proto_lib",
    srcs = [
        "timestamped_message.proto",
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

######### Test related targets below #########

proto_library(
    name = "test_proto_lib",
    srcs = [
        "test/test.proto",
    ],
)

cc_proto_library(
    name = "test_proto",
    deps = [":test_proto_lib"],
)

cc_library(
    name = "test_fixture",
    hdrs = [
        "test/test_fixture.hpp",
    ],
    deps = [
        ":core_ecal",
        "@gtest//:gtest_main",
    ],
)

cc_test(
    name = "test_core_message_consumer",
    srcs = [
        "test/test_message_consumer.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_fixture",
        ":test_proto",
    ],
)

cc_test(
    name = "test_core_node",
    srcs = [
        "test/test_node.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_fixture",
    ],
)

cc_test(
    name = "test_core_pubsub",
    srcs = [
        "test/test_pubsub.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_fixture",
        ":test_proto",
    ],
)

cc_test(
    name = "test_core_service",
    srcs = [
        "test/test_service.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_fixture",
        ":test_proto",
    ],
)

cc_test(
    name = "test_core_timer",
    srcs = [
        "test/test_timer.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_fixture",
    ],
)

cc_test(
    name = "test_core_time",
    srcs = [
        "test/test_time.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":core_ecal",
        "@gtest//:gtest_main",
    ],
)

cc_test(
    name = "test_core_simtime",
    srcs = [
        "test/test_simtime.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_fixture",
        ":test_proto",
        ":core_ecal",
        "@gtest//:gtest_main",
    ],
)
