licenses(["notice"])  # MIT

cc_library(
    name = "mcap",
    srcs = glob(["cpp/mcap/include/mcap/*.inl"]),
    hdrs = glob(["cpp/mcap/include/mcap/*.hpp"]),
    includes = ["cpp/mcap/include/"],
    visibility = ["//visibility:public"],
    deps = [
        "@lz4",
        "@zstd",
    ],
)
