/*
 * Copyright 2016 Toradex AG.
 *
 * Based on adv7180/max9526 driver for iMX6 and adv7280 driver for Tegra
 * by Antmicro
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-chip-ident.h>
#include "v4l2-int-device.h"
#include "mxc_v4l2_capture.h"

#define ADV7280_VOLTAGE_ANALOG               1800000
#define ADV7280_VOLTAGE_DIGITAL_CORE         1800000
#define ADV7280_VOLTAGE_DIGITAL_IO           3300000
#define ADV7280_VOLTAGE_PLL                  1800000

static struct regulator *dvddio_regulator;
static struct regulator *dvdd_regulator;
static struct regulator *avdd_regulator;
static struct regulator *pvdd_regulator;
static int pwn_gpio;

struct sensor {
	struct sensor_data sen;
	v4l2_std_id std_id;
	int rev_id;
} adv7280_data;

typedef enum {
	ADV7280_NTSC = 0,	/*!< Locked on (M) NTSC video signal. */
	ADV7280_PAL,		/*!< (B, G, H, I, N)PAL video signal. */
	ADV7280_NOT_LOCKED,	/*!< Not locked on a signal. */
} video_fmt_idx;

struct video_fmt_t {
	int v4l2_id;
	char name[16];
	u16 raw_width;
	u16 raw_height;
	u16 active_width;
	u16 active_height;
	int frame_rate;
};

static const struct video_fmt_t video_fmts[] = {
	{
	 .v4l2_id = V4L2_STD_NTSC,
	 .name = "NTSC",
	 .raw_width = 720,
	 .raw_height = 525,
	 .active_width = 720,
	 .active_height = 480,
	 .frame_rate = 30,
	 },
	{
	 .v4l2_id = V4L2_STD_PAL,
	 .name = "PAL",
	 .raw_width = 720,
	 .raw_height = 625,
	 .active_width = 720,
	 .active_height = 576,
	 .frame_rate = 25,
	 },
	{
	 .v4l2_id = V4L2_STD_ALL,
	 .name = "Autodetect",
	 .raw_width = 720,
	 .raw_height = 625,
	 .active_width = 720,
	 .active_height = 576,
	 .frame_rate = 0,
	 },
};

static video_fmt_idx video_idx = ADV7280_PAL;

static DEFINE_MUTEX(mutex);

#define IF_NAME					"adv7280"

#define ADV7280_INPUT_CONTROL			0x00
#define ADV7280_VIDEO_SELECTION_1		0x01
#define ADV7280_VIDEO_SELECTION_2		0x02
#define ADV7280_OUTPUT_CONTROL			0x03
#define ADV7280_EXTENDED_OUTPUT_CONTROL		0x04
#define ADV7280_AUTODETECT_ENABLE		0x07
#define ADV7280_BRIGHTNESS			0x0A
#define ADV7280_ADI_CONTROL_1			0x0E
#define ADV7280_POWER_MANAGEMENT		0x0F
#define ADV7280_STATUS_1			0x10
#define ADV7280_IDENT				0x11
#define ADV7280_STATUS_3			0x13
#define ADV7280_SHAPING_FILTER_CONTROL_1	0x17
#define ADV7280_ADI_CONTROL_2			0x1D
#define ADV7280_PIXEL_DELAY_CONTROL		0x27
#define ADV7280_VPP_SLAVE_ADDRESS		0xFD
#define ADV7280_OUTPUT_SYNC_SELECT_2		0x6B
#define ADV7280_SD_SATURATION_CB		0xe3
#define ADV7280_SD_SATURATION_CR		0xe4

#define HW_DEINT /* Enable hardware deinterlacer */
#define VPP_SLAVE_ADDRESS			0x42

#define VPP_DEINT_RESET				0x41
#define VPP_I2C_DEINT_ENABLE			0x55
#define VPP_ADV_TIMING_MODE_EN			0x5B

#define ADV7280_EXTENDED_OUTPUT_CONTROL_NTSCDIS		0xC5
#define ADV7280_AUTODETECT_DEFAULT			0x7f

#define ADV7280_INPUT_CONTROL_COMPOSITE_IN1		0x00
#define ADV7280_INPUT_CONTROL_AD_PAL_BG_NTSC_J_SECAM	0x00

