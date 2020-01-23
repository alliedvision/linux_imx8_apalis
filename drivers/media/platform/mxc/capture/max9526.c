/*
 * Copyright 2005-2013 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2015 Toradex AG All Rights Reserved.
 * copied and adapted from adv7180.c
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file max9526.c
 *
 * @brief Maxim Integrated MAX9526 video decoder functions
 *
 * @ingroup Camera
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

#if 0
#undef dev_dbg
#define dev_dbg(dev, format, arg...) {dev_printk(KERN_ERR, dev, format, ##arg);}
#undef pr_debug
#define pr_debug(fmt, ...) printk(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#endif

#define MAX9526_VOLTAGE_ANALOG               1800000
#define MAX9526_VOLTAGE_DIGITAL_CORE         1800000
#define MAX9526_VOLTAGE_DIGITAL_IO           3300000
#define MAX9526_VOLTAGE_PLL                  1800000

static struct regulator *dvddio_regulator;
static struct regulator *dvdd_regulator;
static struct regulator *avdd_regulator;

static int max9526_probe(struct i2c_client *adapter,
			 const struct i2c_device_id *id);
static int max9526_detach(struct i2c_client *client);

static const struct i2c_device_id max9526_id[] = {
	{"max9526", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, max9526_id);

static struct i2c_driver max9526_i2c_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "max9526",
		   },
	.probe = max9526_probe,
	.remove = max9526_detach,
	.id_table = max9526_id,
};

/*!
 * Maintains the information on the current state of the sensor.
 */
struct sensor {
	struct sensor_data sen;
	v4l2_std_id std_id;
} max9526_data;


/*! List of input video formats supported. The video formats is corresponding
 * with v4l2 id in video_fmt_t
 */
typedef enum {
	MAX9526_NTSC = 0,	/*!< Locked on (M) NTSC video signal. */
	MAX9526_PAL,		/*!< (B, G, H, I, N)PAL video signal. */
	MAX9526_NOT_LOCKED,	/*!< Not locked on a signal. */
} video_fmt_idx;

/*! Number of video standards supported (including 'not locked' signal). */
#define MAX9526_STD_MAX		(MAX9526_PAL + 1)

/*! Video format structure. */
typedef struct {
	int v4l2_id;		/*!< Video for linux ID. */
	char name[16];		/*!< Name (e.g., "NTSC", "PAL", etc.) */
	u16 raw_width;		/*!< Raw width. */
	u16 raw_height;		/*!< Raw height. */
	u16 active_width;	/*!< Active width. */
	u16 active_height;	/*!< Active height. */
} video_fmt_t;

/*! Description of video formats supported.
 *
 *  PAL: raw=720x625, active=720x576.
 *  NTSC: raw=720x525, active=720x480.
 */
static video_fmt_t video_fmts[] = {
	{			/*! NTSC */
	 .v4l2_id = V4L2_STD_NTSC,
	 .name = "NTSC",
	 .raw_width = 720,	/* SENS_FRM_WIDTH */
	 .raw_height = 525,	/* SENS_FRM_HEIGHT */
	 .active_width = 720,	/* ACT_FRM_WIDTH plus 1 */
	 .active_height = 480,	/* ACT_FRM_WIDTH plus 1 */
	 },
	{			/*! (B, G, H, I, N) PAL */
	 .v4l2_id = V4L2_STD_PAL,
	 .name = "PAL",
	 .raw_width = 720,
	 .raw_height = 625,
	 .active_width = 720,
	 .active_height = 576,
	 },
	{			/*! Unlocked standard */
	 .v4l2_id = V4L2_STD_ALL,
	 .name = "Autodetect",
	 .raw_width = 720,
	 .raw_height = 625,
	 .active_width = 720,
	 .active_height = 576,
	 },
};

/*!* Standard index of MAX9526. */
static video_fmt_idx video_idx = MAX9526_PAL;

/*! @brief This mutex is used to provide mutual exclusion.
 *
 *  Create a mutex that can be used to provide mutually exclusive
 *  read/write access to the globally accessible data structures
 *  and variables that were defined above.
 */
static DEFINE_MUTEX(mutex);

#define IF_NAME                    "max9526"
/* registers */
#define REG_STATUS_0                            0x00
#define REG_STATUS_1                            0x01
#define REG_IRQMASK_0                           0x02
#define REG_IRQMASK_1                           0x03
#define REG_STANDARD_SELECT_SHUTDOWN_CONTROL    0x04
#define REG_CONTRAST                            0x05
#define REG_BRIGHTNESS                          0x06
#define REG_HUE                                 0x07
#define REG_SATURATION                          0x08
#define REG_VIDEO_INPUT_SELECT_AND_CLAMP        0x09
#define REG_GAIN_CONTROL                        0x0A
#define REG_COLOR_KILL                          0x0B
#define REG_OUTPUT_TEST_SIGNAL                  0x0C
#define REG_CLOCK_AND_OUTPUT                    0x0D
#define REG_PLL_CONTROL                         0x0E
#define REG_MISCELLANEOUS                       0x0F

