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
	{ 0x4cbcbb6a, "platform_driver_unregister" },
	{ 0x7f64ecda, "_dev_err" },
	{ 0xbf7ef82, "pm_runtime_force_resume" },
	{ 0x937e73, "pm_runtime_force_suspend" },
	{ 0x900c3ea7, "__platform_driver_register" },
	{ 0xfaf9a34c, "__pm_runtime_disable" },
	{ 0x44b6fa89, "__pm_runtime_idle" },
	{ 0xf38b6c5a, "devm_request_threaded_irq" },
	{ 0xe803bac0, "__pm_runtime_resume" },
	{ 0xb8a8d975, "fwnode_property_read_u32_array" },
	{ 0x6836d0a8, "v4l2_event_subdev_unsubscribe" },
	{ 0x647aa4a7, "v4l2_ctrl_subdev_subscribe_event" },
	{ 0x7a4497db, "kzfree" },
	{ 0x7af24e6, "v4l2_device_put" },
	{ 0x67ca0183, "v4l2_device_disconnect" },
	{ 0x1293e3a7, "v4l2_device_unregister_subdev" },
	{ 0xa8437c60, "dma_free_attrs" },
	{ 0x1a64d87e, "devm_free_irq" },
	{ 0x5c3a9bf1, "v4l2_device_register_subdev_nodes" },
	{ 0x3eb8cc29, "platform_get_irq" },
	{ 0x1f61af13, "dma_alloc_attrs" },
	{ 0xdc11c7fb, "devm_ioremap_resource" },
	{ 0x23f918e2, "platform_get_resource" },
	{ 0x3e5bf429, "v4l2_device_register_subdev" },
	{ 0x7daa4823, "v4l2_device_register" },
	{ 0x32654f50, "pm_runtime_enable" },
	{ 0x656e4a6e, "snprintf" },
	{ 0xa156d0d8, "v4l2_subdev_init" },
	{ 0xa387a505, "viv_post_irq_event" },
	{ 0xe882a9e5, "viv_buffer_ready" },
	{ 0xca6b9890, "kmalloc_caches" },
	{ 0xfcb9c22f, "kmem_cache_alloc_trace" },
	{ 0x6b2941b2, "__arch_copy_to_user" },
	{ 0x37a0cba, "kfree" },
	{ 0xeae3dfd6, "__const_udelay" },
	{ 0x5c1c3eb4, "cpu_hwcaps" },
	{ 0x5e2d7875, "cpu_hwcap_keys" },
	{ 0x14b89635, "arm64_const_caps_ready" },
	{ 0xaf507de1, "__arch_copy_from_user" },
	{ 0x988fcb82, "viv_dequeue_buffer" },
	{ 0x2b518ad1, "viv_deinit_bufq" },
	{ 0x680661c5, "viv_queue_buffer" },
	{ 0xa996fdef, "viv_init_bufq" },
	{ 0xc5850110, "printk" },
};

MODULE_INFO(depends, "vvcam-video");

MODULE_ALIAS("of:N*T*Cfsl,imx8mp-isp");
MODULE_ALIAS("of:N*T*Cfsl,imx8mp-ispC*");