#define ADV7280_INPUT_CONTROL_NTSC_M			0x50
#define ADV7280_INPUT_CONTROL_PAL60			0x60
#define ADV7280_INPUT_CONTROL_NTSC_443			0x70
#define ADV7280_INPUT_CONTROL_PAL_BG			0x80
#define ADV7280_INPUT_CONTROL_PAL_N			0x90
#define ADV7280_INPUT_CONTROL_PAL_M			0xa0
#define ADV7280_INPUT_CONTROL_PAL_M_PED			0xb0
#define ADV7280_INPUT_CONTROL_PAL_COMB_N		0xc0
#define ADV7280_INPUT_CONTROL_PAL_COMB_N_PED		0xd0
#define ADV7280_INPUT_CONTROL_PAL_SECAM			0xe0

#define ADV7280_STATUS1_IN_LOCK				0x01
/* Autodetection results */
#define ADV7280_STATUS1_AUTOD_MASK			0x70
#define ADV7280_STATUS1_AUTOD_NTSM_M_J			0x00
#define ADV7280_STATUS1_AUTOD_NTSC_4_43			0x10
#define ADV7280_STATUS1_AUTOD_PAL_M			0x20
#define ADV7280_STATUS1_AUTOD_PAL_60			0x30
#define ADV7280_STATUS1_AUTOD_PAL_B_G			0x40
#define ADV7280_STATUS1_AUTOD_SECAM			0x50
#define ADV7280_STATUS1_AUTOD_PAL_COMB			0x60
#define ADV7280_STATUS1_AUTOD_SECAM_525			0x70

static const struct v4l2_queryctrl adv7280_qctrl[] = {
	{
	.id = V4L2_CID_BRIGHTNESS,
	.type = V4L2_CTRL_TYPE_INTEGER,
	.name = "Brightness",
	.minimum = 0,
	.maximum = 255,
	.step = 1,
	.default_value = 127,
	.flags = 0,
	}, {
	.id = V4L2_CID_SATURATION,
	.type = V4L2_CTRL_TYPE_INTEGER,
	.name = "Saturation",
	.minimum = 0,
	.maximum = 255,
	.step = 0x1,
	.default_value = 127,
	.flags = 0,
	}
};

static inline void adv7280_power_down(int enable)
{
	if (gpio_is_valid(pwn_gpio)) {
		gpio_set_value_cansleep(pwn_gpio, !enable);
		msleep(2);
	}
}

static int adv7280_regulator_enable(struct device *dev)
{
	int ret = 0;

	dvddio_regulator = devm_regulator_get(dev, "DOVDD");

	if (!IS_ERR(dvddio_regulator)) {
		regulator_set_voltage(dvddio_regulator,
				      ADV7280_VOLTAGE_DIGITAL_IO,
				      ADV7280_VOLTAGE_DIGITAL_IO);
		ret = regulator_enable(dvddio_regulator);
		if (ret) {
			dev_err(dev, "set io voltage failed\n");
			return ret;
		} else {
			dev_dbg(dev, "set io voltage ok\n");
		}
	} else {
		dev_warn(dev, "cannot get io voltage\n");
		dvddio_regulator = NULL;
	}

	dvdd_regulator = devm_regulator_get(dev, "DVDD");
	if (!IS_ERR(dvdd_regulator)) {
		regulator_set_voltage(dvdd_regulator,
				      ADV7280_VOLTAGE_DIGITAL_CORE,
				      ADV7280_VOLTAGE_DIGITAL_CORE);
		ret = regulator_enable(dvdd_regulator);
		if (ret) {
			dev_err(dev, "set core voltage failed\n");
			return ret;
		} else {
			dev_dbg(dev, "set core voltage ok\n");
		}
	} else {
		dev_warn(dev, "cannot get core voltage\n");
		dvdd_regulator = NULL;
	}

	avdd_regulator = devm_regulator_get(dev, "AVDD");
	if (!IS_ERR(avdd_regulator)) {
		regulator_set_voltage(avdd_regulator,
				      ADV7280_VOLTAGE_ANALOG,
				      ADV7280_VOLTAGE_ANALOG);
		ret = regulator_enable(avdd_regulator);
		if (ret) {
			dev_err(dev, "set analog voltage failed\n");
			return ret;
		} else {
			dev_dbg(dev, "set analog voltage ok\n");
		}
	} else {
		dev_warn(dev, "cannot get analog voltage\n");
		avdd_regulator = NULL;
	}

	pvdd_regulator = devm_regulator_get(dev, "PVDD");
	if (!IS_ERR(pvdd_regulator)) {
		regulator_set_voltage(pvdd_regulator,
				      ADV7280_VOLTAGE_PLL,
				      ADV7280_VOLTAGE_PLL);
		ret = regulator_enable(pvdd_regulator);
		if (ret) {
			dev_err(dev, "set pll voltage failed\n");
			return ret;
		} else {
			dev_dbg(dev, "set pll voltage ok\n");
		}
	} else {
		dev_warn(dev, "cannot get pll voltage\n");
		pvdd_regulator = NULL;
	}

	return ret;
}

