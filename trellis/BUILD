cc_library(
    name = "trellis_core",
    srcs = [
        "core/config.cpp",
    ],
    hdrs = [
        "core/bind.hpp",
        "core/config.hpp",
        "core/error_code.hpp",
        "core/event_loop.hpp",
        "core/timer.hpp",
    ],
    deps = [
        "@asio",
        "@yaml-cpp",
    ],
)

cc_library(
    name = "trellis_core_ecal",
    srcs = [
        "core/node.cpp",
    ],
    hdrs = [
        "core/logging.hpp",
        "core/message_consumer.hpp",
        "core/node.hpp",
        "core/publisher.hpp",
        "core/service_client.hpp",
        "core/service_server.hpp",
        "core/subscriber.hpp",
        "core/time.hpp",
    ],
    deps = [
        ":trellis_core",
        "@ecal",
        "@fmt",
    ],
)

cc_library(
    name = "trellis_containers",
    hdrs = [
        "containers/fifo.hpp",
        "containers/multi_fifo.hpp",
    ],
)

cc_library(
    name = "trellis_network",
    hdrs = [
        "network/tcp.hpp",
        "network/udp.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":trellis_core",
    ],
)

cc_library(
    name = "trellis",
    visibility = ["//visibility:public"],
    deps = [
        ":trellis_containers",
        ":trellis_core",
        ":trellis_core_ecal",
        ":trellis_network",
    ],
)

cc_test(
    name = "test_containers",
    srcs = ["containers/test/test_multi_fifo.cpp"],
    visibility = ["//visibility:public"],
    deps = [
        ":trellis_containers",
        "@gtest//:gtest_main",
    ],
)

cc_test(
    name = "test_network",
    srcs = [
        "network/test/test_tcp.cpp",
        "network/test/test_udp.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":trellis_network",
        "@gtest//:gtest_main",
    ],
)
