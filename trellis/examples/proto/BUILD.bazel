proto_library(
    name = "proto_lib",
    srcs = [
        "addition_service.proto",
        "hello_world.proto",
    ],
)

cc_proto_library(
    name = "proto",
    visibility = ["//visibility:public"],
    deps = [":proto_lib"],
)