static inline int adv7280_read(u8 reg)
{
	int val;

	val = i2c_smbus_read_byte_data(adv7280_data.sen.i2c_client, reg);
	if (val < 0) {
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"%s:read reg error: reg=%2x\n", __func__, reg);
		return val;
	}

	dev_dbg(&adv7280_data.sen.i2c_client->dev,
		"%s:read reg 0x%2x, val: 0x%2x\n", __func__, reg, val);

	return val;
}

static int adv7280_write_reg(u8 reg, u8 val)
{
	s32 ret;

	dev_dbg(&adv7280_data.sen.i2c_client->dev,
		"%s:write reg 0x%2x, val: 0x%2x\n", __func__, reg, val);

	ret = i2c_smbus_write_byte_data(adv7280_data.sen.i2c_client, reg, val);
	if (ret < 0) {
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"%s:write reg error:reg=%2x,val=%2x\n", __func__,
			reg, val);
		return ret;
	}

	return 0;
}

static int adv7280_write_reg_with_client(struct i2c_client *client,
				u8 reg, u8 val)
{
	s32 ret;

	dev_dbg(&client->dev,
		"%s:write reg 0x%2x, val: 0x%2x\n", __func__, reg, val);

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		dev_dbg(&client->dev,
			"%s:write reg error:reg=%2x,val=%2x\n", __func__,
			reg, val);
		return ret;
	}

	return 0;
}

static void adv7280_get_std(v4l2_std_id *std)
{
	int status_1, standard, idx;
	bool locked = 0;

	dev_dbg(&adv7280_data.sen.i2c_client->dev, "In %s\n", __func__);

	status_1 = adv7280_read(ADV7280_STATUS_1);
	locked = status_1 & ADV7280_STATUS1_IN_LOCK;
	standard = status_1 & ADV7280_STATUS1_AUTOD_MASK;

	mutex_lock(&mutex);
	*std = V4L2_STD_ALL;
	idx = ADV7280_NOT_LOCKED;
	if (locked) {
		if (standard == ADV7280_STATUS1_AUTOD_PAL_B_G) {
			*std = V4L2_STD_PAL;
			idx = ADV7280_PAL;
		} else if (standard == ADV7280_STATUS1_AUTOD_NTSM_M_J) {
			*std = V4L2_STD_NTSC;
			idx = ADV7280_NTSC;
		}
	}
	mutex_unlock(&mutex);

	/* This assumes autodetect which this device uses. */
	if (*std != adv7280_data.std_id) {
		video_idx = idx;
		adv7280_data.std_id = *std;
		adv7280_data.sen.pix.width = video_fmts[video_idx].raw_width;
		adv7280_data.sen.pix.height = video_fmts[video_idx].raw_height;
	}
}

static int ioctl_g_ifparm(struct v4l2_int_device *s, struct v4l2_ifparm *p)
{
	struct device *dev = &adv7280_data.sen.i2c_client->dev;

	dev_dbg(dev, "adv7280: %s\n", __func__);

	if (s == NULL) {
		dev_err(dev, "  ERROR!! no slave device set!\n");
		return -1;
	}

	memset(p, 0, sizeof(*p));
	p->if_type = V4L2_IF_TYPE_BT656; /* This is the only possibility. */
#if 0
	p->u.bt656.mode = V4L2_IF_TYPE_BT656_MODE_NOBT_8BIT;
	p->u.bt656.nobt_hs_inv = 1;
	p->u.bt656.bt_sync_correct = 1;
#else
	p->u.bt656.mode = V4L2_IF_TYPE_BT656_MODE_NOBT_8BIT;
	p->u.bt656.nobt_hs_inv = 0;
	p->u.bt656.bt_sync_correct = 0;
#ifdef HW_DEINT
	p->u.bt656.clock_curr = 1;  /* BT656 de-interlace clock mode */
#endif
#endif
	/* ADV7280 has a dedicated clock so no clock settings needed. */

	return 0;
}

