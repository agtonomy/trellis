load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")

def trellis_deps():
    maybe(
        http_archive,
        name = "ecal",
        build_file = Label("//third_party:ecal.BUILD"),
        sha256 = "b324a866f57ce139344bd529402c46ab1cc37a4c7d5ff832db80e14496038dce",
        strip_prefix = "ecal-5.10.4",
        urls = [
            "https://github.com/eclipse-ecal/ecal/archive/refs/tags/v5.10.4.tar.gz",
        ],
    )

    maybe(
        http_archive,
        name = "fmt",
        build_file = Label("//third_party:fmt.BUILD"),
        sha256 = "fc96dd2d2fdf2bded630787adba892c23cb9e35c6fd3273c136b0c57d4651ad6",
        strip_prefix = "fmt-9.0.0",
        urls = [
            "https://github.com/fmtlib/fmt/releases/download/9.0.0/fmt-9.0.0.zip",
        ],
    )

    maybe(
        http_archive,
        name = "yaml-cpp",
        sha256 = "43e6a9fcb146ad871515f0d0873947e5d497a1c9c60c58cb102a97b47208b7c3",
        strip_prefix = "yaml-cpp-yaml-cpp-0.7.0",
        urls = [
            "https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-0.7.0.tar.gz",
        ],
    )

    maybe(
        http_archive,
        name = "cxxopts",
        build_file = Label("//third_party:cxxopts.BUILD"),
        sha256 = "984aa3c8917d649b14d7f6277104ce38dd142ce378a9198ec926f03302399681",
        strip_prefix = "cxxopts-2.2.1",
        urls = [
            "https://github.com/jarro2783/cxxopts/archive/refs/tags/v2.2.1.tar.gz",
        ],
    )

    maybe(
        http_archive,
        name = "tclap",
        build_file = Label("//third_party:tclap.BUILD"),
        sha256 = "7363f8f571e6e733b269c4b4e9c18f392d3cd7240d39a379d95de5a4c4bdc47f",
        strip_prefix = "tclap-1.2.4",
        urls = [
            "https://github.com/xguerin/tclap/archive/refs/tags/v1.2.4.tar.gz",
        ],
    )

    maybe(
        http_archive,
        name = "asio",
        build_file = Label("//third_party:asio.BUILD"),
        sha256 = "5ee191aee825dfb1325cbacf643d599b186de057c88464ea98f1bae5ba4ff47a",
        strip_prefix = "asio-asio-1-19-2",
        urls = [
            "https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-19-2.tar.gz",
        ],
    )

    maybe(
        http_archive,
        name = "com_google_googletest",
        sha256 = "81964fe578e9bd7c94dfdb09c8e4d6e6759e19967e397dbea48d1c10e45d0df2",
        strip_prefix = "googletest-release-1.12.1",
        urls = ["https://github.com/google/googletest/archive/refs/tags/release-1.12.1.tar.gz"],
    )

    maybe(
        http_archive,
        name = "simpleini",
        build_file = Label("//third_party:simpleini.BUILD"),
        sha256 = "14e5bc1cb318ed374d45d6faf48da0b79db7e069c12ec6e090523b8652ef47c7",
        strip_prefix = "simpleini-4.17",
        urls = [
            "https://github.com/brofield/simpleini/archive/refs/tags/4.17.tar.gz",
        ],
    )

    maybe(
        http_archive,
        name = "com_google_protobuf",
        sha256 = "3bd7828aa5af4b13b99c191e8b1e884ebfa9ad371b0ce264605d347f135d2568",
        strip_prefix = "protobuf-3.19.4",
        urls = [
            "https://github.com/protocolbuffers/protobuf/archive/v3.19.4.tar.gz",
        ],
    )

    maybe(
        http_archive,
        name = "hdf5",
        build_file = Label("//third_party:hdf5.BUILD"),
        sha256 = "7a1a0a54371275ce2dfc5cd093775bb025c365846512961e7e5ceaecb437ef15",
        strip_prefix = "hdf5-1.10.7",
        urls = [
            "https://hdf-wordpress-1.s3.amazonaws.com/wp-content/uploads/manual/HDF5/HDF5_1_10_7/src/hdf5-1.10.7.tar.gz",  # Oct 16, 2020
        ],
    )

    maybe(
        http_archive,
        name = "termcolor",
        build_file = Label("//third_party:termcolor.BUILD"),
        sha256 = "4a73a77053822ca1ed6d4a2af416d31028ec992fb0ffa794af95bd6216bb6a20",
        strip_prefix = "termcolor-2.0.0",
        urls = [
            "https://github.com/ikalnytskyi/termcolor/archive/refs/tags/v2.0.0.tar.gz",
        ],
    )

    maybe(
        http_archive,
        name = "spdlog",
        build_file = Label("//third_party:spdlog.BUILD"),
        sha256 = "6fff9215f5cb81760be4cc16d033526d1080427d236e86d70bb02994f85e3d38",
        strip_prefix = "spdlog-1.9.2",
        urls = [
            "https://github.com/gabime/spdlog/archive/refs/tags/v1.9.2.tar.gz",
        ],
    )

    maybe(
        http_archive,
        name = "json",
        build_file = Label("//third_party:json.BUILD"),
        sha256 = "61e605be15e88deeac4582aaf01c09d616f8302edde7adcaba9261ddc3b4ceca",
        strip_prefix = "single_include",
        urls = [
            "https://github.com/nlohmann/json/releases/download/v3.10.2/include.zip",
        ],
    )

    # New eCAL dependency as of v5.10.0
    maybe(
        http_archive,
        name = "tcp_pubsub",
        build_file = Label("//third_party:tcp_pubsub.BUILD"),
        sha256 = "f03245e8878f215e9c852b35f30d90d111e250ddecea75ce0be2619583dbe052",
        strip_prefix = "tcp_pubsub-1.0.3/tcp_pubsub",
        urls = [
            "https://github.com/eclipse-ecal/tcp_pubsub/archive/refs/tags/v1.0.3.tar.gz",
        ],
    )

    # Submodule of tcp_pubsub
    maybe(
        http_archive,
        name = "recycle",
        build_file = Label("//third_party:recycle.BUILD"),
        sha256 = "d1cf8a5256110c068f366b0e4e16ad39427b9def13876670aad9f167afd7aaee",
        strip_prefix = "recycle-c5425709b2273ef6371647247d1a1d86aa75c2e6",
        urls = [
            "https://github.com/steinwurf/recycle/archive/c5425709b2273ef6371647247d1a1d86aa75c2e6.tar.gz",
        ],
    )

    maybe(
        http_archive,
        name = "rules_pkg",
        sha256 = "62eeb544ff1ef41d786e329e1536c1d541bb9bcad27ae984d57f18f314018e66",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/rules_pkg/releases/download/0.6.0/rules_pkg-0.6.0.tar.gz",
            "https://github.com/bazelbuild/rules_pkg/releases/download/0.6.0/rules_pkg-0.6.0.tar.gz",
        ],
    )
    maybe(
        http_archive,
        name = "variadic_table",
        build_file = Label("//third_party:variadic_table.BUILD"),
        sha256 = "6799c0ee507fb3c739bde936630fc826f3c13abeb7b3245ebf997a6446fd0cb3",
        strip_prefix = "variadic_table-82fcf65c00c70afca95f71c0c77fba1982a20a86",
        urls = [
            "https://github.com/friedmud/variadic_table/archive/82fcf65c00c70afca95f71c0c77fba1982a20a86.tar.gz",
        ],
    )
    maybe(
        http_archive,
        name = "eigen",
        build_file = Label("//third_party:eigen.BUILD"),
        sha256 = "8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72",
        strip_prefix = "eigen-3.4.0",
        urls = [
            "https://github.com/agtonomy/eigen/archive/refs/tags/3.4.0.tar.gz",
        ],
    )
    maybe(
        http_archive,
        name = "mcap",
        build_file = Label("//third_party:mcap.BUILD"),
        sha256 = "2833f72344308ea58639f3b363a0cf17669580ae7ab435f43f3b104cff6ef548",
        strip_prefix = "mcap-releases-cpp-v0.8.0",
        urls = ["https://github.com/foxglove/mcap/archive/refs/tags/releases/cpp/v0.8.0.tar.gz"],
    )
    maybe(
        http_archive,
        name = "lz4",
        build_file = Label("//third_party:lz4.BUILD"),
        sha256 = "0b0e3aa07c8c063ddf40b082bdf7e37a1562bda40a0ff5272957f3e987e0e54b",
        strip_prefix = "lz4-1.9.4",
        urls = ["https://github.com/lz4/lz4/archive/refs/tags/v1.9.4.tar.gz"],
    )
    maybe(
        http_archive,
        name = "zstd",
        build_file = Label("//third_party:zstd.BUILD"),
        sha256 = "7c42d56fac126929a6a85dbc73ff1db2411d04f104fae9bdea51305663a83fd0",
        strip_prefix = "zstd-1.5.2",
        urls = ["https://github.com/facebook/zstd/releases/download/v1.5.2/zstd-1.5.2.tar.gz"],
    )