#define I2C_RETRY_COUNT                         5

/* supported controls */
/* This hasn't been fully implemented yet.
 * This is how it should work, though. */
static struct v4l2_queryctrl max9526_qctrl[] = {
	{
	.id = V4L2_CID_BRIGHTNESS,
	.type = V4L2_CTRL_TYPE_INTEGER,
	.name = "Brightness",
	.minimum = 0,		/* check this value */
	.maximum = 255,		/* check this value */
	.step = 1,		/* check this value */
	.default_value = 127,	/* check this value */
	.flags = 0,
	}, {
	.id = V4L2_CID_SATURATION,
	.type = V4L2_CTRL_TYPE_INTEGER,
	.name = "Saturation",
	.minimum = 0,		/* check this value */
	.maximum = 255,		/* check this value */
	.step = 0x1,		/* check this value */
	.default_value = 127,	/* check this value */
	.flags = 0,
	}
};

static inline void max9526_power_down(int enable)
{
}

static int max9526_regulator_enable(struct device *dev)
{
	int ret = 0;

	dvddio_regulator = devm_regulator_get(dev, "DVDDIO");

	if (!IS_ERR(dvddio_regulator)) {
		regulator_set_voltage(dvddio_regulator,
				      MAX9526_VOLTAGE_DIGITAL_IO,
				      MAX9526_VOLTAGE_DIGITAL_IO);
		ret = regulator_enable(dvddio_regulator);
		if (ret) {
			dev_err(dev, "set io voltage failed\n");
			return ret;
		} else {
			dev_dbg(dev, "set io voltage ok\n");
		}
	} else {
		dev_warn(dev, "cannot get io voltage\n");
	}

	dvdd_regulator = devm_regulator_get(dev, "DVDD");
	if (!IS_ERR(dvdd_regulator)) {
		regulator_set_voltage(dvdd_regulator,
				      MAX9526_VOLTAGE_DIGITAL_CORE,
				      MAX9526_VOLTAGE_DIGITAL_CORE);
		ret = regulator_enable(dvdd_regulator);
		if (ret) {
			dev_err(dev, "set core voltage failed\n");
			return ret;
		} else {
			dev_dbg(dev, "set core voltage ok\n");
		}
	} else {
		dev_warn(dev, "cannot get core voltage\n");
	}

	avdd_regulator = devm_regulator_get(dev, "AVDD");
	if (!IS_ERR(avdd_regulator)) {
		regulator_set_voltage(avdd_regulator,
				      MAX9526_VOLTAGE_ANALOG,
				      MAX9526_VOLTAGE_ANALOG);
		ret = regulator_enable(avdd_regulator);
		if (ret) {
			dev_err(dev, "set analog voltage failed\n");
			return ret;
		} else {
			dev_dbg(dev, "set analog voltage ok\n");
		}
	} else {
		dev_warn(dev, "cannot get analog voltage\n");
	}

	return ret;
}


/***********************************************************************
 * I2C transfert.
 ***********************************************************************/

/*! Read one register from a MAX9526 i2c slave device.
 *
 *  @param *reg		register in the device we wish to access.
 *
 *  @return		       0 if success, an error code otherwise.
 */
static inline int __max9526_read__(u8 reg)
{
	int val;
	unsigned retry = 0;

	do {
		val = i2c_smbus_read_byte_data(max9526_data.sen.i2c_client, reg);
		if (val < 0) {
			dev_dbg(&max9526_data.sen.i2c_client->dev,
				"%s:read reg error: reg=%2x, retry %u\n", __func__, reg, retry);
			val = -1;
			i2c_recover_bus(max9526_data.sen.i2c_client->adapter);
		}
	} while ((val < 0) && (++retry < I2C_RETRY_COUNT));

	if (val < 0) {
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"%s:read reg ERROR:reg=%2x,val=%2x\n", __func__,
			reg, val);
	}

	dev_dbg(&max9526_data.sen.i2c_client->dev,
		"%s:read reg 0x%2x, val: 0x%2x\n", __func__, reg, val);

	return val;
}
static inline int max9526_read(u8 reg)
{
	int val, last_val;
	unsigned retry = 0;

	last_val = __max9526_read__(reg);

	do {
		val = __max9526_read__(reg);
		if (val != last_val) {
			dev_dbg(&max9526_data.sen.i2c_client->dev,
				"%s:read reg error: reg=%2x, retry %u\n", __func__, reg, retry);
			last_val = val;
			val = -1;
			msleep(1);
		}
	} while ((val == -1) && (++retry < I2C_RETRY_COUNT));
	return val;
}
/*! Write one register of a MAX9526 i2c slave device.
 *
 *  @param *reg		register in the device we wish to access.
 *
 *  @return		       0 if success, an error code otherwise.
 */
