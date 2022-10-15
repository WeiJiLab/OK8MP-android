#include <linux/module.h>
#include <linux/build-salt.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(.gnu.linkonce.this_module) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section(__versions) = {
	{ 0x1e5b7ab7, "module_layout" },
	{ 0xdcb764ad, "memset" },
	{ 0x4829a47e, "memcpy" },
	{ 0x8f678b07, "__stack_chk_guard" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0xca6b9890, "kmalloc_caches" },
	{ 0xb2ead97c, "kimage_vaddr" },
	{ 0x5c1c3eb4, "cpu_hwcaps" },
	{ 0x5e2d7875, "cpu_hwcap_keys" },
	{ 0x14b89635, "arm64_const_caps_ready" },
	{ 0xf133db1b, "v4l2_subdev_call_wrappers" },
	{ 0xd8432e6f, "param_ops_int" },
	{ 0x4cbcbb6a, "platform_driver_unregister" },
	{ 0x27efd7f9, "regulator_enable" },
	{ 0xb6e6d99d, "clk_disable" },
	{ 0x4eb320e1, "regulator_disable" },
	{ 0xbf7ef82, "pm_runtime_force_resume" },
	{ 0x937e73, "pm_runtime_force_suspend" },
	{ 0x7af24e6, "v4l2_device_put" },
	{ 0x67ca0183, "v4l2_device_disconnect" },
	{ 0x1293e3a7, "v4l2_device_unregister_subdev" },
	{ 0x409bcb62, "mutex_unlock" },
	{ 0x2ab7989d, "mutex_lock" },
	{ 0xfcb9c22f, "kmem_cache_alloc_trace" },
	{ 0x5c3a9bf1, "v4l2_device_register_subdev_nodes" },
	{ 0x3e5bf429, "v4l2_device_register_subdev" },
	{ 0x7daa4823, "v4l2_device_register" },
	{ 0x656e4a6e, "snprintf" },
	{ 0xa156d0d8, "v4l2_subdev_init" },
	{ 0xb077e70a, "clk_unprepare" },
	{ 0x815588a6, "clk_enable" },
	{ 0x7c9a7371, "clk_prepare" },
	{ 0xd4c88402, "dev_driver_string" },
	{ 0x76d9b876, "clk_set_rate" },
	{ 0x9c32e15d, "devm_clk_get" },
	{ 0xc4f5cecf, "regulator_set_voltage" },
	{ 0xe085ce7f, "devm_regulator_get" },
	{ 0x3d022de, "of_find_property" },
	{ 0x9b1018cc, "of_property_read_variable_u32_array" },
	{ 0x18407fb5, "of_graph_get_next_endpoint" },
	{ 0xe5a1ff3c, "of_alias_get_id" },
	{ 0x33d27980, "devm_kmalloc" },
	{ 0xf125bbc2, "_dev_info" },
	{ 0x32654f50, "pm_runtime_enable" },
	{ 0x13784233, "media_entity_pads_init" },
	{ 0x6b4b2933, "__ioremap" },
	{ 0x23f918e2, "platform_get_resource" },
	{ 0xb565ec5c, "of_clk_set_defaults" },
	{ 0x69db59b0, "syscon_regmap_lookup_by_phandle" },
	{ 0x99540757, "of_match_node" },
	{ 0x7f64ecda, "_dev_err" },
	{ 0xbb43a20e, "of_find_compatible_node" },
	{ 0x977f511b, "__mutex_init" },
	{ 0x900c3ea7, "__platform_driver_register" },
	{ 0xaf507de1, "__arch_copy_from_user" },
	{ 0x56470118, "__warn_printk" },
	{ 0x44b6fa89, "__pm_runtime_idle" },
	{ 0x5b941d8e, "regmap_update_bits_base" },
	{ 0xf9a482f9, "msleep" },
	{ 0x3812050a, "_raw_spin_unlock_irqrestore" },
	{ 0x51760917, "_raw_spin_lock_irqsave" },
	{ 0xe803bac0, "__pm_runtime_resume" },
	{ 0x17c77172, "media_entity_remote_pad" },
	{ 0x2cd90edd, "__cfi_slowpath" },
	{ 0xfa9e473c, "regmap_write" },
	{ 0x5ecb9197, "regmap_read" },
	{ 0xc5850110, "printk" },
	{ 0xeae3dfd6, "__const_udelay" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("of:N*T*Cfsl,imx8mn-mipi-csi");
MODULE_ALIAS("of:N*T*Cfsl,imx8mn-mipi-csiC*");
