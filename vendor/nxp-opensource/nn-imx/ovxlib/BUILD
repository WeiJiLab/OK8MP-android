# Description:
#      VSI OVX wrapper logic

package(
    default_visibility = ["//visibility:public"],
    features = ["-parse_headers"],
)

load("@bazel_tools//tools/build_defs/pkg:pkg.bzl", "pkg_tar")

filegroup(
    name = "all_files",
    srcs = glob(
        ["**/*"],
        exclude = [
            "**/METADATA",
            "**/OWNERS",
            "external/*"
        ],
    ),
)

filegroup(
    name = "nnapi_0_3_hdrs",
    srcs = [
        "include/platform/vsi_nn_pf_softmax.h",
        "include/platform/vsi_nn_pf_depth2space.h",
    ],
)

filegroup(
    name = "kernel_hdrs",
    srcs = glob([
        "include/kernel/cl/*.h",
        "include/kernel/evis/*.h",
        "include/kernel/cpu/*.h",
    ])
)

filegroup(
    name = "kernel_srcs",
    srcs = glob([
        "src/kernel/cl/*.c",
        "src/kernel/evis/*.c",
        "src/kernel/cpu/*.c",
        "src/kernel/vx/*.c",
    ])
)

filegroup(
    name = "operation_srcs",
    srcs = glob([
        "src/ops/*.c",
    ])
)

filegroup(
    name = "operation_hdrs",
    srcs = glob([
        "include/ops/*.h",
    ])
)

filegroup(
    name = "custom_hdrs",
    srcs = glob([
        "include/custom/ops/*.h",
    ]) + [
        #custom
        "include/custom/custom_node_type.def",
        "include/custom/custom_ops.def",
        "include/custom/vsi_nn_custom_node_type.h",
    ]
)

filegroup(
    name = "custom_srcs",
    srcs = glob([
        "src/custom/ops/*.c",
        "src/custom/ops/kernel/*.c",
    ])
)

filegroup(
    name = "nnapi_0_3_srcs",
    srcs = [
        "src/platform/nnapi0.3/vsi_nn_pf_softmax.c",
    ],
)

filegroup(
    name = "nnapi_0_4_hdrs",
    srcs = [
        "include/platform/vsi_nn_pf_softmax.h",
        "include/platform/vsi_nn_pf_depth2space.h",
    ],
)

filegroup(
    name = "nnapi_0_4_srcs",
    srcs = [
        "src/platform/nnapi0.4/vsi_nn_pf_softmax.c",
        "src/platform/nnapi0.4/vsi_nn_pf_depth2space.c",
    ],
)

cc_binary(
    name = "ovxlib.so",
    linkshared = True,
    linkstatic = False,
    deps = [ "ovxlib" ],
)

pkg_tar(
    name = "ovxlib.so_pkg",
    srcs = [
        "ovxlib.so",
    ],
    package_dir = "ovxlib",
    symlinks = {
        "ovxlib/libovxlib.so": "ovxlib.so"
    },
)

