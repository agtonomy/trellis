cc_library(
    name = "containers",
    hdrs = [
        "fifo.hpp",
        "multi_fifo.hpp",
    ],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "test",
    srcs = ["test/test_multi_fifo.cpp"],
    deps = [
        ":containers",
        "@gtest//:gtest_main",
    ],
)
