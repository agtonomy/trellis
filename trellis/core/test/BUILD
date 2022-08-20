proto_library(
    name = "test_proto_lib",
    srcs = [
        "test.proto",
    ],
)

cc_proto_library(
    name = "test_proto",
    deps = [":test_proto_lib"],
)

cc_library(
    name = "test_fixture",
    hdrs = [
        "test_fixture.hpp",
    ],
    deps = [
        "//trellis/core:core_ecal",
        "@gtest//:gtest_main",
    ],
)

cc_test(
    name = "test_core_message_consumer",
    srcs = [
        "test_message_consumer.cpp",
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
        "test_node.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_fixture",
    ],
)

cc_test(
    name = "test_core_pubsub",
    srcs = [
        "test_pubsub.cpp",
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
        "test_service.cpp",
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
        "test_timer.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_fixture",
    ],
)

cc_test(
    name = "test_core_time",
    srcs = [
        "test_time.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//trellis/core:core_ecal",
        "@gtest//:gtest_main",
    ],
)

cc_test(
    name = "test_core_simtime",
    srcs = [
        "test_simtime.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":test_fixture",
        ":test_proto",
        "//trellis/core:core_ecal",
        "@gtest//:gtest_main",
    ],
)

cc_test(
    name = "test_core_config",
    srcs = [
        "test_config.cpp",
    ],
    data = [
        ":test_base_config.yml",
        ":test_overlay_config.yml",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//trellis/core:core_config",
        "@gtest//:gtest_main",
    ],
)

cc_test(
    name = "test_core_proto_utils",
    srcs = [
        "test_proto_utils.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//trellis/core",
        "@gtest//:gtest_main",
    ],
)
