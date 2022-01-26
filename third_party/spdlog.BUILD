cc_library(
    name = "spdlog",
    hdrs = glob([
        "include/**/*.h",
    ]),
    srcs = [
      "src/async.cpp",
      "src/cfg.cpp",
      "src/color_sinks.cpp",
      "src/file_sinks.cpp",
      "src/fmt.cpp",
      "src/spdlog.cpp",
      "src/stdout_sinks.cpp",
    ],
    includes = ["include"],
    defines = ["SPDLOG_COMPILED_LIB"],
    visibility = ["//visibility:public"],
)