static int max9526_write_reg(u8 reg, u8 val)
{
	s32 ret = 0;
	unsigned retry = 0;

	dev_dbg(&max9526_data.sen.i2c_client->dev,
		"%s:write reg 0x%2x, val: 0x%2x\n", __func__, reg, val);

	do {
		ret = i2c_smbus_write_byte_data(max9526_data.sen.i2c_client, reg, val);
		if (ret < 0) {
			dev_dbg(&max9526_data.sen.i2c_client->dev,
				"%s:write reg error:reg=%2x,val=%2x, retry %u\n", __func__,
				reg, val, retry+1);
			ret = -1;
			i2c_recover_bus(max9526_data.sen.i2c_client->adapter);
		}
	} while ((ret < 0) && (++retry < I2C_RETRY_COUNT));

	if (ret < 0) {
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"%s:write reg ERROR:reg=%2x,val=%2x\n", __func__,
			reg, val);
	}

	return ret;
}

/***********************************************************************
 * mxc_v4l2_capture interface.
 ***********************************************************************/

/*!
 * Return attributes of current video standard.
 * Since this device autodetects the current standard, this function also
 * sets the values that need to be changed if the standard changes.
 * There is no set std equivalent function.
 *
 *  @return		None.
 */
static void max9526_get_std(v4l2_std_id *std)
{
	int tmp = 0;
	int idx;

	dev_dbg(&max9526_data.sen.i2c_client->dev, "In max9526_get_std\n");

	/* reset the status flag to get current state */
#if 0
	(void) max9526_read(REG_STATUS_0);
	msleep(1);
	tmp = max9526_read(REG_STATUS_0) & 0x1;
#endif
	tmp |= max9526_read(REG_STATUS_1) & 0x40;

	mutex_lock(&mutex);
	if (tmp == 0x0) {
		/* PAL */
		*std = V4L2_STD_PAL;
		idx = MAX9526_PAL;
	} else if (tmp == 0x40) {
		/*NTSC*/
		*std = V4L2_STD_NTSC;
		idx = MAX9526_NTSC;
	} else {
		*std = V4L2_STD_ALL;
		idx = MAX9526_NOT_LOCKED;
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"Got invalid video standard!\n");
	}
	mutex_unlock(&mutex);

	/* This assumes autodetect which this device uses. */
	if (*std != max9526_data.std_id) {
		video_idx = idx;
		max9526_data.std_id = *std;
		max9526_data.sen.pix.width = video_fmts[video_idx].raw_width;
		max9526_data.sen.pix.height = video_fmts[video_idx].raw_height;
	}
}

/***********************************************************************
 * IOCTL Functions from v4l2_int_ioctl_desc.
 ***********************************************************************/

/*!
 * ioctl_g_ifparm - V4L2 sensor interface handler for vidioc_int_g_ifparm_num
 * s: pointer to standard V4L2 device structure
 * p: pointer to standard V4L2 vidioc_int_g_ifparm_num ioctl structure
 *
 * Gets slave interface parameters.
 * Calculates the required xclk value to support the requested
 * clock parameters in p.  This value is returned in the p
 * parameter.
 *
 * vidioc_int_g_ifparm returns platform-specific information about the
 * interface settings used by the sensor.
 *
 * Called on open.
 */
static int ioctl_g_ifparm(struct v4l2_int_device *s, struct v4l2_ifparm *p)
{
	dev_dbg(&max9526_data.sen.i2c_client->dev, "max9526:ioctl_g_ifparm\n");

	if (s == NULL) {
		pr_err("   ERROR!! no slave device set!\n");
		return -1;
	}

	/* Initialize structure to 0s then set any non-0 values. */
	memset(p, 0, sizeof(*p));
	p->if_type = V4L2_IF_TYPE_BT656; /* This is the only possibility. */
#if 0
	p->u.bt656.mode = V4L2_IF_TYPE_BT656_MODE_NOBT_8BIT;
	p->u.bt656.nobt_hs_inv = 1;
	p->u.bt656.bt_sync_correct = 1;
#elif 0
//working for PAL
	p->u.bt656.mode = V4L2_IF_TYPE_BT656_MODE_NOBT_8BIT;
	p->u.bt656.nobt_hs_inv = 0;
	p->u.bt656.bt_sync_correct = 0;
	p->u.bt656.clock_curr = 0;  //BT656 interlace clock mode
#else
	p->u.bt656.mode = V4L2_IF_TYPE_BT656_MODE_BT_8BIT;
	p->u.bt656.nobt_hs_inv = 1;
	p->u.bt656.nobt_vs_inv = 1;
	p->u.bt656.bt_sync_correct = 0;
	p->u.bt656.clock_curr = 0;  //BT656 interlace clock mode
#endif
	/* MAX9526 has a dedicated clock so no clock settings needed. */

	return 0;
}

