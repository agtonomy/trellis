cc_library(
    name = "containers",
    visibility = ["//visibility:public"],
    hdrs = [
        "fifo.hpp",
        "multi_fifo.hpp",
    ],
)

cc_test(
    name = "test",
    srcs = ["test/test_multi_fifo.cpp"],
    deps = [
        ":containers",
        "@gtest//:gtest_main",
    ],
)
