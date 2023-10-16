licenses(["notice"])  # BSD license

cc_library(
    name = "zstd",
    srcs = glob([
        "lib/common/*.c",
        "lib/common/*.h",
        "lib/compress/*.c",
        "lib/compress/*.h",
        "lib/decompress/*.c",
        "lib/decompress/*.h",
        "lib/decompress/*.S",
        "lib/*.h",
    ]),
    hdrs = [
        "lib/zdict.h",
        "lib/zstd.h",
    ],
    defines = [],
    includes = ["lib/"],
    visibility = ["//visibility:public"],
)
