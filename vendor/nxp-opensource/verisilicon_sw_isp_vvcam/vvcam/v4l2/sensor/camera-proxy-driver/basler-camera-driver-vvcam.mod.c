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
	{ 0x5c1c3eb4, "cpu_hwcaps" },
	{ 0x5e2d7875, "cpu_hwcap_keys" },
	{ 0x14b89635, "arm64_const_caps_ready" },
	{ 0x56c3221a, "i2c_del_driver" },
	{ 0x1d9f9345, "v4l2_async_unregister_subdev" },
	{ 0x491a2ca3, "v4l2_ctrl_handler_free" },
	{ 0x6ed72663, "v4l2_ctrl_new_custom" },
	{ 0x507afa10, "v4l2_ctrl_handler_init_class" },
	{ 0x409bcb62, "mutex_unlock" },
	{ 0x2ab7989d, "mutex_lock" },
	{ 0x712a64c6, "v4l2_fwnode_endpoint_alloc_parse" },
	{ 0x18407fb5, "of_graph_get_next_endpoint" },
	{ 0xe86e45e1, "i2c_transfer_buffer_flags" },
	{ 0xe8e31829, "i2c_transfer" },
	{ 0xc5850110, "printk" },
	{ 0x6b2941b2, "__arch_copy_to_user" },
	{ 0xaf507de1, "__arch_copy_from_user" },
	{ 0x3c3ff9fd, "sprintf" },
	{ 0x9b1018cc, "of_property_read_variable_u32_array" },
	{ 0x33d27980, "devm_kmalloc" },
	{ 0x2cd90edd, "__cfi_slowpath" },
	{ 0xccc76eef, "v4l2_async_register_subdev_sensor_common" },
	{ 0x977f511b, "__mutex_init" },
	{ 0x13784233, "media_entity_pads_init" },
	{ 0x7b6beea1, "v4l2_i2c_subdev_init" },
	{ 0x7f64ecda, "_dev_err" },
	{ 0xfdae9eb4, "i2c_register_driver" },
};

MODULE_INFO(depends, "v4l2-fwnode");

MODULE_ALIAS("i2c:basler-camera-vvcam");
MODULE_ALIAS("of:N*T*Cbasler,basler-camera-vvcam");
MODULE_ALIAS("of:N*T*Cbasler,basler-camera-vvcamC*");
