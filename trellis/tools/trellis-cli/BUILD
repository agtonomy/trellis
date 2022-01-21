cc_binary(
    name = "trellis-cli",
    srcs = [
        "command_handlers.hpp",
        "constants.hpp",
        "main.cpp",
        "monitoring_utils.cpp",
        "monitoring_utils.hpp",
        "node/node_list_main.cpp",
        "node/node_main.cpp",
        "service/service_info_main.cpp",
        "service/service_list_main.cpp",
        "service/service_main.cpp",
        "topic/topic_echo_main.cpp",
        "topic/topic_list_main.cpp",
        "topic/topic_main.cpp",
        "topic/topic_publish_main.cpp",
    ],
    linkstatic = False,
    deps = [
        "@//trellis",
        "@cxxopts",
    ],
)
