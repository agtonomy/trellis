cc_library(
    name = "fmt",
    srcs = [
        "src/format.cc",
        "src/os.cc",
    ],
    hdrs = glob(["include/fmt/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "fmt_headers",
    srcs = glob(["include/fmt/*.h"]),
    visibility = ["//visibility:public"],
)
