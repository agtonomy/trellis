cc_library(
    name = "trellis",
    visibility = ["//visibility:public"],
    deps = [
        "//trellis/containers",
        "//trellis/core",
        #"//trellis/core:core_ecal",
        "//trellis/network",
    ],
)

# Produce a shared object that can be exported and used in other projects with non-Bazel build systems
cc_binary(
    name = "trellis.so",
    linkshared = True,
    visibility = ["//visibility:public"],
    deps = [
        ":trellis",
    ],
)
