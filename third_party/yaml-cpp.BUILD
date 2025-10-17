# Although the upstream yaml-cpp project does in fact contain a BUILD.bazel file
# We maintain our own version here so that we can also define the filegroup, which
# allows us to export header files in the case of packaging a shared object of trellis

cc_library(
    name = "yaml-cpp_internal",
    hdrs = glob(["src/**/*.h"]),
    strip_include_prefix = "src",
    visibility = ["//:__subpackages__"],
)

cc_library(
    name = "yaml-cpp",
    srcs = glob([
        "src/**/*.cpp",
        "src/**/*.h",
    ]),
    hdrs = glob(["include/**/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "yaml_cpp_headers",
    srcs = glob(["include/**/*.h"]),
    visibility = ["//visibility:public"],
)
