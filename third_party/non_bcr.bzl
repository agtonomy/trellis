load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _non_bcr_impl(_ctx):
    http_archive(
        name = "simpleini",
        build_file = Label("//third_party:simpleini.BUILD"),
        sha256 = "14e5bc1cb318ed374d45d6faf48da0b79db7e069c12ec6e090523b8652ef47c7",
        strip_prefix = "simpleini-4.17",
        urls = ["https://github.com/brofield/simpleini/archive/refs/tags/4.17.tar.gz"],
    )

non_bcr = module_extension(implementation = _non_bcr_impl)
