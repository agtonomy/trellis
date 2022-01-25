load("@rules_proto//proto:defs.bzl", "proto_library")

cc_library(
    name = "threading_utils",
    srcs = [
        "lib/ThreadingUtils/include/ThreadingUtils/DynamicSleeper.h",
        "lib/ThreadingUtils/include/ThreadingUtils/InterruptibleLoopThread.h",
        "lib/ThreadingUtils/include/ThreadingUtils/InterruptibleThread.h",
        "lib/ThreadingUtils/include/ThreadingUtils/ThreadSafeQueue.h",
    ],
    includes = [
        "lib/ThreadingUtils/include",
    ],
)

cc_library(
    name = "ecal_utils",
    srcs = [
        "lib/ecal_utils/include/ecal_utils/ecal_utils.h",
        "lib/ecal_utils/include/ecal_utils/filesystem.h",
        "lib/ecal_utils/include/ecal_utils/string.h",
        "lib/ecal_utils/src/filesystem.cpp",
    ],
    includes = [
        "lib/ecal_utils/include",
    ],
)

cc_library(
    name = "ecal_parser",
    srcs = [
        "lib/EcalParser/include/EcalParser/EcalParser.h",
        "lib/EcalParser/include/EcalParser/Function.h",
        "lib/EcalParser/src/EcalParser.cpp",
        "lib/EcalParser/src/functions/env.cpp",
        "lib/EcalParser/src/functions/env.h",
        "lib/EcalParser/src/functions/hostname.cpp",
        "lib/EcalParser/src/functions/hostname.h",
        "lib/EcalParser/src/functions/os.cpp",
        "lib/EcalParser/src/functions/os.h",
        "lib/EcalParser/src/functions/osselect.cpp",
        "lib/EcalParser/src/functions/osselect.h",
        "lib/EcalParser/src/functions/time.cpp",
        "lib/EcalParser/src/functions/time.h",
        "lib/EcalParser/src/functions/username.cpp",
        "lib/EcalParser/src/functions/username.h",
    ],
    includes = [
        "lib/EcalParser/include",
    ],
    deps = [
        ":ecal_utils",
    ],
)

cc_library(
    name = "custom_tclap",
    srcs = [
        "lib/CustomTclap/include/custom_tclap/advanced_tclap_output.h",
        "lib/CustomTclap/include/custom_tclap/fuzzy_duo_value_arg_unsigned_longlong_string.h",
        "lib/CustomTclap/include/custom_tclap/fuzzy_value_switch_arg_bool.h",
        "lib/CustomTclap/include/custom_tclap/fuzzy_value_switch_arg_double.h",
        "lib/CustomTclap/include/custom_tclap/fuzzy_value_switch_arg_unsigned_longlong.h",
        "lib/CustomTclap/src/advanced_tclap_output.cpp",
        "lib/CustomTclap/src/fuzzy_duo_value_arg_unsigned_longlong_string.cpp",
        "lib/CustomTclap/src/fuzzy_value_switch_arg_bool.cpp",
        "lib/CustomTclap/src/fuzzy_value_switch_arg_double.cpp",
        "lib/CustomTclap/src/fuzzy_value_switch_arg_unsigned_longlong.cpp",
    ],
    includes = [
        "lib/CustomTclap/include",
    ],
    deps = [
        "@tclap",
    ],
)

cc_library(
    name = "ecal_hdf5",
    srcs = [
        "contrib/ecalhdf5/include/ecal/measurement/imeasurement.h",
        "contrib/ecalhdf5/include/ecal/measurement/measurement.h",
        "contrib/ecalhdf5/include/ecal/measurement/omeasurement.h",
        "contrib/ecalhdf5/include/ecalhdf5/eh5_defs.h",
        "contrib/ecalhdf5/include/ecalhdf5/eh5_meas.h",
        "contrib/ecalhdf5/include/ecalhdf5/eh5_types.h",
        "contrib/ecalhdf5/src/eh5_meas.cpp",
        "contrib/ecalhdf5/src/eh5_meas_dir.cpp",
        "contrib/ecalhdf5/src/eh5_meas_dir.h",
        "contrib/ecalhdf5/src/eh5_meas_file_v1.cpp",
        "contrib/ecalhdf5/src/eh5_meas_file_v1.h",
        "contrib/ecalhdf5/src/eh5_meas_file_v2.cpp",
        "contrib/ecalhdf5/src/eh5_meas_file_v2.h",
        "contrib/ecalhdf5/src/eh5_meas_file_v3.cpp",
        "contrib/ecalhdf5/src/eh5_meas_file_v3.h",
        "contrib/ecalhdf5/src/eh5_meas_file_v4.cpp",
        "contrib/ecalhdf5/src/eh5_meas_file_v4.h",
        "contrib/ecalhdf5/src/eh5_meas_file_v5.cpp",
        "contrib/ecalhdf5/src/eh5_meas_file_v5.h",
        "contrib/ecalhdf5/src/eh5_meas_impl.h",
        "contrib/ecalhdf5/src/eh5_util.h",
        "contrib/ecalhdf5/src/escape.cpp",
        "contrib/ecalhdf5/src/escape.h",
    ],
    includes = [
        "contrib/ecalhdf5/include",
    ],
    deps = [
        ":ecal_utils",
        "@hdf5",
    ],
)

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
        ],
        exclude = [
            "ecal/**/win32/**",
            "ecal/**/*iceoryx*/**",
            "ecal/core/src/ecal_process_stub.cpp",
        ],
    ) + ["ecal/core/include/ecal/ecal_defs.h"],
    hdrs = glob([
        "ecal/core/include/**",
        "contrib/ecalproto/include/**",
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
    ],
    linkopts = [
        "-ldl",
        "-lrt",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":custom_tclap",
        ":ecal_cc_proto",
        ":ecal_utils",
        ":ecaltime-localtime",
        "@asio",
        "@simpleini",
    ],
)

