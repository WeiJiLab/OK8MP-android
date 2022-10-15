// SPDX-License-Identifier: GPL-2.0
/*
 * Raydium RM67191 MIPI-DSI panel driver
 *
 * Copyright 2019 NXP
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

/* Panel specific color-format bits */
#define COL_FMT_16BPP 0x55
#define COL_FMT_18BPP 0x66
#define COL_FMT_24BPP 0x77


static const u32 rad_bus_formats[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_RGB666_1X18,
	MEDIA_BUS_FMT_RGB565_1X16,
};

static const u32 rad_bus_flags = DRM_BUS_FLAG_DE_LOW |
				 DRM_BUS_FLAG_PIXDATA_NEGEDGE;

struct rad_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;

	struct gpio_desc *enable;
	struct backlight_device *backlight;

	struct regulator_bulk_data *supplies;
	unsigned int num_supplies;

	bool prepared;
	bool enabled;

	const struct rad_platform_data *pdata;
};

struct rad_platform_data {
	int (*enable)(struct rad_panel *panel);
};

static const struct drm_display_mode default_mode = {
	.clock = 45000,
	.hdisplay = 1024,
	.hsync_start = 1024 + 160,     //bp
	.hsync_end = 1024 + 160 + 70,   //bp sw
	.htotal = 1024 + 160 + 70 + 160, //bp sw fp
	.vdisplay = 600,
	.vsync_start = 600 + 23,
	.vsync_end = 600 + 23 + 10,
	.vtotal = 600 + 23 + 10 + 12,
	.vrefresh = 60,
	.width_mm = 68,
	.height_mm = 121,
	.flags = DRM_MODE_FLAG_NHSYNC |
		 DRM_MODE_FLAG_NVSYNC,
};

static inline struct rad_panel *to_rad_panel(struct drm_panel *panel)
{
	return container_of(panel, struct rad_panel, panel);
}


static int rad_panel_prepare(struct drm_panel *panel)
{
	struct rad_panel *rad = to_rad_panel(panel);
	int ret;

	if (rad->prepared)
		return 0;

	if(rad->enable)
		gpiod_set_value_cansleep(rad->enable, 1);

	rad->prepared = true;

	return 0;
}

static int rad_panel_unprepare(struct drm_panel *panel)
{
	struct rad_panel *rad = to_rad_panel(panel);
	int ret;

	if (!rad->prepared)
		return 0;

	if(rad->enable)
		gpiod_set_value_cansleep(rad->enable, 0);

	rad->prepared = false;

	return 0;
}

static int rm67191_enable(struct rad_panel *panel)
{
	struct mipi_dsi_device *dsi = panel->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	if (panel->enabled)
		return 0;

	if(panel->backlight){
		panel->backlight->props.state &= ~BL_CORE_FBBLANK;
		panel->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(panel->backlight);
	}

	panel->enabled = true;

	return 0;

fail:
	if(panel->enable)
		gpiod_set_value_cansleep(panel->enable, 0);

	return ret;
}

static int rad_panel_enable(struct drm_panel *panel)
{
	struct rad_panel *rad = to_rad_panel(panel);
	//for different panel
	return rad->pdata->enable(rad);
}

static int rad_panel_disable(struct drm_panel *panel)
{
	struct rad_panel *rad = to_rad_panel(panel);
	struct mipi_dsi_device *dsi = rad->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	if (!rad->enabled)
		return 0;

	if(rad->backlight){
		rad->backlight->props.power = FB_BLANK_POWERDOWN;
		rad->backlight->props.state |= BL_CORE_FBBLANK;
		backlight_update_status(rad->backlight);
	}

	if(rad->enable)
		gpiod_set_value_cansleep(rad->enable, 0);

	rad->enabled = false;

	return 0;
}

static int rad_panel_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct drm_device *drm = panel->drm;
	struct drm_display_mode *mode = NULL;
	struct rad_panel *rad = to_rad_panel(panel);
	struct device_node *timings_np, *np = rad->dsi->dev.of_node;
	int ret;

	timings_np = of_get_child_by_name(np, "display-timings");
	if(timings_np){
		mode = drm_mode_create(drm);
		if(!mode){
			of_node_put(timings_np);
			return 0;
		}
		ret = of_get_drm_display_mode(np, mode, NULL, 0);
		if(ret){
			dev_dbg(panel->dev, "failed to find dts display timings\n");
			drm_mode_destroy(drm, mode);
			mode = NULL;
		} else {
			mode->type |= DRM_MODE_TYPE_PREFERRED;
		}
		of_node_put(timings_np);
	} 

	if(!mode){
		dev_info(panel->dev, "using default display timings\n");
		mode = drm_mode_duplicate(panel->drm, &default_mode);
		if (!mode) {
			DRM_DEV_ERROR(panel->dev, "failed to add mode %ux%ux@%u\n",
					default_mode.hdisplay, default_mode.vdisplay,
					default_mode.vrefresh);
			return -ENOMEM;
		}
		mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	}


	drm_mode_set_name(mode);
	drm_mode_probed_add(panel->connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	connector->display_info.bus_flags = rad_bus_flags;

	drm_display_info_set_bus_formats(&connector->display_info,
			rad_bus_formats,
			ARRAY_SIZE(rad_bus_formats));
	return 1;
}