/*!
 * Sets the camera power.
 *
 * s  pointer to the camera device
 * on if 1, power is to be turned on.  0 means power is to be turned off
 *
 * ioctl_s_power - V4L2 sensor interface handler for vidioc_int_s_power_num
 * @s: pointer to standard V4L2 device structure
 * @on: power state to which device is to be set
 *
 * Sets devices power state to requrested state, if possible.
 * This is called on open, close, suspend and resume.
 */
static int ioctl_s_power(struct v4l2_int_device *s, int on)
{
	struct sensor *sensor = s->priv;

	dev_dbg(&max9526_data.sen.i2c_client->dev, "max9526:ioctl_s_power\n");

	if (on && !sensor->sen.on) {
		if (max9526_write_reg(REG_STANDARD_SELECT_SHUTDOWN_CONTROL, 0x10) != 0)
			return -EIO;

		/*
		 * FIXME:Additional 400ms to wait the chip to be stable?
		 * This is a workaround for preview scrolling issue.
		 * Taken from ADV7180, really needed on MAX9526??
		 */
		msleep(400);

	} else if (!on && sensor->sen.on) {
		if (max9526_write_reg(REG_STANDARD_SELECT_SHUTDOWN_CONTROL, 0x18) != 0)
			return -EIO;
	}

	sensor->sen.on = on;

	return 0;
}

/*!
 * ioctl_g_parm - V4L2 sensor interface handler for VIDIOC_G_PARM ioctl
 * @s: pointer to standard V4L2 device structure
 * @a: pointer to standard V4L2 VIDIOC_G_PARM ioctl structure
 *
 * Returns the sensor's video CAPTURE parameters.
 */
static int ioctl_g_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct sensor *sensor = s->priv;
	struct v4l2_captureparm *cparm = &a->parm.capture;

	dev_dbg(&max9526_data.sen.i2c_client->dev, "In max9526:ioctl_g_parm\n");

	switch (a->type) {
	/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		pr_debug("   type is V4L2_BUF_TYPE_VIDEO_CAPTURE\n");
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
		pr_debug("ioctl_g_parm:type is unknown %d\n", a->type);
		break;
	}

	return 0;
}

/*!
 * ioctl_s_parm - V4L2 sensor interface handler for VIDIOC_S_PARM ioctl
 * @s: pointer to standard V4L2 device structure
 * @a: pointer to standard V4L2 VIDIOC_S_PARM ioctl structure
 *
 * Configures the sensor to use the input parameters, if possible.  If
 * not possible, reverts to the old parameters and returns the
 * appropriate error code.
 *
 * This driver cannot change these settings.
 */
static int ioctl_s_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	dev_dbg(&max9526_data.sen.i2c_client->dev, "In max9526:ioctl_s_parm\n");

	switch (a->type) {
	/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		break;

	default:
		pr_debug("   type is unknown - %d\n", a->type);
		break;
	}

	return 0;
}

/*!
 * ioctl_g_fmt_cap - V4L2 sensor interface handler for ioctl_g_fmt_cap
 * @s: pointer to standard V4L2 device structure
 * @f: pointer to standard V4L2 v4l2_format structure
 *
 * Returns the sensor's current pixel format in the v4l2_format
 * parameter.
 */
static int ioctl_g_fmt_cap(struct v4l2_int_device *s, struct v4l2_format *f)
{
	struct sensor *sensor = s->priv;

	dev_dbg(&max9526_data.sen.i2c_client->dev, "max9526:ioctl_g_fmt_cap\n");
	//dump_stack();

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		pr_debug("   Returning size of %dx%d\n",
			 sensor->sen.pix.width, sensor->sen.pix.height);
		f->fmt.pix = sensor->sen.pix;
		break;

	case V4L2_BUF_TYPE_PRIVATE: {
		v4l2_std_id std;
		max9526_get_std(&std);
		f->fmt.pix.pixelformat = (u32)std;
		}
		break;

	default:
		f->fmt.pix = sensor->sen.pix;
		break;
	}

	return 0;
}

