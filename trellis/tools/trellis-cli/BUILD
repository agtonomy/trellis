cc_binary(
    name = "trellis-cli",
    srcs = [
        "command_handlers.hpp",
        "main.cpp",
        "monitoring_utils.cpp",
        "monitoring_utils.hpp",
        "topic/topic_main.cpp",
        "topic/topic_echo_main.cpp",
        "topic/topic_publish_main.cpp",
        "topic/topic_list_main.cpp",
    ],
    linkstatic = False,
    deps = [
        "@cxxopts",
        "@trellis//trellis",
    ],
)