static int ioctl_s_power(struct v4l2_int_device *s, int on)
{
	struct sensor *sensor = s->priv;

	dev_dbg(&adv7280_data.sen.i2c_client->dev, "adv7280: %s\n", __func__);

	if (on && !sensor->sen.on) {
		if (adv7280_write_reg(ADV7280_POWER_MANAGEMENT, 0x04) != 0)
			return -EIO;

		msleep(400);

	} else if (!on && sensor->sen.on) {
		if (adv7280_write_reg(ADV7280_POWER_MANAGEMENT, 0x24) != 0)
			return -EIO;
	}

	sensor->sen.on = on;

	return 0;
}

static int ioctl_g_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct sensor *sensor = s->priv;
	struct v4l2_captureparm *cparm = &a->parm.capture;
	struct device *dev = &adv7280_data.sen.i2c_client->dev;

	dev_dbg(dev, "adv7280: %s\n", __func__);

	switch (a->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		dev_dbg(dev, "   type is V4L2_BUF_TYPE_VIDEO_CAPTURE\n");
		memset(a, 0, sizeof(*a));
		a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cparm->capability = sensor->sen.streamcap.capability;
		cparm->timeperframe = sensor->sen.streamcap.timeperframe;
		cparm->capturemode = sensor->sen.streamcap.capturemode;
		break;

	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		break;

	default:
		dev_dbg(dev, "ioctl_g_parm:type is unknown %d\n", a->type);
		break;
	}

	return 0;
}

static int ioctl_s_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct device *dev = &adv7280_data.sen.i2c_client->dev;

	dev_dbg(dev, "adv7280: %s\n", __func__);

	switch (a->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		break;

	default:
		dev_dbg(dev, "   type is unknown - %d\n", a->type);
		break;
	}

	return 0;
}

static int ioctl_g_fmt_cap(struct v4l2_int_device *s, struct v4l2_format *f)
{
	struct device *dev = &adv7280_data.sen.i2c_client->dev;
	struct sensor *sensor = s->priv;
	v4l2_std_id std;

	dev_dbg(&adv7280_data.sen.i2c_client->dev, "adv7280: %s\n", __func__);

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		dev_dbg(dev, "   Returning size of %dx%d\n",
			 sensor->sen.pix.width, sensor->sen.pix.height);
		f->fmt.pix = sensor->sen.pix;
		break;

	case V4L2_BUF_TYPE_PRIVATE:
		adv7280_get_std(&std);
		f->fmt.pix.pixelformat = (u32)std;
		break;

	default:
		f->fmt.pix = sensor->sen.pix;
		break;
	}

	return 0;
}

static int ioctl_queryctrl(struct v4l2_int_device *s,
			   struct v4l2_queryctrl *qc)
{
	int i;

	dev_dbg(&adv7280_data.sen.i2c_client->dev, "adv7280: %s\n", __func__);

	for (i = 0; i < ARRAY_SIZE(adv7280_qctrl); i++)
		if (qc->id && qc->id == adv7280_qctrl[i].id) {
			*qc = adv7280_qctrl[i];
			return 0;
		}

	return -EINVAL;
}