static int rad_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	struct rad_panel *rad = mipi_dsi_get_drvdata(dsi);
	u16 brightness;
	int ret;

	if (!rad->prepared)
		return 0;

//	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
//
//	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
//	if (ret < 0)
//		return ret;
//
	bl->props.brightness = brightness;

	return brightness & 0xff;
}

static int rad_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	struct rad_panel *rad = mipi_dsi_get_drvdata(dsi);
	int ret = 0;

	if (!rad->prepared)
		return 0;

//	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
//
//	ret = mipi_dsi_dcs_set_display_brightness(dsi, bl->props.brightness);
//	if (ret < 0)
//		return ret;
//
	return 0;
}

static const struct backlight_ops rad_bl_ops = {
	.update_status = rad_bl_update_status,
	.get_brightness = rad_bl_get_brightness,
};

static const struct drm_panel_funcs rad_panel_funcs = {
	.prepare = rad_panel_prepare,
	.unprepare = rad_panel_unprepare,
	.enable = rad_panel_enable,
	.disable = rad_panel_disable,
	.get_modes = rad_panel_get_modes,
};

static const char * const rad_supply_names[] = {
	"v3p3",
	"v1p8",
};


static const struct rad_platform_data rad_rm67191 = {
	.enable = &rm67191_enable,
};

static const struct of_device_id rad_of_match[] = {
	{ .compatible = "forlinx,mipi-dsi", .data = &rad_rm67191 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rad_of_match);

static int rad_panel_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct of_device_id *of_id = of_match_device(rad_of_match, dev);
	struct device_node *backlight, *np = dev->of_node;
	struct rad_panel *panel;
	int ret;
	u32 video_mode;

	if (!of_id || !of_id->data)
		return -ENODEV;

	panel = devm_kzalloc(&dsi->dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, panel);

	panel->dsi = dsi;
	panel->pdata = of_id->data;

	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags =  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO;

	ret = of_property_read_u32(np, "video-mode", &video_mode);
	if (!ret) {
		switch (video_mode) {
		case 0:
			/* burst mode */
			dsi->mode_flags |= MIPI_DSI_MODE_VIDEO_BURST;
			break;
		case 1:
			/* non-burst mode with sync event */
			break;
		case 2:
			/* non-burst mode with sync pulse */
			dsi->mode_flags |= MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
			break;
		default:
			dev_warn(dev, "invalid video mode %d\n", video_mode);
			break;
		}
	}

	ret = of_property_read_u32(np, "dsi-lanes", &dsi->lanes);
	if (ret) {
		dev_err(dev, "Failed to get dsi-lanes property (%d)\n", ret);
		return ret;
	}

//	panel->enable = devm_gpiod_get_optional(dev, "enable",
//					       GPIOD_OUT_LOW |
//					       GPIOD_FLAGS_BIT_NONEXCLUSIVE);
//	if (IS_ERR(panel->enable)) {
//		ret = PTR_ERR(panel->enable);
//		dev_err(dev, "Failed to get enable gpio (%d)\n", ret);
//		panel->enable = NULL;
//	}
	if(panel->enable)
		gpiod_set_value_cansleep(panel->enable, 0);

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if(backlight){
		panel->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);
		if(!panel->backlight)
			printk("panel without backlight\n");
	}

	drm_panel_init(&panel->panel);
	panel->panel.funcs = &rad_panel_funcs;
	panel->panel.dev = dev;
	dev_set_drvdata(dev, panel);

	ret = drm_panel_add(&panel->panel);
	if (ret){
		if(panel->backlight){
			put_device(&panel->backlight->dev);
		}
		return ret;
	}

	ret = mipi_dsi_attach(dsi);
	if (ret)
		drm_panel_remove(&panel->panel);

	return ret;
}

static int rad_panel_remove(struct mipi_dsi_device *dsi)
{
	struct rad_panel *rad = mipi_dsi_get_drvdata(dsi);
	struct device *dev = &dsi->dev;
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret)
		DRM_DEV_ERROR(dev, "Failed to detach from host (%d)\n",
			      ret);

	drm_panel_remove(&rad->panel);

	if(rad->backlight)
		put_device(&rad->backlight->dev);
	if(rad->enable)
		gpiod_set_value_cansleep(rad->enable, 0);


	return 0;
}

static void rad_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct rad_panel *rad = mipi_dsi_get_drvdata(dsi);

	rad_panel_disable(&rad->panel);
	rad_panel_unprepare(&rad->panel);
}

static struct mipi_dsi_driver rad_panel_driver = {
	.driver = {
		.name = "panel-forlinx-mipi",
		.of_match_table = rad_of_match,
	},
	.probe = rad_panel_probe,
	.remove = rad_panel_remove,
	.shutdown = rad_panel_shutdown,
};
module_mipi_dsi_driver(rad_panel_driver);

MODULE_AUTHOR("Robert Chiras <robert.chiras@nxp.com>");
MODULE_DESCRIPTION("DRM Driver for Raydium RM67191 MIPI DSI panel");
MODULE_LICENSE("GPL v2");
