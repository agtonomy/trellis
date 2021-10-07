cc_library(
    name = "tclap",
    hdrs = glob(["include/**/*.h"]),
    copts = [
        "-fexceptions",
    ],
    defines = [
        "HAVE_LONG_LONG=1",
        "HAVE_SSTREAM=1",
    ],
    features = ["-parse_headers"],
    includes = [
        "include",
    ],
    visibility = ["//visibility:public"],
)
