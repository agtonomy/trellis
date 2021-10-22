load("@rules_proto//proto:defs.bzl", "proto_library")

cc_library(
    name = "ecal",
    srcs = glob(
        [
            "contrib/ecalproto/src/*.cpp",
            "ecal/core/**/*.h",
            "ecal/core/**/*.cpp",
            "ecal/*.h",
            "ecal/core/src/service/*.h",
            "ecal/core/src/*.h",
            "lib/CustomTclap/src/**",
        ],
        exclude = [
            "ecal/**/win32/**",
            "ecal/**/*iceoryx*/**",
            "ecal/core/src/ecal_process_stub.cpp",
        ],
    ) + ["ecal/core/include/ecal/ecal_defs.h"],
    hdrs = glob([
        "ecal/core/include/**",
        "lib/ecal_utils/include/**",
        "contrib/ecalproto/include/**",
        "lib/CustomTclap/include/**",
        "app/apps/include/**",
    ]),
    copts = [
        "-Iexternal/ecal/ecal/core/src",
    ],
    defines = [
        "ECAL_THIRDPARTY_BUILD_SPDLOG=OFF",
        "ECAL_THIRDPARTY_BUILD_TINYXML2=OFF",
        "ECAL_THIRDPARTY_BUILD_FINEFTP=OFF",
        "ECAL_THIRDPARTY_BUILD_TERMCOLOR=OFF",
        "ECAL_THIRDPARTY_BUILD_CURL=OFF",
        "ECAL_THIRDPARTY_BUILD_GTEST=OFF",
        "ECAL_THIRDPARTY_BUILD_HDF5=OFF",
        "HAS_QT5=OFF",
        "HAS_HDF5=OFF",
        "HAS_CURL=OFF",
        "BUILD_APPS=OFF",
        "BUILD_SAMPLES=OFF",
        "BUILD_TIME=OFF",
        "ECAL_INSTALL_SAMPLE_SOURCES=OFF",
    ],
    includes = [
        "app/apps/include",
        "contrib/ecalproto/include",
        "ecal/core/include",
        "lib/CustomTclap/include",
        "lib/ecal_utils/include",
    ],
    linkopts = [
        "-ldl",
        "-lrt",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":ecal_cc_proto",
        ":ecaltime-localtime",
        "@asio",
        "@simpleini",
        "@tclap",
    ],
)

cc_library(
    name = "ecaltime-localtime",
    srcs = ["libecaltime-localtime.so"],
)

cc_binary(
    name = "libecaltime-localtime.so",
    srcs = [
        "contrib/ecaltime/include/ecaltime.h",
        "contrib/ecaltime/localtime/src/ecaltime.cpp",
    ],
    includes = [
        "contrib/ecaltime/include",
    ],
    linkshared = True,
)

proto_library(
    name = "ecal_proto",
    srcs = glob(["ecal/**/*.proto"]),
    strip_import_prefix = "ecal/pb/src",
)

cc_proto_library(
    name = "ecal_cc_proto",
    deps = [":ecal_proto"],
)

genrule(
    name = "ecal_defs_h",
    outs = ["ecal/core/include/ecal/ecal_defs.h"],
    cmd = "\n".join([
        "cat <<'EOF' >$@",
        "#ifndef ecal_defs_h_included",
        "#define ecal_defs_h_included",
        "#define ECAL_VERSION_MAJOR (5)",
        "#define ECAL_VERSION_MINOR (9)",
        "#define ECAL_VERSION_PATCH (0)",
        "#define ECAL_VERSION \"v5.9.0\"",
        "#define ECAL_DATE \"\"",
        "#define ECAL_PLATFORMTOOLSET \"\"",
        "#define ECAL_INSTALL_CONFIG_DIR \"/etc/ecal\"",
        "#define ECAL_INSTALL_PREFIX \"\"",
        "#endif // ecal_defs_h_included",
        "EOF",
    ]),
)