cc_library(
    name = "rec_client_core",
    srcs = [
        "app/rec/rec_client_core/include/rec_client_core/ecal_rec.h",
        "app/rec/rec_client_core/include/rec_client_core/ecal_rec_defs.h",
        "app/rec/rec_client_core/include/rec_client_core/ecal_rec_logger.h",
        "app/rec/rec_client_core/include/rec_client_core/job_config.h",
        "app/rec/rec_client_core/include/rec_client_core/proto_helpers.h",
        "app/rec/rec_client_core/include/rec_client_core/rec_error.h",
        "app/rec/rec_client_core/include/rec_client_core/record_mode.h",
        "app/rec/rec_client_core/include/rec_client_core/state.h",
        "app/rec/rec_client_core/include/rec_client_core/topic_info.h",
        "app/rec/rec_client_core/include/rec_client_core/upload_config.h",
        "app/rec/rec_client_core/src/addons/addon.cpp",
        "app/rec/rec_client_core/src/addons/addon.h",
        "app/rec/rec_client_core/src/addons/addon_manager.cpp",
        "app/rec/rec_client_core/src/addons/addon_manager.h",
        "app/rec/rec_client_core/src/addons/common_types.h",
        "app/rec/rec_client_core/src/addons/concurrent_queue.h",
        "app/rec/rec_client_core/src/addons/function_descriptors.h",
        "app/rec/rec_client_core/src/addons/pipe_handler.cpp",
        "app/rec/rec_client_core/src/addons/pipe_handler.h",
        "app/rec/rec_client_core/src/addons/response_handler.cpp",
        "app/rec/rec_client_core/src/addons/response_handler.h",
        "app/rec/rec_client_core/src/ecal_rec.cpp",
        "app/rec/rec_client_core/src/ecal_rec_impl.cpp",
        "app/rec/rec_client_core/src/ecal_rec_impl.h",
        "app/rec/rec_client_core/src/frame.h",
        "app/rec/rec_client_core/src/garbage_collector_trigger_thread.cpp",
        "app/rec/rec_client_core/src/garbage_collector_trigger_thread.h",
        "app/rec/rec_client_core/src/job/ftp_upload_thread.cpp",
        "app/rec/rec_client_core/src/job/ftp_upload_thread.h",
        "app/rec/rec_client_core/src/job/hdf5_writer_thread.cpp",
        "app/rec/rec_client_core/src/job/hdf5_writer_thread.h",
        "app/rec/rec_client_core/src/job/record_job.cpp",
        "app/rec/rec_client_core/src/job/record_job.h",
        "app/rec/rec_client_core/src/job_config.cpp",
        "app/rec/rec_client_core/src/monitoring_thread.cpp",
        "app/rec/rec_client_core/src/monitoring_thread.h",
        "app/rec/rec_client_core/src/proto_helpers.cpp",
    ],
    includes = [
        "app/rec/rec_client_core/include",
        "app/rec/rec_client_core/src",
    ],
    deps = [
        ":ecal",
        ":ecal_hdf5",
        ":ecal_parser",
        ":threading_utils",
    ],
)

cc_library(
    name = "play_core",
    srcs = [
        "app/play/play_core/include/continuity_report.h",
        "app/play/play_core/include/ecal_play.h",
        "app/play/play_core/include/ecal_play_globals.h",
        "app/play/play_core/include/ecal_play_logger.h",
        "app/play/play_core/include/ecal_play_scenario.h",
        "app/play/play_core/include/ecal_play_state.h",
        "app/play/play_core/src/ecal_play.cpp",
        "app/play/play_core/src/ecal_play_command.h",
        "app/play/play_core/src/measurement_container.cpp",
        "app/play/play_core/src/measurement_container.h",
        "app/play/play_core/src/play_thread.cpp",
        "app/play/play_core/src/play_thread.h",
        "app/play/play_core/src/state_publisher_thread.cpp",
        "app/play/play_core/src/state_publisher_thread.h",
        "app/play/play_core/src/stop_watch.cpp",
        "app/play/play_core/src/stop_watch.h",
    ],
    includes = [
        "app/play/play_core/include",
    ],
    deps = [
        ":ecal",
        ":threading_utils",
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

cc_binary(
    name = "rec_client_cli",
    srcs = [
        "app/rec/rec_client_cli/src/ecal_rec_cli.cpp",
        "app/rec/rec_client_cli/src/ecal_rec_service.cpp",
        "app/rec/rec_client_cli/src/ecal_rec_service.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":rec_client_core",
        ":threading_utils",
        "@fmtv6//:fmt",
    ],
)

cc_binary(
    name = "play_cli",
    srcs = [
        "app/play/play_cli/src/convert_utf.cpp",
        "app/play/play_cli/src/convert_utf.h",
        "app/play/play_cli/src/ecal_play_cli.cpp",
        "app/play/play_cli/src/ecal_play_service.cpp",
        "app/play/play_cli/src/ecal_play_service.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":ecal_hdf5",
        ":play_core",
        "@fmtv6//:fmt",
        "@termcolor",
    ],
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
        "#define ECAL_INSTALL_LIB_DIR \"\"",
        "#endif // ecal_defs_h_included",
        "EOF",
    ]),
)
