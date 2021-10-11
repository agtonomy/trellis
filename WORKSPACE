workspace(name = "trellis")

# Download all of trellis dependencies
load("//third_party:repositories.bzl", "trellis_deps")

trellis_deps()

# Required transitive loader for protobuf dependencies.
# TODO(curtismuntz): Investigate if this can be wrapped into `trellis_deps`.
load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")

protobuf_deps()
