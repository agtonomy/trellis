workspace(name = "trellis")

# Download all of trellis dependencies
load("//third_party:repositories.bzl", "trellis_deps")

trellis_deps()

# Required transitive loader for protobuf dependencies.
# TODO(curtismuntz): Investigate if this can be wrapped into `trellis_deps`.
load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")

protobuf_deps()

# @rules_python pulled in by @com_google_protobuf
load("@rules_python//python:repositories.bzl", "py_repositories")

py_repositories()

load("@rules_pkg//:deps.bzl", "rules_pkg_dependencies")

rules_pkg_dependencies()
