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
	{ 0x437f14a4, "vb2_dma_contig_memops" },
	{ 0xe4bbc1dd, "kimage_voffset" },
	{ 0x1ba7c500, "physvirt_offset" },
	{ 0xc56a41e6, "vabits_actual" },
	{ 0xf9a482f9, "msleep" },
	{ 0x4cbcbb6a, "platform_driver_unregister" },
	{ 0x7af24e6, "v4l2_device_put" },
	{ 0x67ca0183, "v4l2_device_disconnect" },
	{ 0x491a2ca3, "v4l2_ctrl_handler_free" },
	{ 0xcd5913a1, "video_unregister_device" },
	{ 0xd9a5ea54, "__init_waitqueue_head" },
	{ 0xb8b9f817, "kmalloc_order_trace" },
	{ 0xfcb9c22f, "kmem_cache_alloc_trace" },
	{ 0xd21066cc, "__video_register_device" },
	{ 0x1f61af13, "dma_alloc_attrs" },
	{ 0x29361773, "complete" },
	{ 0x589cfca9, "v4l2_event_unsubscribe" },
	{ 0x60dc39ca, "v4l2_ctrl_subscribe_event" },
	{ 0xcf4006c0, "v4l2_event_subscribe" },
	{ 0x5792f848, "strlcpy" },
	{ 0x18cda413, "vb2_streamoff" },
	{ 0xba770c42, "vb2_streamon" },
	{ 0x545a5d7, "vb2_dqbuf" },
	{ 0x7aefd763, "vb2_expbuf" },
	{ 0x3a2f1c53, "vb2_qbuf" },
	{ 0x82965b30, "vb2_querybuf" },
	{ 0xcdffb306, "vb2_reqbufs" },
	{ 0x656e4a6e, "snprintf" },
	{ 0xf9b703a9, "vb2_queue_release" },
	{ 0x37a0cba, "kfree" },
	{ 0xa8437c60, "dma_free_attrs" },
	{ 0xeda75927, "vb2_plane_cookie" },
	{ 0xf4c85623, "video_devdata" },
	{ 0xf5ef842e, "v4l_bound_align_image" },
	{ 0x977f511b, "__mutex_init" },
	{ 0x7a4497db, "kzfree" },
	{ 0xba390773, "v4l2_fh_exit" },
	{ 0xd82aa673, "v4l2_fh_del" },
	{ 0xb3b8ab64, "vb2_queue_init" },
	{ 0xade6d97c, "v4l2_fh_add" },
	{ 0xb9d9bde7, "v4l2_fh_init" },
	{ 0x169c5124, "v4l2_ctrl_poll" },
	{ 0x6c53254d, "vb2_poll" },
	{ 0x960ec422, "v4l2_event_pending" },
	{ 0xb0764e5f, "video_ioctl2" },
	{ 0xe914e41e, "strcpy" },
	{ 0x5c3a9bf1, "v4l2_device_register_subdev_nodes" },
	{ 0xbf815c50, "video_device_release" },
	{ 0x6ed72663, "v4l2_ctrl_new_custom" },
	{ 0x507afa10, "v4l2_ctrl_handler_init_class" },
	{ 0x3c3ff9fd, "sprintf" },
	{ 0x7daa4823, "v4l2_device_register" },
	{ 0x2f795a4c, "video_device_alloc" },
	{ 0xaf3a9f32, "platform_device_unregister" },
	{ 0x900c3ea7, "__platform_driver_register" },
	{ 0xe1196a42, "platform_device_register" },
	{ 0x7aa03f17, "remap_pfn_range" },
	{ 0xb43f9365, "ktime_get" },
	{ 0x68f31cbd, "__list_add_valid" },
	{ 0xe1537255, "__list_del_entry_valid" },
	{ 0x3812050a, "_raw_spin_unlock_irqrestore" },
	{ 0x17dfdb32, "vb2_mmap" },
	{ 0x4d1ff60a, "wait_for_completion_timeout" },
	{ 0xd93be262, "v4l2_event_queue" },
	{ 0x35c9a3ef, "vb2_buffer_done" },
	{ 0x2cd90edd, "__cfi_slowpath" },
	{ 0xc5850110, "printk" },
	{ 0x409bcb62, "mutex_unlock" },
	{ 0x2ab7989d, "mutex_lock" },
	{ 0x51760917, "_raw_spin_lock_irqsave" },
};

MODULE_INFO(depends, "");