static int ioctl_g_ctrl(struct v4l2_int_device *s, struct v4l2_control *vc)
{
	int ret = 0;
	int sat = 0;

	dev_dbg(&adv7280_data.sen.i2c_client->dev, "adv7280: %s\n", __func__);

	switch (vc->id) {
	case V4L2_CID_BRIGHTNESS:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_BRIGHTNESS\n");
		adv7280_data.sen.brightness = adv7280_read(ADV7280_BRIGHTNESS);
		vc->value = adv7280_data.sen.brightness;
		break;
	case V4L2_CID_CONTRAST:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_CONTRAST\n");
		vc->value = adv7280_data.sen.contrast;
		break;
	case V4L2_CID_SATURATION:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_SATURATION\n");
		sat = adv7280_read(ADV7280_SD_SATURATION_CB);
		adv7280_data.sen.saturation = sat;
		vc->value = adv7280_data.sen.saturation;
		break;
	case V4L2_CID_HUE:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_HUE\n");
		vc->value = adv7280_data.sen.hue;
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_AUTO_WHITE_BALANCE\n");
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_DO_WHITE_BALANCE\n");
		break;
	case V4L2_CID_RED_BALANCE:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_RED_BALANCE\n");
		vc->value = adv7280_data.sen.red;
		break;
	case V4L2_CID_BLUE_BALANCE:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_BLUE_BALANCE\n");
		vc->value = adv7280_data.sen.blue;
		break;
	case V4L2_CID_GAMMA:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_GAMMA\n");
		break;
	case V4L2_CID_EXPOSURE:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_EXPOSURE\n");
		vc->value = adv7280_data.sen.ae_mode;
		break;
	case V4L2_CID_AUTOGAIN:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_AUTOGAIN\n");
		break;
	case V4L2_CID_GAIN:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_GAIN\n");
		break;
	case V4L2_CID_HFLIP:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_HFLIP\n");
		break;
	case V4L2_CID_VFLIP:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_VFLIP\n");
		break;
	default:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   Default case\n");
		vc->value = 0;
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int ioctl_s_ctrl(struct v4l2_int_device *s, struct v4l2_control *vc)
{
	int retval = 0;
	u8 tmp;

	dev_dbg(&adv7280_data.sen.i2c_client->dev, "adv7280: %s\n", __func__);

	switch (vc->id) {
	case V4L2_CID_BRIGHTNESS:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_BRIGHTNESS\n");
		tmp = vc->value;
		adv7280_write_reg(ADV7280_BRIGHTNESS, tmp);
		adv7280_data.sen.brightness = vc->value;
		break;
	case V4L2_CID_CONTRAST:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_CONTRAST\n");
		break;
	case V4L2_CID_SATURATION:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_SATURATION\n");
		tmp = vc->value;
		adv7280_write_reg(ADV7280_SD_SATURATION_CB, tmp);
		adv7280_write_reg(ADV7280_SD_SATURATION_CR, tmp);
		adv7280_data.sen.saturation = vc->value;
		break;
	case V4L2_CID_HUE:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_HUE\n");
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_AUTO_WHITE_BALANCE\n");
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_DO_WHITE_BALANCE\n");
		break;
	case V4L2_CID_RED_BALANCE:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_RED_BALANCE\n");
		break;
	case V4L2_CID_BLUE_BALANCE:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_BLUE_BALANCE\n");
		break;
	case V4L2_CID_GAMMA:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_GAMMA\n");
		break;
	case V4L2_CID_EXPOSURE:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_EXPOSURE\n");
		break;
	case V4L2_CID_AUTOGAIN:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_AUTOGAIN\n");
		break;
	case V4L2_CID_GAIN:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_GAIN\n");
		break;
	case V4L2_CID_HFLIP:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_HFLIP\n");
		break;
	case V4L2_CID_VFLIP:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   V4L2_CID_VFLIP\n");
		break;
	default:
		dev_dbg(&adv7280_data.sen.i2c_client->dev,
			"   Default case\n");
		retval = -EINVAL;
		break;
	}

	return retval;
}

static int ioctl_enum_framesizes(struct v4l2_int_device *s,
				 struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index >= 1)
		return -EINVAL;

	fsize->discrete.width = video_fmts[video_idx].active_width;
	fsize->discrete.height  = video_fmts[video_idx].active_height;

	return 0;
}

static int ioctl_enum_frameintervals(struct v4l2_int_device *s,
					 struct v4l2_frmivalenum *fival)
{
	struct video_fmt_t fmt;
	int i;

	if (fival->index != 0)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(video_fmts) - 1; i++) {
		fmt = video_fmts[i];
		if (fival->width  == fmt.active_width &&
		    fival->height == fmt.active_height) {
			fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
			fival->discrete.numerator = 1;
			fival->discrete.denominator = fmt.frame_rate;
			return 0;
		}
	}

	return -EINVAL;
}

static int ioctl_g_chip_ident(struct v4l2_int_device *s, int *id)
{
	((struct v4l2_dbg_chip_ident *)id)->match.type =
					V4L2_CHIP_MATCH_I2C_DRIVER;
	strcpy(((struct v4l2_dbg_chip_ident *)id)->match.name,
						"adv7280_decoder");
	((struct v4l2_dbg_chip_ident *)id)->ident = V4L2_IDENT_ADV7280;

	return 0;
}