cc_library(
    name = "ovxlib_dynamic",
    srcs = ["ovxlib.so"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "ovxlib",
    copts = [
        "-Werror", "-Wmisleading-indentation",
        "-fvisibility=hidden", '-DOVXLIB_API=__attribute__((visibility(\\"default\\")))',
    ],
    linkopts = ["-ldl", "-lm"],
    alwayslink=True,
    linkstatic = True,
    includes = [
        "include",
    ],
    hdrs = [
        "include/vsi_nn_pub.h",
        "include/vsi_nn_ops.h",
        "include/vsi_nn_log.h",
        "include/vsi_nn_context.h",
        "include/vsi_nn_node_attr_template.h",
        "include/vsi_nn_tensor.h",
        "include/vsi_nn_prv.h",
        "include/vsi_nn_types.h",
        "include/vsi_nn_node.h",
        "include/vsi_nn_node_type.h",
        "include/vsi_nn_client_op.h",
        "include/vsi_nn_graph.h",
        "include/vsi_nn_test.h",
        "include/vsi_nn_tensor_util.h",
        "include/vsi_nn_version.h",
        "include/vsi_nn_compatibility.h",
        "include/vsi_nn_assert.h",
        "include/vsi_nn_feature.h",
        "include/vsi_nn_post.h",
        "include/vsi_nn_rnn.h",
        "include/vsi_nn_rnn_helper.h",
        "include/vsi_nn_rnn_prv.h",
        "include/vsi_nn_internal_node.h",
        "include/vsi_nn_daemon.h",
        "include/vsi_nn_pre_post_process.h",
        "include/vsi_nn_graph_optimization.h",
        "include/utils/vsi_nn_link_list.h",
        "include/utils/vsi_nn_math.h",
        "include/utils/vsi_nn_util.h",
        "include/utils/vsi_nn_code_generator.h",
        "include/utils/vsi_nn_binary_tree.h",
        "include/utils/vsi_nn_map.h",
        "include/utils/vsi_nn_hashmap.h",
        "include/utils/vsi_nn_limits.h",
        "include/utils/vsi_nn_dtype_util.h",
        "include/utils/vsi_nn_dtype_util_prv.h",
        "include/utils/vsi_nn_vdata.h",
        "include/utils/vsi_nn_tensor_op.h",
        "include/utils/vsi_nn_dlfcn.h",
        "include/utils/vsi_nn_shape_util.h",
        "include/utils/vsi_nn_constraint_check.h",
        "include/pycc/vsi_pycc_interface.h",
        "include/quantization/vsi_nn_asymmetric_affine.h",
        "include/quantization/vsi_nn_dynamic_fixed_point.h",
        "include/quantization/vsi_nn_perchannel_symmetric_affine.h",
        "include/post/vsi_nn_post_fasterrcnn.h",
        "include/post/vsi_nn_post_cmupose.h",
        "include/cpu_backend/vsi_nn_cpu_backend.h",
        "include/cpu_backend/npuref_interface.h",
        "include/client/vsi_nn_vxkernel.h",
        "include/interface/ops.def",
        "include/kernel/vsi_nn_kernel.h",
        "include/kernel/vsi_nn_gpu.h",
        "include/kernel/vsi_nn_gpu_config.h",
        "include/kernel/vsi_nn_kernel_eltwise.h",
        "include/kernel/vsi_nn_kernel_node.h",
        "include/kernel/vsi_nn_kernel_gpu_shape_optimize.h",
        "include/vsi_nn_error.h",

        # libnnext
        "include/libnnext/vx_lib_nnext.h",
        "include/libnnext/vsi_nn_libnnext_resource.h",


        #internal
        "include/internal/internal_ops.def",
    ] + [":kernel_hdrs"]
      + [":operation_hdrs"]
      + [":custom_hdrs"]
      + [
        "include/vsi_nn_platform.h",
    ] + select({
        "//conditions:default": [":nnapi_0_4_hdrs"],
        ":nnapi_0_3": [":nnapi_0_3_hdrs"],
        ":nnapi_0_4": [":nnapi_0_4_hdrs"],
    }) + [
    "vsi_cfg_feature_header"
    ]
    ,
    srcs = [
        "src/vsi_nn_graph.c",
        "src/vsi_nn_ops.c",
        "src/vsi_nn_context.c",
        "src/vsi_nn_node.c",
        "src/vsi_nn_tensor.c",
        "src/vsi_nn_client_op.c",
        "src/vsi_nn_node_attr_template.c",
        "src/vsi_nn_version.c",
        "src/vsi_nn_rnn.c",
        "src/vsi_nn_rnn_helper.c",
        "src/vsi_nn_log.c",
        "src/vsi_nn_internal_node.c",
        "src/vsi_nn_daemon.c",
        "src/vsi_nn_graph_optimization.c",
        "src/vsi_nn_pre_post_process.c",
        "src/client/vsi_nn_vxkernel.c",
        "src/utils/vsi_nn_link_list.c",
        "src/utils/vsi_nn_util.c",
        "src/utils/vsi_nn_math.c",
        "src/utils/vsi_nn_code_generator.c",
        "src/utils/vsi_nn_binary_tree.c",
        "src/utils/vsi_nn_map.c",
        "src/utils/vsi_nn_hashmap.c",
        "src/utils/vsi_nn_limits.c",
        "src/utils/vsi_nn_dtype_util.c",
        "src/utils/vsi_nn_vdata.c",
        "src/utils/vsi_nn_tensor_op.c",
        "src/utils/vsi_nn_dlfcn.c",
        "src/utils/vsi_nn_shape_util.c",
        "src/utils/vsi_nn_dtype.c",
        "src/utils/vsi_nn_constraint_check.c",
        "src/pycc/vsi_pycc_interface.c",
        "src/quantization/vsi_nn_asymmetric_affine.c",
        "src/quantization/vsi_nn_dynamic_fixed_point.c",
        "src/quantization/vsi_nn_perchannel_symmetric_affine.c",
        "src/post/vsi_nn_post_fasterrcnn.c",
        "src/post/vsi_nn_post_cmupose.c",
        "src/cpu_backend/vsi_nn_cpu_backend_conv2d.c",
        "src/cpu_backend/vsi_nn_cpu_backend_deconv2d.c",
        "src/cpu_backend/vsi_nn_cpu_backend.c",
        "src/cpu_backend/npuref_interface.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_conv2d.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_deconv2d.c",
        "src/kernel/vsi_nn_kernel.c",
        "src/kernel/vsi_nn_kernel_util.c",
        "src/kernel/vsi_nn_kernel_backend.c",
        "src/kernel/vsi_nn_kernel_eltwise.c",
        "src/kernel/vsi_nn_kernel_selector.c",
        "src/kernel/vsi_nn_kernel_node.c",
        "src/kernel/vsi_nn_kernel_param.c",
        "src/kernel/vsi_nn_gpu.c",
        "src/kernel/vsi_nn_kernel_gpu_shape_optimize.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_crop.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_fullconnect2.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_shufflechannel.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_resize.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_scale.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_space2depth.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_imageprocess.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_layernormalize.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_reduce.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_signalframe.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_tensorstackconcat.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_sync_host.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_spatial_transformer.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_extra_ending.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_topk.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_roi_align.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_heatmap_max_keypoint.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_axis_aligned_bbox_transform.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_box_with_nms_limit.c",
        "src/libnnext/ops/kernel/vsi_nn_kernel_generate_proposals.c",
        "src/libnnext/vsi_nn_libnnext_resource.c",
    ] + [":kernel_srcs"]
      + [":operation_srcs"]
      + [":custom_srcs"]
      + select({
        "//conditions:default": [":nnapi_0_4_srcs"],
        ":nnapi_0_3": ["nnapi_0_3_srcs"],
        ":nnapi_0_4": ["nnapi_0_4_srcs"],
        }),
    defines = select({
        "//conditions:default": ["VSI_CFG_NNAPI_0_4"],
        ":nnapi_0_3": ["VSI_CFG_NNAPI_0_3"],
        ":nnapi_0_4": ["VSI_CFG_NNAPI_0_4"],
    }),
    deps = ["@VIV_SDK//:VIV_SDK_INC", "@VIV_SDK//:VIV_SDK_LIB"]
)

config_setting(
    name = "nnapi_0_3",
    define_values = {
        "OVXLIB_CONFIG": "VSI_CFG_NNAPI_0_3",
    },
)

config_setting(
    name = "nnapi_0_4",
    define_values = {
        "OVXLIB_CONFIG": "VSI_CFG_NNAPI_0_4",
    },
)

filegroup(
    name = "vsi_feature_config_file",
    srcs = [
        "vsi_feature_config",
    ],
)

genrule(
    name = "vsi_cfg_feature_header",
    srcs = [":vsi_feature_config_file"],
    outs = ["include/vsi_nn_feature_config.h"],
    cmd = "./$(location bazel_gen_feature_config_header.sh) $(location :vsi_feature_config_file)> \"$@\"",
    tools = ["bazel_gen_feature_config_header.sh"],
)

config_setting(
    name = "toybrick",
    values = {
        "crosstool_top": "//tools/rockchips_toybrick_compiler:toolchain",
        "cpu": "aarch64",
    },
    visibility = ["//visibility:public"],
)

