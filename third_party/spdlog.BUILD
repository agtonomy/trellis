cc_library(
    name = "spdlog",
    srcs = [
        "src/async.cpp",
        "src/cfg.cpp",
        "src/color_sinks.cpp",
        "src/file_sinks.cpp",
        "src/fmt.cpp",
        "src/spdlog.cpp",
        "src/stdout_sinks.cpp",
    ],
    hdrs = glob([
        "include/**/*.h",
    ]),
    defines = ["SPDLOG_COMPILED_LIB"],
    includes = ["include"],
    visibility = ["//visibility:public"],
)
