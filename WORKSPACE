workspace(name = "trellis")

# Download all of trellis dependencies
load("//third_party:repositories.bzl", "trellis_deps")

trellis_deps()

# Required transitive loader for protobuf dependencies.
# TODO(curtismuntz): Investigate if this can be wrapped into `trellis_deps`.
load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")

protobuf_deps()

# Remove after we switch to rules_python's py_proto_library:
load("@com_github_grpc_grpc//bazel:grpc_deps.bzl", "grpc_deps")

grpc_deps()

load("@rules_pkg//:deps.bzl", "rules_pkg_dependencies")

rules_pkg_dependencies()