/*!
 * ioctl_queryctrl - V4L2 sensor interface handler for VIDIOC_QUERYCTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @qc: standard V4L2 VIDIOC_QUERYCTRL ioctl structure
 *
 * If the requested control is supported, returns the control information
 * from the video_control[] array.  Otherwise, returns -EINVAL if the
 * control is not supported.
 */
static int ioctl_queryctrl(struct v4l2_int_device *s,
			   struct v4l2_queryctrl *qc)
{
	int i;

	dev_dbg(&max9526_data.sen.i2c_client->dev, "max9526:ioctl_queryctrl\n");

	for (i = 0; i < ARRAY_SIZE(max9526_qctrl); i++)
		if (qc->id && qc->id == max9526_qctrl[i].id) {
			memcpy(qc, &(max9526_qctrl[i]),
				sizeof(*qc));
			return 0;
		}

	return -EINVAL;
}

/*!
 * ioctl_g_ctrl - V4L2 sensor interface handler for VIDIOC_G_CTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @vc: standard V4L2 VIDIOC_G_CTRL ioctl structure
 *
 * If the requested control is supported, returns the control's current
 * value from the video_control[] array.  Otherwise, returns -EINVAL
 * if the control is not supported.
 */
static int ioctl_g_ctrl(struct v4l2_int_device *s, struct v4l2_control *vc)
{
	int ret = 0;
	int sat = 0;

	dev_dbg(&max9526_data.sen.i2c_client->dev, "In max9526:ioctl_g_ctrl\n");

	switch (vc->id) {
	case V4L2_CID_BRIGHTNESS:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_BRIGHTNESS\n");
		max9526_data.sen.brightness = max9526_read(REG_BRIGHTNESS);
		vc->value = max9526_data.sen.brightness;
		break;
	case V4L2_CID_CONTRAST:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_CONTRAST\n");
		vc->value = max9526_data.sen.contrast;
		break;
	case V4L2_CID_SATURATION:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_SATURATION\n");
		sat = max9526_read(REG_SATURATION);
		max9526_data.sen.saturation = sat;
		vc->value = max9526_data.sen.saturation;
		break;
	case V4L2_CID_HUE:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_HUE\n");
		vc->value = max9526_data.sen.hue;
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_AUTO_WHITE_BALANCE\n");
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_DO_WHITE_BALANCE\n");
		break;
	case V4L2_CID_RED_BALANCE:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_RED_BALANCE\n");
		vc->value = max9526_data.sen.red;
		break;
	case V4L2_CID_BLUE_BALANCE:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_BLUE_BALANCE\n");
		vc->value = max9526_data.sen.blue;
		break;
	case V4L2_CID_GAMMA:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_GAMMA\n");
		break;
	case V4L2_CID_EXPOSURE:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_EXPOSURE\n");
		vc->value = max9526_data.sen.ae_mode;
		break;
	case V4L2_CID_AUTOGAIN:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_AUTOGAIN\n");
		break;
	case V4L2_CID_GAIN:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_GAIN\n");
		break;
	case V4L2_CID_HFLIP:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_HFLIP\n");
		break;
	case V4L2_CID_VFLIP:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_VFLIP\n");
		break;
	default:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   Default case\n");
		vc->value = 0;
		ret = -EPERM;
		break;
	}

	return ret;
}

/*!
 * ioctl_s_ctrl - V4L2 sensor interface handler for VIDIOC_S_CTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @vc: standard V4L2 VIDIOC_S_CTRL ioctl structure
 *
 * If the requested control is supported, sets the control's current
 * value in HW (and updates the video_control[] array).  Otherwise,
 * returns -EINVAL if the control is not supported.
 */