static int ioctl_init(struct v4l2_int_device *s)
{
	dev_dbg(&adv7280_data.sen.i2c_client->dev, "adv7280: %s\n", __func__);

	return 0;
}

static int ioctl_dev_init(struct v4l2_int_device *s)
{
	dev_dbg(&adv7280_data.sen.i2c_client->dev, "adv7280: %s\n", __func__);

	return 0;
}

static struct v4l2_int_ioctl_desc adv7280_ioctl_desc[] = {

	{vidioc_int_dev_init_num, (v4l2_int_ioctl_func*)ioctl_dev_init},

	{vidioc_int_s_power_num, (v4l2_int_ioctl_func*)ioctl_s_power},
	{vidioc_int_g_ifparm_num, (v4l2_int_ioctl_func*)ioctl_g_ifparm},
	{vidioc_int_init_num, (v4l2_int_ioctl_func*)ioctl_init},

	{vidioc_int_g_fmt_cap_num, (v4l2_int_ioctl_func*)ioctl_g_fmt_cap},

	{vidioc_int_g_parm_num, (v4l2_int_ioctl_func*)ioctl_g_parm},
	{vidioc_int_s_parm_num, (v4l2_int_ioctl_func*)ioctl_s_parm},
	{vidioc_int_queryctrl_num, (v4l2_int_ioctl_func*)ioctl_queryctrl},
	{vidioc_int_g_ctrl_num, (v4l2_int_ioctl_func*)ioctl_g_ctrl},
	{vidioc_int_s_ctrl_num, (v4l2_int_ioctl_func*)ioctl_s_ctrl},
	{vidioc_int_enum_framesizes_num,
				(v4l2_int_ioctl_func *)ioctl_enum_framesizes},
	{vidioc_int_enum_frameintervals_num,
				(v4l2_int_ioctl_func *)
				ioctl_enum_frameintervals},
	{vidioc_int_g_chip_ident_num,
				(v4l2_int_ioctl_func *)ioctl_g_chip_ident},
};

static struct v4l2_int_slave adv7280_slave = {
	.ioctls = adv7280_ioctl_desc,
	.num_ioctls = ARRAY_SIZE(adv7280_ioctl_desc),
};

static struct v4l2_int_device adv7280_int_device = {
	.module = THIS_MODULE,
	.name = "adv7280",
	.type = v4l2_int_type_slave,
	.u = {
		.slave = &adv7280_slave,
	},
};

