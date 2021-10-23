cc_library(
    name = "asio",
    srcs = glob([
        "asio/**/*.hpp",
        "asio/**/*.ipp",
    ]),
    hdrs = glob(
        ["asio/include/*"],
        exclude = [
            "asio/include/asio/detail/config.hpp",
        ],
    ),
    defines = [
        "ASIO_STANDALONE",
    ],
    includes = ["asio/include"],
    linkopts = [
        "-lpthread",
    ],
    visibility = ["//visibility:public"],
)