static int ioctl_s_ctrl(struct v4l2_int_device *s, struct v4l2_control *vc)
{
	int retval = 0;
	u8 tmp;

	dev_dbg(&max9526_data.sen.i2c_client->dev, "In max9526:ioctl_s_ctrl\n");

	switch (vc->id) {
	case V4L2_CID_BRIGHTNESS:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_BRIGHTNESS\n");
		tmp = vc->value;
		max9526_write_reg(REG_BRIGHTNESS, tmp);
		max9526_data.sen.brightness = vc->value;
		break;
	case V4L2_CID_CONTRAST:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_CONTRAST\n");
		break;
	case V4L2_CID_SATURATION:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_SATURATION\n");
		tmp = vc->value;
		max9526_write_reg(REG_SATURATION, tmp);
		max9526_data.sen.saturation = vc->value;
		break;
	case V4L2_CID_HUE:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_HUE\n");
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_AUTO_WHITE_BALANCE\n");
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_DO_WHITE_BALANCE\n");
		break;
	case V4L2_CID_RED_BALANCE:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_RED_BALANCE\n");
		break;
	case V4L2_CID_BLUE_BALANCE:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_BLUE_BALANCE\n");
		break;
	case V4L2_CID_GAMMA:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_GAMMA\n");
		break;
	case V4L2_CID_EXPOSURE:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_EXPOSURE\n");
		break;
	case V4L2_CID_AUTOGAIN:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_AUTOGAIN\n");
		break;
	case V4L2_CID_GAIN:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_GAIN\n");
		break;
	case V4L2_CID_HFLIP:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_HFLIP\n");
		break;
	case V4L2_CID_VFLIP:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   V4L2_CID_VFLIP\n");
		break;
	default:
		dev_dbg(&max9526_data.sen.i2c_client->dev,
			"   Default case\n");
		retval = -EPERM;
		break;
	}

	return retval;
}

/*!
 * ioctl_enum_framesizes - V4L2 sensor interface handler for
 *			   VIDIOC_ENUM_FRAMESIZES ioctl
 * @s: pointer to standard V4L2 device structure
 * @fsize: standard V4L2 VIDIOC_ENUM_FRAMESIZES ioctl structure
 *
 * Return 0 if successful, otherwise -EINVAL.
 */
static int ioctl_enum_framesizes(struct v4l2_int_device *s,
				 struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index >= 1)
		return -EINVAL;

	fsize->discrete.width = video_fmts[video_idx].active_width;
	fsize->discrete.height  = video_fmts[video_idx].active_height;

	return 0;
}

/*!
 * ioctl_g_chip_ident - V4L2 sensor interface handler for
 *			VIDIOC_DBG_G_CHIP_IDENT ioctl
 * @s: pointer to standard V4L2 device structure
 * @id: pointer to int
 *
 * Return 0.
 */
static int ioctl_g_chip_ident(struct v4l2_int_device *s, int *id)
{
	((struct v4l2_dbg_chip_ident *)id)->match.type =
					V4L2_CHIP_MATCH_I2C_DRIVER;
	strcpy(((struct v4l2_dbg_chip_ident *)id)->match.name,
						"max9526_decoder");
	((struct v4l2_dbg_chip_ident *)id)->ident = V4L2_IDENT_AMBIGUOUS;

	return 0;
}

/*!
 * ioctl_init - V4L2 sensor interface handler for VIDIOC_INT_INIT
 * @s: pointer to standard V4L2 device structure
 */
static int ioctl_init(struct v4l2_int_device *s)
{
	dev_dbg(&max9526_data.sen.i2c_client->dev, "In max9526:ioctl_init\n");
	return 0;
}

/*!
 * ioctl_dev_init - V4L2 sensor interface handler for vidioc_int_dev_init_num
 * @s: pointer to standard V4L2 device structure
 *
 * Initialise the device when slave attaches to the master.
 */
static int ioctl_dev_init(struct v4l2_int_device *s)
{
	dev_dbg(&max9526_data.sen.i2c_client->dev, "max9526:ioctl_dev_init\n");
	return 0;
}

/*!
 * This structure defines all the ioctls for this module.
 */
static struct v4l2_int_ioctl_desc max9526_ioctl_desc[] = {

	{vidioc_int_dev_init_num, (v4l2_int_ioctl_func*)ioctl_dev_init},

	/*!
	 * Delinitialise the dev. at slave detach.
	 * The complement of ioctl_dev_init.
	 */
/*	{vidioc_int_dev_exit_num, (v4l2_int_ioctl_func *)ioctl_dev_exit}, */

	{vidioc_int_s_power_num, (v4l2_int_ioctl_func*)ioctl_s_power},
	{vidioc_int_g_ifparm_num, (v4l2_int_ioctl_func*)ioctl_g_ifparm},
/*	{vidioc_int_g_needs_reset_num,
				(v4l2_int_ioctl_func *)ioctl_g_needs_reset}, */
/*	{vidioc_int_reset_num, (v4l2_int_ioctl_func *)ioctl_reset}, */
	{vidioc_int_init_num, (v4l2_int_ioctl_func*)ioctl_init},

	/*!
	 * VIDIOC_ENUM_FMT ioctl for the CAPTURE buffer type.
	 */
/*	{vidioc_int_enum_fmt_cap_num,
				(v4l2_int_ioctl_func *)ioctl_enum_fmt_cap}, */

