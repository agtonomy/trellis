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
        "lib/ecal_utils/include/ecal_utils/command_line.h",
        "lib/ecal_utils/include/ecal_utils/ecal_utils.h",
        "lib/ecal_utils/include/ecal_utils/filesystem.h",
        "lib/ecal_utils/include/ecal_utils/portable_endian.h",
        "lib/ecal_utils/include/ecal_utils/str_convert.h",
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
    copts = [
        "--std=c++17",
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
        "contrib/ecalhdf5/src/eh5_meas_file_writer_v5.cpp",
        "contrib/ecalhdf5/src/eh5_meas_file_writer_v5.h",
        "contrib/ecalhdf5/src/eh5_meas_impl.h",
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
            "ecal/core/src/io/udp_receiver_npcap.h",  # avoiding pcap deps
            "ecal/core/src/io/udp_receiver_npcap.cpp",  # avoiding pcap deps
        ],
    ) + ["ecal/core/include/ecal/ecal_defs.h"],
    hdrs = glob([
        "ecal/core/include/**",
        "contrib/ecalproto/include/**",
        "app/apps/include/**",
    ]),
    copts = [
        "-Iexternal/ecal/ecal/core/src",
        "--std=c++17",
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
        ":ecal_core_cc_proto",
        ":ecal_utils",
        ":libecaltime-localtime.so",
        "@asio",
        "@simpleini",
        "@tcp_pubsub",
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
        "app/rec/rec_client_core/src/frame_buffer.cpp",
        "app/rec/rec_client_core/src/frame_buffer.h",
        "app/rec/rec_client_core/src/garbage_collector_trigger_thread.cpp",
        "app/rec/rec_client_core/src/garbage_collector_trigger_thread.h",
        #"app/rec/rec_client_core/src/job/ftp_upload_thread.cpp", # disabling due to curl dependency
        #"app/rec/rec_client_core/src/job/ftp_upload_thread.h", # disabling due to curl dependency
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
    visibility = ["//visibility:public"],
    deps = [
        ":ecal",
        ":ecal_app_cc_proto",
        ":ecal_hdf5",
        ":ecal_parser",
        ":threading_utils",
        "@spdlog",
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
        ":ecal_app_cc_proto",
        ":ecal_hdf5",
        ":sim_time_cc_proto",
        ":threading_utils",
        "@spdlog",
    ],
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

cc_library(
    name = "rec_client_cli",
    srcs = [
        "app/rec/rec_client_cli/src/ecal_rec_cli.cpp",
        "app/rec/rec_client_cli/src/ecal_rec_service.cpp",
        "app/rec/rec_client_cli/src/ecal_rec_service.h",
    ],
    copts = [
        "--std=c++17",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":ecal_app_cc_proto",
        ":rec_client_core",
        ":threading_utils",
    ],
)

cc_library(
    name = "play_cli",
    srcs = [
        "app/play/play_cli/src/ecal_play_cli.cpp",
        "app/play/play_cli/src/ecal_play_service.cpp",
        "app/play/play_cli/src/ecal_play_service.h",
    ],
    copts = [
        "--std=c++17",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":ecal_app_cc_proto",
        ":play_core",
        "@termcolor",
    ],
)

cc_library(
    name = "mma",
    srcs = [
        "contrib/mma/include/interruptable_timer.h",
        "contrib/mma/include/linux/mma_linux.h",
        "contrib/mma/include/linux/pipe_refresher.h",
        "contrib/mma/include/linux/ressource.h",
        "contrib/mma/include/logger.h",
        "contrib/mma/include/mma.h",
        "contrib/mma/include/mma_defs.h",
        "contrib/mma/include/mma_impl.h",
        "contrib/mma/include/query_manager.h",
        "contrib/mma/include/zombie_instance_killer.h",
        "contrib/mma/src/linux/mma_linux.cpp",
        "contrib/mma/src/linux/pipe_refresher.cpp",
        "contrib/mma/src/logger.cpp",
        "contrib/mma/src/mma.cpp",
        "contrib/mma/src/mma_application.cpp",
        "contrib/mma/src/query_manager.cpp",
        "contrib/mma/src/zombie_instance_killer.cpp",
    ],
    includes = [
        "contrib/mma/include",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":ecal",
        ":ecal_app_cc_proto",
        ":threading_utils",
    ],
)

proto_library(
    name = "ecal_core_proto",
    srcs = glob([
        "ecal/core_pb/src/ecal/core/pb/*.proto",
    ]),
    strip_import_prefix = "ecal/core_pb/src",
)

proto_library(
    name = "ecal_app_proto",
    srcs = glob([
        "app/app_pb/src/ecal/app/pb/mma/*.proto",
        "app/app_pb/src/ecal/app/pb/play/*.proto",
        "app/app_pb/src/ecal/app/pb/rec/*.proto",
        "app/app_pb/src/ecal/app/pb/sys/*.proto",
    ]),
    strip_import_prefix = "app/app_pb/src",
)

proto_library(
    name = "sim_time_proto",
    srcs = ["contrib/ecaltime/ecaltime_pb/src/ecal/ecaltime/pb/sim_time.proto"],
    strip_import_prefix = "contrib/ecaltime/ecaltime_pb/src",
)

cc_proto_library(
    name = "ecal_core_cc_proto",
    deps = [":ecal_core_proto"],
)

cc_proto_library(
    name = "ecal_app_cc_proto",
    deps = [":ecal_app_proto"],
)

cc_proto_library(
    name = "sim_time_cc_proto",
    deps = [":sim_time_proto"],
)

genrule(
    name = "ecal_defs_h",
    outs = ["ecal/core/include/ecal/ecal_defs.h"],
    cmd = "\n".join([
        "cat <<'EOF' >$@",
        "#ifndef ecal_defs_h_included",
        "#define ecal_defs_h_included",
        "#define ECAL_VERSION_MAJOR (5)",
        "#define ECAL_VERSION_MINOR (12)",
        "#define ECAL_VERSION_PATCH (0)",
        "#define ECAL_VERSION_CALCULATE(major, minor, patch)   (((major)<<16)|((minor)<<8)|(patch))",
        "#define ECAL_VERSION_INTEGER        ECAL_VERSION_CALCULATE(ECAL_VERSION_MAJOR, ECAL_VERSION_MINOR, ECAL_VERSION_PATCH)",
        "#define ECAL_VERSION \"v5.12.0\"",
        "#define ECAL_DATE \"\"",
        "#define ECAL_PLATFORMTOOLSET \"\"",
        "#define ECAL_INSTALL_CONFIG_DIR \"/etc/ecal\"",
        "#define ECAL_INSTALL_PREFIX \"\"",
        "#define ECAL_INSTALL_LIB_DIR \"\"",
        "#endif // ecal_defs_h_included",
        "EOF",
    ]),
)

cc_library(
    name = "sys_client_core",
    srcs = [
        "app/sys/sys_client_core/include/sys_client_core/ecal_sys_client.h",
        "app/sys/sys_client_core/include/sys_client_core/ecal_sys_client_defs.h",
        "app/sys/sys_client_core/include/sys_client_core/ecal_sys_client_logger.h",
        "app/sys/sys_client_core/include/sys_client_core/proto_helpers.h",
        "app/sys/sys_client_core/include/sys_client_core/runner.h",
        "app/sys/sys_client_core/include/sys_client_core/task.h",
        "app/sys/sys_client_core/src/ecal_sys_client.cpp",
        "app/sys/sys_client_core/src/proto_helpers.cpp",
        "app/sys/sys_client_core/src/task.cpp",
    ],
    includes = [
        "app/sys/sys_client_core/include",
        "app/sys/sys_client_core/src",
    ],
    deps = [
        ":ecal",
        ":ecal_app_cc_proto",
        ":ecal_parser",
        "@spdlog",
    ],
)

cc_library(
    name = "sys_client_cli",
    srcs = [
        "app/sys/sys_client_cli/src/ecal_sys_client_cli.cpp",
        "app/sys/sys_client_cli/src/ecal_sys_client_service.cpp",
        "app/sys/sys_client_cli/src/ecal_sys_client_service.h",
    ],
    copts = [
        "--std=c++17",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "ecal_app_cc_proto",
        ":sys_client_core",
    ],
)
