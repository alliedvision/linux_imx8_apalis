/*
 * Copyright (C) 2011-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2014-2016 Toradex AG. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*
 * copy of mxc_lcdif.c
 * adds a second parallel output which drives a Video DAC
 * available on the second IPU, first DI on a
 * Apalis iMX6 module
 */

#include <linux/init.h>
#include <linux/ipu.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mxcfb.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>

#include "mxc_dispdrv.h"

struct mxc_vdac_platform_data {
	u32 default_ifmt;
	u32 ipu_id;
	u32 disp_id;
};

struct mxc_vdacif_data {
	struct platform_device *pdev;
	struct mxc_dispdrv_handle *disp_vdacif;
};

#define DISPDRV_LCD	"vdac"

static struct fb_videomode vdacif_modedb[] = {
	/* 0 640x350-85 VESA */
	{ NULL, 85, 640, 350, 31746,  96, 32, 60, 32, 64, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_OE_LOW_ACT, FB_VMODE_NONINTERLACED,
	  FB_MODE_IS_VESA},
	/* 1 640x400-85 VESA */
	{ NULL, 85, 640, 400, 31746,  96, 32, 41, 01, 64, 3,
	  FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT, FB_VMODE_NONINTERLACED,
	  FB_MODE_IS_VESA },
	/* 2 720x400-85 VESA */
	{ NULL, 85, 721, 400, 28169, 108, 36, 42, 01, 72, 3,
	  FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT, FB_VMODE_NONINTERLACED,
	  FB_MODE_IS_VESA },
	/* 3 640x480-60 VESA */
	{ NULL, 60, 640, 480, 39682,  48, 16, 33, 10, 96, 2,
	  FB_SYNC_OE_LOW_ACT, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 4 640x480-72 VESA */
	{ NULL, 72, 640, 480, 31746, 128, 24, 29, 9, 40, 2,
	  FB_SYNC_OE_LOW_ACT, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 5 640x480-75 VESA */
	{ NULL, 75, 640, 480, 31746, 120, 16, 16, 01, 64, 3,
	  FB_SYNC_OE_LOW_ACT, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 6 640x480-85 VESA */
	{ NULL, 85, 640, 480, 27777, 80, 56, 25, 01, 56, 3,
	  FB_SYNC_OE_LOW_ACT, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 7 800x600-56 VESA */
	{ NULL, 56, 800, 600, 27777, 128, 24, 22, 01, 72, 2,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 8 800x600-60 VESA */
	{ NULL, 60, 800, 600, 25000, 88, 40, 23, 01, 128, 4,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 9 800x600-72 VESA */
	{ NULL, 72, 800, 600, 20000, 64, 56, 23, 37, 120, 6,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 10 800x600-75 VESA */
	{ NULL, 75, 800, 600, 20202, 160, 16, 21, 01, 80, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 11 800x600-85 VESA */
	{ NULL, 85, 800, 600, 17761, 152, 32, 27, 01, 64, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
        /* 12 1024x768i-43 VESA */
	{ NULL, 43, 1024, 768, 22271, 56, 8, 41, 0, 176, 8,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_INTERLACED, FB_MODE_IS_VESA },
	/* 13 1024x768-60 VESA */
	{ NULL, 60, 1024, 768, 15384, 160, 24, 29, 3, 136, 6,
	  FB_SYNC_OE_LOW_ACT, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 14 1024x768-70 VESA */
	{ NULL, 70, 1024, 768, 13333, 144, 24, 29, 3, 136, 6,
	  FB_SYNC_OE_LOW_ACT, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 15 1024x768-75 VESA */
	{ NULL, 75, 1024, 768, 12690, 176, 16, 28, 1, 96, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 16 1024x768-85 VESA */
	{ NULL, 85, 1024, 768, 10582, 208, 48, 36, 1, 96, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 17 1152x864-75 VESA */
	{ NULL, 75, 1152, 864, 9259, 256, 64, 32, 1, 128, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 18 1280x960-60 VESA */
	{ NULL, 60, 1280, 960, 9259, 312, 96, 36, 1, 112, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 19 1280x960-85 VESA */
	{ NULL, 85, 1280, 960, 6734, 224, 64, 47, 1, 160, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 20 1280x1024-60 VESA */
	{ NULL, 60, 1280, 1024, 9259, 248, 48, 38, 1, 112, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 21 1280x1024-75 VESA */
	{ NULL, 75, 1280, 1024, 7407, 248, 16, 38, 1, 144, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 22 1280x1024-85 VESA */
	{ NULL, 85, 1280, 1024, 6349, 224, 64, 44, 1, 160, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 23 1600x1200-60 VESA */
	{ NULL, 60, 1600, 1200, 6172, 304, 64, 46, 1, 192, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 24 1600x1200-65 VESA */
	{ NULL, 65, 1600, 1200, 5698, 304,  64, 46, 1, 192, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 25 1600x1200-70 VESA */
	{ NULL, 70, 1600, 1200, 5291, 304, 64, 46, 1, 192, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 26 1600x1200-75 VESA */
	{ NULL, 75, 1600, 1200, 4938, 304, 64, 46, 1, 192, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 27 1600x1200-85 VESA */
	{ NULL, 85, 1600, 1200, 4357, 304, 64, 46, 1, 192, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 28 1792x1344-60 VESA */
	{ NULL, 60, 1792, 1344, 4882, 328, 128, 46, 1, 200, 3,
	  FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 29 1792x1344-75 VESA */
	{ NULL, 75, 1792, 1344, 3831, 352, 96, 69, 1, 216, 3,
	  FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 30 1856x1392-60 VESA */
	{ NULL, 60, 1856, 1392, 4580, 352, 96, 43, 1, 224, 3,
	  FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 31 1856x1392-75 VESA */
	{ NULL, 75, 1856, 1392, 3472, 352, 128, 104, 1, 224, 3,
	  FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 32 1920x1440-60 VESA */
	{ NULL, 60, 1920, 1440, 4273, 344, 128, 56, 1, 200, 3,
	  FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 33 1920x1440-75 VESA */
	{ NULL, 75, 1920, 1440, 3367, 352, 144, 56, 1, 224, 3,
	  FB_SYNC_VERT_HIGH_ACT  | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	/* 1920x1200 @ 60 Hz, 74.5 Khz hsync */
	{ NULL, 60, 1920, 1200, 5177, 128, 336, 1, 38, 208, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED },
        /* #16: 1920x1080p@60Hz 16:9 */
	{ NULL, 60, 1920, 1080, 6734, 148, 88, 36, 4, 44, 5,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT | FB_SYNC_OE_LOW_ACT,
	  FB_VMODE_NONINTERLACED, 0 },
};
static int vdacif_modedb_sz = ARRAY_SIZE(vdacif_modedb);

static int vdacif_init(struct mxc_dispdrv_handle *disp,
	struct mxc_dispdrv_setting *setting)
{
	int ret, i;
	struct mxc_vdacif_data *vdacif = mxc_dispdrv_getdata(disp);
	struct device *dev = &vdacif->pdev->dev;
	struct mxc_vdac_platform_data *plat_data = dev->platform_data;
	struct fb_videomode *modedb = vdacif_modedb;
	int modedb_sz = vdacif_modedb_sz;

	/* use platform defined ipu/di */
	ret = ipu_di_to_crtc(dev, plat_data->ipu_id,
			     plat_data->disp_id, &setting->crtc);
	if (ret < 0)
		return ret;

	ret = fb_find_mode(&setting->fbi->var, setting->fbi, setting->dft_mode_str,
				modedb, modedb_sz, NULL, setting->default_bpp);
	if (!ret) {
		fb_videomode_to_var(&setting->fbi->var, &modedb[0]);
		setting->if_fmt = plat_data->default_ifmt;
	}
	/* Peter's VDAC requires OE to be low active */
	setting->fbi->var.sync |= FB_SYNC_OE_LOW_ACT;

	INIT_LIST_HEAD(&setting->fbi->modelist);
	for (i = 0; i < modedb_sz; i++) {
		struct fb_videomode m;
		fb_var_to_videomode(&m, &setting->fbi->var);
		if (fb_mode_is_equal(&m, &modedb[i])) {
			fb_add_videomode(&modedb[i],
					&setting->fbi->modelist);
			break;
		}
	}

	return ret;
}

void vdacif_deinit(struct mxc_dispdrv_handle *disp)
{
	/*TODO*/
}

static struct mxc_dispdrv_driver vdacif_drv = {
	.name 	= DISPDRV_LCD,
	.init 	= vdacif_init,
	.deinit	= vdacif_deinit,
};

static int vdac_get_of_property(struct platform_device *pdev,
				struct mxc_vdac_platform_data *plat_data)
{
	struct device_node *np = pdev->dev.of_node;
	int err;
	u32 ipu_id, disp_id;
	const char *default_ifmt;

	err = of_property_read_string(np, "default_ifmt", &default_ifmt);
	if (err) {
		dev_dbg(&pdev->dev, "get of property default_ifmt fail\n");
		return err;
	}
	err = of_property_read_u32(np, "ipu_id", &ipu_id);
	if (err) {
		dev_dbg(&pdev->dev, "get of property ipu_id fail\n");
		return err;
	}
	err = of_property_read_u32(np, "disp_id", &disp_id);
	if (err) {
		dev_dbg(&pdev->dev, "get of property disp_id fail\n");
		return err;
	}

	if ( (ipu_id == 0) || (disp_id == 1) ) {
		dev_err(&pdev->dev, "wrong ipu_id %x or disp_id %x, expected 1, 0\n", ipu_id, disp_id);
	}
	plat_data->ipu_id = ipu_id;
	plat_data->disp_id = disp_id;
	if (!strncmp(default_ifmt, "RGB565", 6))
		plat_data->default_ifmt = IPU_PIX_FMT_RGB565;
	else {
		dev_err(&pdev->dev, "err default_ifmt!\n");
		return -ENOENT;
	}

	return err;
}

static int mxc_vdacif_probe(struct platform_device *pdev)
{
	int ret;
	struct pinctrl *pinctrl;
	struct mxc_vdacif_data *vdacif;
	struct mxc_vdac_platform_data *plat_data;

	dev_dbg(&pdev->dev, "%s enter\n", __func__);
	vdacif = devm_kzalloc(&pdev->dev, sizeof(struct mxc_vdacif_data),
				GFP_KERNEL);
	if (!vdacif)
		return -ENOMEM;
	plat_data = devm_kzalloc(&pdev->dev,
				sizeof(struct mxc_vdac_platform_data),
				GFP_KERNEL);
	if (!plat_data)
		return -ENOMEM;
	pdev->dev.platform_data = plat_data;

	ret = vdac_get_of_property(pdev, plat_data);
	if (ret < 0) {
		dev_err(&pdev->dev, "get vdac of property fail\n");
		return ret;
	}

	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pinctrl)) {
		dev_err(&pdev->dev, "can't get/select pinctrl\n");
		return PTR_ERR(pinctrl);
	}

	vdacif->pdev = pdev;
	vdacif->disp_vdacif = mxc_dispdrv_register(&vdacif_drv);
	mxc_dispdrv_setdata(vdacif->disp_vdacif, vdacif);

	dev_set_drvdata(&pdev->dev, vdacif);
	dev_dbg(&pdev->dev, "%s exit\n", __func__);

	return ret;
}

static int mxc_vdacif_remove(struct platform_device *pdev)
{
	struct mxc_vdacif_data *vdacif = dev_get_drvdata(&pdev->dev);

	mxc_dispdrv_puthandle(vdacif->disp_vdacif);
	mxc_dispdrv_unregister(vdacif->disp_vdacif);
	kfree(vdacif);
	return 0;
}

static const struct of_device_id imx_vdac_dt_ids[] = {
	{ .compatible = "fsl,vdac"},
	{ /* sentinel */ }
};
static struct platform_driver mxc_vdacif_driver = {
	.driver = {
		.name = "mxc_vdacif",
		.of_match_table	= imx_vdac_dt_ids,
	},
	.probe = mxc_vdacif_probe,
	.remove = mxc_vdacif_remove,
};

static int __init mxc_vdacif_init(void)
{
	return platform_driver_register(&mxc_vdacif_driver);
}

static void __exit mxc_vdacif_exit(void)
{
	platform_driver_unregister(&mxc_vdacif_driver);
}

module_init(mxc_vdacif_init);
module_exit(mxc_vdacif_exit);

MODULE_AUTHOR("Toradex AG");
MODULE_DESCRIPTION("i.MX ipuv3 VDAC extern port driver");
MODULE_LICENSE("GPL");
