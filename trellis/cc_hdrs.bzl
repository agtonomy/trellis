"""cc_hdrs_filegroup: expose a cc_library's raw hdrs files as a filegroup.

Used by //trellis:BUILD.bazel to repackage headers from BCR modules (asio, fmt,
yaml-cpp) into libtrellis_tarball. BCR modules only expose cc_library targets,
not standalone header filegroups; pulling the hdrs straight out of CcInfo gives
us the _virtual_includes symlinks (which embed strip_include_prefix), not the
on-disk source files we want to ship. An aspect lets us reach the cc_library's
`hdrs` attribute directly.
"""

_HdrsInfo = provider("cc_library hdrs files", fields = ["hdrs"])

def _hdrs_aspect_impl(target, ctx):
    hdrs = []
    if hasattr(ctx.rule.attr, "hdrs"):
        for h in ctx.rule.attr.hdrs:
            hdrs.extend(h.files.to_list())
    return [_HdrsInfo(hdrs = depset(hdrs))]

_hdrs_aspect = aspect(
    implementation = _hdrs_aspect_impl,
)

def _cc_hdrs_filegroup_impl(ctx):
    files = []
    for dep in ctx.attr.deps:
        files.extend(dep[_HdrsInfo].hdrs.to_list())
    return [DefaultInfo(files = depset(files))]

cc_hdrs_filegroup = rule(
    implementation = _cc_hdrs_filegroup_impl,
    attrs = {
        "deps": attr.label_list(
            aspects = [_hdrs_aspect],
            providers = [CcInfo],
            mandatory = True,
        ),
    },
    doc = "Exposes a cc_library's raw hdrs files as a filegroup's default files.",
)
