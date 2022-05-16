cc_library(
    name = "network",
    hdrs = [
        "tcp.hpp",
        "udp.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//trellis/core",
    ],
)

cc_test(
    name = "test",
    srcs = [
        "test/test_tcp.cpp",
        "test/test_udp.cpp",
    ],
    deps = [
        ":network",
        "@gtest//:gtest_main",
    ],
)
