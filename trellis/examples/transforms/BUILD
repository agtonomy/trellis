cc_library(
    name = "transforms_app",
    srcs = [
        "app.cpp",
        "app.hpp",
    ],
    deps = [
        "//trellis",
    ],
)

cc_binary(
    name = "nodea",
    srcs = [
        "main_a.cpp",
    ],
    data = [
        "//trellis/examples:config.yml",
    ],
    deps = [
        ":transforms_app",
        "//trellis",
        "//trellis/examples/proto",
    ],
)

cc_binary(
    name = "nodeb",
    srcs = [
        "main_b.cpp",
    ],
    data = [
        "//trellis/examples:config.yml",
    ],
    deps = [
        ":transforms_app",
        "//trellis",
        "//trellis/examples/proto",
    ],
)
