cc_library(
    name = "variadic_table",
    hdrs = [
        "include/VariadicTable.h",
    ],
    includes = ["include"],
    visibility = ["//visibility:public"],
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