	/*!
	 * VIDIOC_TRY_FMT ioctl for the CAPTURE buffer type.
	 * This ioctl is used to negotiate the image capture size and
	 * pixel format without actually making it take effect.
	 */
/*	{vidioc_int_try_fmt_cap_num,
				(v4l2_int_ioctl_func *)ioctl_try_fmt_cap}, */

	{vidioc_int_g_fmt_cap_num, (v4l2_int_ioctl_func*)ioctl_g_fmt_cap},

	/*!
	 * If the requested format is supported, configures the HW to use that
	 * format, returns error code if format not supported or HW can't be
	 * correctly configured.
	 */
/*	{vidioc_int_s_fmt_cap_num, (v4l2_int_ioctl_func *)ioctl_s_fmt_cap}, */

	{vidioc_int_g_parm_num, (v4l2_int_ioctl_func*)ioctl_g_parm},
	{vidioc_int_s_parm_num, (v4l2_int_ioctl_func*)ioctl_s_parm},
	{vidioc_int_queryctrl_num, (v4l2_int_ioctl_func*)ioctl_queryctrl},
	{vidioc_int_g_ctrl_num, (v4l2_int_ioctl_func*)ioctl_g_ctrl},
	{vidioc_int_s_ctrl_num, (v4l2_int_ioctl_func*)ioctl_s_ctrl},
	{vidioc_int_enum_framesizes_num,
				(v4l2_int_ioctl_func *) ioctl_enum_framesizes},
	{vidioc_int_g_chip_ident_num,
				(v4l2_int_ioctl_func *)ioctl_g_chip_ident},
};

static struct v4l2_int_slave max9526_slave = {
	.ioctls = max9526_ioctl_desc,
	.num_ioctls = ARRAY_SIZE(max9526_ioctl_desc),
};

static struct v4l2_int_device max9526_int_device = {
	.module = THIS_MODULE,
	.name = "max9526",
	.type = v4l2_int_type_slave,
	.u = {
		.slave = &max9526_slave,
	},
};


/***********************************************************************
 * I2C client and driver.
 ***********************************************************************/

/*! MAX9526 Reset function.
 *
 *  @return		None.
 */
static void max9526_hard_reset(void)
{
	dev_dbg(&max9526_data.sen.i2c_client->dev,
		"In max9526:max9526_hard_reset\n");

	/* System Reset */
	max9526_write_reg(REG_STANDARD_SELECT_SHUTDOWN_CONTROL, 0x04);
	msleep(10);

	/* Delta to Datasheet poweron state */
	max9526_write_reg(REG_STANDARD_SELECT_SHUTDOWN_CONTROL, 0x18); /* shutdown */
	max9526_write_reg(REG_VIDEO_INPUT_SELECT_AND_CLAMP, 0x82); /* autom. input sel. */
	max9526_write_reg(REG_OUTPUT_TEST_SIGNAL, 0x03); /* select 100% color bars */
	max9526_write_reg(REG_CLOCK_AND_OUTPUT, 0x04); /* Output HSync/Vsync */
	max9526_write_reg(REG_MISCELLANEOUS, 0x14); /* Output HSync/Vsync */
}

/*! MAX9526 I2C attach function.
 *
 *  @param *adapter	struct i2c_adapter *.
 *
 *  @return		Error code indicating success or failure.
 */

/*!
 * MAX9526 I2C probe function.
 * Function set in i2c_driver struct.
 * Called by insmod.
 *
 *  @param *adapter	I2C adapter descriptor.
 *
 *  @return		Error code indicating success or failure.
 */
