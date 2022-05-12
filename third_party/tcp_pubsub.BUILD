cc_library(
    name = "tcp_pubsub",
    srcs = [
        "src/executor.cpp",
        "src/executor_impl.cpp",
        "src/executor_impl.h",
        "src/portable_endian.h",
        "src/protocol_handshake_message.h",
        "src/publisher.cpp",
        "src/publisher_impl.cpp",
        "src/publisher_impl.h",
        "src/publisher_session.cpp",
        "src/publisher_session.h",
        "src/subscriber.cpp",
        "src/subscriber_impl.cpp",
        "src/subscriber_impl.h",
        "src/subscriber_session.cpp",
        "src/subscriber_session_impl.cpp",
        "src/subscriber_session_impl.h",
        "src/tcp_header.h",
        "src/tcp_pubsub_logger_abstraction.h",
    ],
    hdrs = [
        "include/tcp_pubsub/callback_data.h",
        "include/tcp_pubsub/executor.h",
        "include/tcp_pubsub/publisher.h",
        "include/tcp_pubsub/subscriber.h",
        "include/tcp_pubsub/subscriber_session.h",
        "include/tcp_pubsub/tcp_pubsub_logger.h",
        "include/tcp_pubsub_export.h",
        "include/tcp_pubsub_version.h",
    ],
    includes = ["include"],
    visibility = ["//visibility:public"],
    deps = [
        "@asio",
        "@recycle",
    ],
)

genrule(
    name = "tcp_pubsub_version_h",
    outs = ["include/tcp_pubsub_version.h"],
    cmd = "\n".join([
        "cat <<'EOF' >$@",
        "#pragma once",
        "#define TCP_PUBSUB_VERSION_MAJOR 1",
        "#define TCP_PUBSUB_VERSION_MINOR 0",
        "#define TCP_PUBSUB_VERSION_PATCH 0",
        "EOF",
    ]),
)

genrule(
    name = "tcp_pubsub_export_h",
    outs = ["include/tcp_pubsub_export.h"],
    cmd = "\n".join([
        "cat <<'EOF' >$@",
        "#pragma once",
        "#define TCP_PUBSUB_EXPORT",
        "EOF",
    ]),
)
