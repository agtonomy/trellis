load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")

def trellis_deps():
    maybe(
        http_archive,
        name = "ecal",
        build_file = Label("//third_party:ecal.BUILD"),
        sha256 = "dafcaa1e9088b7e8a715bb3a53ef3cafa8ea72ca9d350476e95bf67199100c4e",
        strip_prefix = "ecal-5.9.2",
        urls = [
            "https://github.com/continental/ecal/archive/refs/tags/v5.9.2.tar.gz",
        ],
    )

    maybe(
        http_archive,
        name = "fmt",
        build_file = Label("//third_party:fmt.BUILD"),
        sha256 = "36016a75dd6e0a9c1c7df5edb98c93a3e77dabcf122de364116efb9f23c6954a",
        strip_prefix = "fmt-8.0.0",
        urls = [
            "https://github.com/fmtlib/fmt/releases/download/8.0.0/fmt-8.0.0.zip",
        ],
    )

    maybe(
        http_archive,
        name = "fmtv6",
        build_file = Label("//third_party:fmt.BUILD"),
        sha256 = "94fea742ddcccab6607b517f6e608b1e5d63d712ddbc5982e44bafec5279881a",
        strip_prefix = "fmt-6.2.1",
        urls = [
            "https://github.com/fmtlib/fmt/releases/download/6.2.1/fmt-6.2.1.zip",
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
        name = "gtest",
        sha256 = "9dc9157a9a1551ec7a7e43daea9a694a0bb5fb8bec81235d8a1e6ef64c716dcb",
        strip_prefix = "googletest-release-1.10.0",
        urls = [
            "https://github.com/google/googletest/archive/release-1.10.0.tar.gz",  # Oct 3, 2019
        ],
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
        sha256 = "cf63d46ef743f4c30b0e36a562caf83cabed3f10e6ca49eb476913c4655394d5",
        strip_prefix = "protobuf-436bd7880e458532901c58f4d9d1ea23fa7edd52",
        urls = [
            "https://storage.googleapis.com/grpc-bazel-mirror/github.com/google/protobuf/archive/436bd7880e458532901c58f4d9d1ea23fa7edd52.tar.gz",
            "https://github.com/google/protobuf/archive/436bd7880e458532901c58f4d9d1ea23fa7edd52.tar.gz",
        ],
    )

    maybe(
        http_archive,
        name = "hdf5",
        build_file = "//third_party:hdf5.BUILD",
        sha256 = "a1b7c2a477090508365d79bb1356d995a90d5c75e9e3ff0f2bd09d54d8a225d0",
        strip_prefix = "hdf5-hdf5-1_10_7",
        urls = [
            "https://github.com/HDFGroup/hdf5/archive/hdf5-1_10_7.tar.gz",  # Oct 16, 2020
        ],
    )