static int adv7280_hard_reset(void)
{
	int ret;

	struct i2c_client vpp_client = {
		.flags = adv7280_data.sen.i2c_client->flags,
		.addr = VPP_SLAVE_ADDRESS,
		.name = "ADV7280_VPP",
		.adapter = adv7280_data.sen.i2c_client->adapter,
		.dev = adv7280_data.sen.i2c_client->dev,
	};

	dev_dbg(&adv7280_data.sen.i2c_client->dev,
		"In %s\n", __func__);

	/* Reset */
	ret = adv7280_write_reg(ADV7280_POWER_MANAGEMENT, 0xA0);
	if (ret < 0)
		goto error;
	msleep(10);

	/* Initialize adv7280 */
	/* Exit Power Down Mode */
	ret = adv7280_write_reg(ADV7280_POWER_MANAGEMENT, 0x00);
	if (ret < 0)
		goto error;

	/* analog devices recommends */
	ret = adv7280_write_reg(ADV7280_ADI_CONTROL_1, 0x80);
	if (ret < 0)
		goto error;

	/* analog devices recommends */
	ret = adv7280_write_reg(0x9C, 0x00);
	if (ret < 0)
		goto error;

	/* analog devices recommends */
	ret = adv7280_write_reg(0x9C, 0xFF);
	if (ret < 0)
		goto error;

	/* Enter User Sub Map */
	ret = adv7280_write_reg(ADV7280_ADI_CONTROL_1, 0x00);
	if (ret < 0)
		goto error;

	/* Enable Pixel & Sync output drivers */
	ret = adv7280_write_reg(ADV7280_OUTPUT_CONTROL, 0x0C);
	if (ret < 0)
		goto error;

	/* Power-up INTRQ, HS & VS pads */
	ret = adv7280_write_reg(ADV7280_EXTENDED_OUTPUT_CONTROL, 0x07);
	if (ret < 0)
		goto error;

	/* Enable SH1 */
	/*
	ret = adv7280_write_reg(ADV7280_SHAPING_FILTER_CONTROL_1, 0x41);
	if (ret < 0)
		goto error;
	*/

	/* Disable comb filtering */
	/*
	ret = adv7280_write_reg(0x39, 0x24);
	if (ret < 0)
		goto error;
	*/

	/* Enable LLC output driver */
	ret = adv7280_write_reg(ADV7280_ADI_CONTROL_2, 0x40);
	if (ret < 0)
		goto error;

	/* VSYNC on VS/FIELD/SFL pin */
	ret = adv7280_write_reg(ADV7280_OUTPUT_SYNC_SELECT_2, 0x01);
	if (ret < 0)
		goto error;

	/* Enable autodetection */
	ret = adv7280_write_reg(ADV7280_INPUT_CONTROL,
		ADV7280_INPUT_CONTROL_AD_PAL_BG_NTSC_J_SECAM |
				(ADV7280_INPUT_CONTROL_COMPOSITE_IN1 +
				 0));
	if (ret < 0)
		goto error;

	ret = adv7280_write_reg(ADV7280_AUTODETECT_ENABLE,
		ADV7280_AUTODETECT_DEFAULT);
	if (ret < 0)
		goto error;

	/* ITU-R BT.656-4 compatible */
	/*ret = adv7280_write_reg(ADV7280_EXTENDED_OUTPUT_CONTROL,
		ADV7280_EXTENDED_OUTPUT_CONTROL_NTSCDIS);
	if (ret < 0)
		goto error;
	*/

	/* analog devices recommends */
	ret = adv7280_write_reg(0x52, 0xCD);
	if (ret < 0)
		goto error;

	/* analog devices recommends */
	ret = adv7280_write_reg(0x80, 0x51);
	if (ret < 0)
		goto error;

	/* analog devices recommends */
	ret = adv7280_write_reg(0x81, 0x51);
	if (ret < 0)
		goto error;

	/* analog devices recommends */
	ret = adv7280_write_reg(0x82, 0x68);
	if (ret < 0)
		goto error;

#ifdef HW_DEINT
	/* Set VPP Map */
	ret = adv7280_write_reg(ADV7280_VPP_SLAVE_ADDRESS, (VPP_SLAVE_ADDRESS << 1));
	if (ret < 0)
		goto error;

	/* VPP - not documented */
	ret = adv7280_write_reg_with_client(&vpp_client,
		0xA3, 0x00);
	if (ret < 0)
		goto error;

	/* VPP - Enbable Advanced Timing Mode */
	ret = adv7280_write_reg_with_client(&vpp_client,
		VPP_ADV_TIMING_MODE_EN, 0x00);
	if (ret < 0)
		goto error;

	/* VPP - Enable Deinterlacer */
	ret = adv7280_write_reg_with_client(&vpp_client,
		VPP_I2C_DEINT_ENABLE, 0x80);
	if (ret < 0)
		goto error;
#endif

	return 0;

error:
	return ret;
}

