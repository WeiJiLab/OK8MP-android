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
	{ 0xca6b9890, "kmalloc_caches" },
	{ 0x4cbcbb6a, "platform_driver_unregister" },
	{ 0x7f64ecda, "_dev_err" },
	{ 0xbf7ef82, "pm_runtime_force_resume" },
	{ 0x937e73, "pm_runtime_force_suspend" },
	{ 0x900c3ea7, "__platform_driver_register" },
	{ 0xf38b6c5a, "devm_request_threaded_irq" },
	{ 0xfcb9c22f, "kmem_cache_alloc_trace" },
	{ 0x7a4497db, "kzfree" },
	{ 0x7af24e6, "v4l2_device_put" },
	{ 0x67ca0183, "v4l2_device_disconnect" },
	{ 0x1293e3a7, "v4l2_device_unregister_subdev" },
	{ 0x1a64d87e, "devm_free_irq" },
	{ 0x5c3a9bf1, "v4l2_device_register_subdev_nodes" },
	{ 0x3eb8cc29, "platform_get_irq" },
	{ 0xdc11c7fb, "devm_ioremap_resource" },
	{ 0x23f918e2, "platform_get_resource" },
	{ 0x3e5bf429, "v4l2_device_register_subdev" },
	{ 0x7daa4823, "v4l2_device_register" },
	{ 0xa156d0d8, "v4l2_subdev_init" },
	{ 0x589cfca9, "v4l2_event_unsubscribe" },
	{ 0xcf4006c0, "v4l2_event_subscribe" },
	{ 0x3812050a, "_raw_spin_unlock_irqrestore" },
	{ 0x306d2ee7, "viv_dequeue_sg_buffer" },
	{ 0x6a833592, "viv_get_source" },
	{ 0x9165f8b, "viv_loop_source" },
	{ 0xe882a9e5, "viv_buffer_ready" },
	{ 0x51760917, "_raw_spin_lock_irqsave" },
	{ 0x5c1c3eb4, "cpu_hwcaps" },
	{ 0x5e2d7875, "cpu_hwcap_keys" },
	{ 0x14b89635, "arm64_const_caps_ready" },
	{ 0x6b2941b2, "__arch_copy_to_user" },
	{ 0xaf507de1, "__arch_copy_from_user" },
	{ 0xc5850110, "printk" },
	{ 0x2b518ad1, "viv_deinit_bufq" },
	{ 0x6be34a93, "viv_register_dst_notifier" },
	{ 0x786f4619, "viv_register_src_notifier" },
	{ 0xa996fdef, "viv_init_bufq" },
};

MODULE_INFO(depends, "vvcam-video");

MODULE_ALIAS("of:N*T*Cfsl,imx8mp-dwe");
MODULE_ALIAS("of:N*T*Cfsl,imx8mp-dweC*");
