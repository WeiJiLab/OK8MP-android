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
	{ 0x8f678b07, "__stack_chk_guard" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x5c1c3eb4, "cpu_hwcaps" },
	{ 0x5e2d7875, "cpu_hwcap_keys" },
	{ 0x14b89635, "arm64_const_caps_ready" },
	{ 0xaf507de1, "__arch_copy_from_user" },
	{ 0x6b2941b2, "__arch_copy_to_user" },
	{ 0x56c3221a, "i2c_del_driver" },
	{ 0x4eb320e1, "regulator_disable" },
	{ 0x1d9f9345, "v4l2_async_unregister_subdev" },
	{ 0xb6e6d99d, "clk_disable" },
	{ 0x27efd7f9, "regulator_enable" },
	{ 0xc4f5cecf, "regulator_set_voltage" },
	{ 0xe085ce7f, "devm_regulator_get" },
	{ 0xb077e70a, "clk_unprepare" },
	{ 0x815588a6, "clk_enable" },
	{ 0x7c9a7371, "clk_prepare" },
	{ 0x76d9b876, "clk_set_rate" },
	{ 0x9d4b265e, "gpio_to_desc" },
	{ 0x5167b7fa, "gpiod_set_raw_value_cansleep" },
	{ 0x9b1018cc, "of_property_read_variable_u32_array" },
	{ 0x52e88724, "of_get_named_gpio_flags" },
	{ 0xccc76eef, "v4l2_async_register_subdev_sensor_common" },
	{ 0x13784233, "media_entity_pads_init" },
	{ 0x7b6beea1, "v4l2_i2c_subdev_init" },
	{ 0xf9a482f9, "msleep" },
	{ 0xeae3dfd6, "__const_udelay" },
	{ 0x9c32e15d, "devm_clk_get" },
	{ 0x8916084c, "devm_gpio_request_one" },
	{ 0xfe29d807, "_dev_warn" },
	{ 0x33d27980, "devm_kmalloc" },
	{ 0xfdae9eb4, "i2c_register_driver" },
	{ 0xe86e45e1, "i2c_transfer_buffer_flags" },
	{ 0xc5850110, "printk" },
	{ 0x3c3ff9fd, "sprintf" },
	{ 0x7f64ecda, "_dev_err" },
};

MODULE_INFO(depends, "v4l2-fwnode");

MODULE_ALIAS("i2c:ov2775");
MODULE_ALIAS("of:N*T*Covti,ov2775");
MODULE_ALIAS("of:N*T*Covti,ov2775C*");