static int adv7280_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int ret = 0;
	struct pinctrl *pinctrl;
	struct device *dev = &client->dev;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(dev, "Required functionality not supported by I2C adapter\n");
		return -EIO;
	}

	v4l_info(client, "chip found @ 0x%02x (%s)\n",
		client->addr << 1, client->adapter->name);

	pinctrl = devm_pinctrl_get_select_default(dev);
	if (IS_ERR(pinctrl)) {
		dev_err(dev, "setup pinctrl failed\n");
		return PTR_ERR(pinctrl);
	}

	pwn_gpio = of_get_named_gpio(dev->of_node, "pwn-gpios", 0);
	if (!gpio_is_valid(pwn_gpio))
		dev_warn(dev, "no sensor pwdn pin available\n");
	else {
		ret = devm_gpio_request_one(dev, pwn_gpio, GPIOF_OUT_INIT_HIGH,
					"adv7280_pwdn");
		if (ret < 0)
			dev_err(dev, "no power pin available!\n");
	}

	adv7280_regulator_enable(dev);

	adv7280_power_down(0);

	msleep(1);

	memset(&adv7280_data, 0, sizeof(adv7280_data));
	adv7280_data.sen.i2c_client = client;
	adv7280_data.sen.streamcap.timeperframe.denominator = 30;
	adv7280_data.sen.streamcap.timeperframe.numerator = 1;
	adv7280_data.std_id = V4L2_STD_ALL;
	video_idx = ADV7280_NOT_LOCKED;
	adv7280_data.sen.pix.width = video_fmts[video_idx].raw_width;
	adv7280_data.sen.pix.height = video_fmts[video_idx].raw_height;
	adv7280_data.sen.pix.pixelformat = V4L2_PIX_FMT_UYVY;  /* YUV422 */
	adv7280_data.sen.pix.priv = 1;  /* 1 is used to indicate TV in */
	adv7280_data.sen.on = true;

	adv7280_data.sen.sensor_clk = devm_clk_get(dev, "csi_mclk");
	if (IS_ERR(adv7280_data.sen.sensor_clk)) {
		dev_err(dev, "get mclk failed\n");
		return PTR_ERR(adv7280_data.sen.sensor_clk);
	}

	ret = of_property_read_u32(dev->of_node, "mclk",
					&adv7280_data.sen.mclk);
	if (ret) {
		dev_err(dev, "mclk frequency is invalid\n");
		return ret;
	}

	ret = of_property_read_u32(
		dev->of_node, "mclk_source",
		(u32 *) &(adv7280_data.sen.mclk_source));
	if (ret) {
		dev_err(dev, "mclk_source invalid\n");
		return ret;
	}

	ret = of_property_read_u32(dev->of_node, "csi_id",
					&(adv7280_data.sen.csi));
	if (ret) {
		dev_err(dev, "csi_id invalid\n");
		return ret;
	}

	/* ADV7280 is always parallel IF */
	adv7280_data.sen.mipi_camera = 0;

	clk_prepare_enable(adv7280_data.sen.sensor_clk);

	adv7280_data.rev_id = adv7280_read(ADV7280_IDENT);
	switch (adv7280_data.rev_id) {
	case 0x42:
		dev_dbg(dev,
			"%s:Analog Device adv7280 ident %2X detected!\n",
			__func__, adv7280_data.rev_id);
		break;
	default:
		dev_err(dev,
			"%s:Analog Device adv7280 not detected %d!\n", __func__,
			adv7280_data.rev_id);
		return -ENODEV;

	}

	dev_dbg(dev, "   type is %d (expect %d)\n",
		 adv7280_int_device.type, v4l2_int_type_slave);
	dev_dbg(dev, "   num ioctls is %d\n",
		 adv7280_int_device.u.slave->num_ioctls);

	ret = adv7280_hard_reset();
	if (ret) {
		dev_err(dev, "error resetting adv7280\n");
		return ret;
	}

	/* This function attaches this structure to the /dev/video0 device.
	 * The pointer in priv points to the adv7280_data structure here.*/
	adv7280_int_device.priv = &adv7280_data;
	ret = v4l2_int_device_register(&adv7280_int_device);

	clk_disable_unprepare(adv7280_data.sen.sensor_clk);

	return 0;
}

static int adv7280_detach(struct i2c_client *client)
{
	dev_dbg(&adv7280_data.sen.i2c_client->dev,
		"%s:Removing %s video decoder @ 0x%02X from adapter %s\n",
		__func__, IF_NAME, client->addr << 1, client->adapter->name);

	adv7280_write_reg(ADV7280_POWER_MANAGEMENT, 0x24);

	if (dvddio_regulator)
		regulator_disable(dvddio_regulator);

	if (dvdd_regulator)
		regulator_disable(dvdd_regulator);

	if (avdd_regulator)
		regulator_disable(avdd_regulator);

	if (pvdd_regulator)
		regulator_disable(pvdd_regulator);

	v4l2_int_device_unregister(&adv7280_int_device);

	return 0;
}

static const struct i2c_device_id adv7280_id[] = {
	{"adv7280", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, adv7280_id);

static struct i2c_driver adv7280_i2c_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "adv7280",
	},
	.probe = adv7280_probe,
	.remove = adv7280_detach,
	.id_table = adv7280_id,
};
module_i2c_driver(adv7280_i2c_driver);

MODULE_AUTHOR("Freescale Semiconductor");
MODULE_DESCRIPTION("Analog Device ADV7280 video decoder driver");
MODULE_LICENSE("GPL");