static int max9526_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int ret = 0;
	struct pinctrl *pinctrl;
	struct device *dev = &client->dev;

	dev_dbg(dev, "%s sensor data is at %p\n", __func__, &max9526_data);

	/* Set initial values for the sensor struct. */
	memset(&max9526_data, 0, sizeof(max9526_data));
	max9526_data.sen.i2c_client = client;
	max9526_data.sen.streamcap.timeperframe.denominator = 30;
	max9526_data.sen.streamcap.timeperframe.numerator = 1;
	max9526_data.std_id = V4L2_STD_ALL;
	video_idx = MAX9526_NOT_LOCKED;
	max9526_data.sen.pix.width = video_fmts[video_idx].raw_width;
	max9526_data.sen.pix.height = video_fmts[video_idx].raw_height;
	max9526_data.sen.pix.pixelformat = V4L2_PIX_FMT_UYVY;  /* YUV422 */
	max9526_data.sen.pix.priv = 1;  /* 1 is used to indicate TV in */
	max9526_data.sen.on = false;

	max9526_data.sen.sensor_clk = devm_clk_get(dev, "csi_mclk");
	if (IS_ERR(max9526_data.sen.sensor_clk)) {
		dev_err(dev, "get mclk failed\n");
		return PTR_ERR(max9526_data.sen.sensor_clk);
	}

	ret = of_property_read_u32(dev->of_node, "mclk",
					&max9526_data.sen.mclk);
	if (ret) {
		dev_err(dev, "mclk frequency is invalid\n");
		return ret;
	}

	ret = of_property_read_u32(
		dev->of_node, "mclk_source",
		(u32 *) &(max9526_data.sen.mclk_source));
	if (ret) {
		dev_err(dev, "mclk_source invalid\n");
		return ret;
	}

	ret = of_property_read_u32(dev->of_node, "csi_id",
					&(max9526_data.sen.csi));
	if (ret) {
		dev_err(dev, "csi_id invalid\n");
		return ret;
	}

	/* MAX9526 is always parallel IF */
	max9526_data.sen.mipi_camera = 0;

	clk_prepare_enable(max9526_data.sen.sensor_clk);

	dev_dbg(&max9526_data.sen.i2c_client->dev,
		"%s:max9526 probe i2c address is 0x%02X\n",
		__func__, max9526_data.sen.i2c_client->addr);

	/*! Read a register to test I2C device address */
	ret = max9526_read(REG_MISCELLANEOUS) & 0xd0;
	if(ret != 0x10) {
		/* if the first read fails, that might go undetected */
		ret = max9526_read(REG_MISCELLANEOUS) & 0xd0;
		if(ret != 0x10) {
			dev_err(dev, "Device seems not to be a MAX9526\n");
			return -ENODEV;
		}
	}

	/* MAX9526 pinctrl */
	pinctrl = devm_pinctrl_get_select_default(dev);
	if (IS_ERR(pinctrl)) {
		dev_err(dev, "setup pinctrl failed\n");
		return PTR_ERR(pinctrl);
	}

	max9526_regulator_enable(dev);

	max9526_power_down(0);

	msleep(1);

	/*! MAX9526 initialization. */
	max9526_hard_reset();

	pr_debug("   type is %d (expect %d)\n",
		 max9526_int_device.type, v4l2_int_type_slave);
	pr_debug("   num ioctls is %d\n",
		 max9526_int_device.u.slave->num_ioctls);

	/* This function attaches this structure to the /dev/video0 device.
	 * The pointer in priv points to the max9526_data structure here.*/
	max9526_int_device.priv = &max9526_data;
	ret = v4l2_int_device_register(&max9526_int_device);

	clk_disable_unprepare(max9526_data.sen.sensor_clk);

	return ret;
}

/*!
 * MAX9526 I2C detach function.
 * Called on rmmod.
 *
 *  @param *client	struct i2c_client*.
 *
 *  @return		Error code indicating success or failure.
 */
static int max9526_detach(struct i2c_client *client)
{
	dev_dbg(&max9526_data.sen.i2c_client->dev,
		"%s:Removing %s video decoder @ 0x%02X from adapter %s\n",
		__func__, IF_NAME, client->addr << 1, client->adapter->name);

	/* Power down via i2c */
	max9526_write_reg(REG_STANDARD_SELECT_SHUTDOWN_CONTROL, 0x18);

	if (dvddio_regulator)
		regulator_disable(dvddio_regulator);

	if (dvdd_regulator)
		regulator_disable(dvdd_regulator);

	if (avdd_regulator)
		regulator_disable(avdd_regulator);

	v4l2_int_device_unregister(&max9526_int_device);

	return 0;
}

/*!
 * MAX9526 init function.
 * Called on insmod.
 *
 * @return    Error code indicating success or failure.
 */
static __init int max9526_init(void)
{
	u8 err = 0;

	pr_debug("In max9526_init\n");

	/* Tells the i2c driver what functions to call for this driver. */
	err = i2c_add_driver(&max9526_i2c_driver);
	if (err != 0)
		pr_err("%s:driver registration failed, error=%d\n",
			__func__, err);

	return err;
}

/*!
 * MAX9526 cleanup function.
 * Called on rmmod.
 *
 * @return   Error code indicating success or failure.
 */
static void __exit max9526_clean(void)
{
	dev_dbg(&max9526_data.sen.i2c_client->dev, "In max9526_clean\n");
	i2c_del_driver(&max9526_i2c_driver);
}

module_init(max9526_init);
module_exit(max9526_clean);

MODULE_AUTHOR("max.krummenacher@toradex.com");
MODULE_DESCRIPTION("Maxim Integrated MAX9526 video decoder driver for i.MX6");
MODULE_LICENSE("GPL");
