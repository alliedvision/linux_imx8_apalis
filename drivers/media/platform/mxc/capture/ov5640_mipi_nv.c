/*
 * Copyright (c) 2012-2013 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-mediabus.h>
#include <media/soc_camera.h>
#include <linux/of_gpio.h>

struct ov5640_reg {
	u16 addr;
	u16 val;
};

#define MIPI_CSI2_SENS_VC0_PAD_SOURCE	0
#define MIPI_CSI2_SENS_VC1_PAD_SOURCE	1
#define MIPI_CSI2_SENS_VC2_PAD_SOURCE	2
#define MIPI_CSI2_SENS_VC3_PAD_SOURCE	3
#define MIPI_CSI2_SENS_VCX_PADS_NUM	4

#define OV5640_TABLE_WAIT_MS 0
#define OV5640_TABLE_END 1

/* -> mode_2592x1944 mode_1920x1080 mode_1280x960 mode_640x480 */

/* System and IO Pad Control [0x3000 ~ 0x3052] */
#define OV5640_SYSTEM_RESET_00    0x3000  /* default 0x30 -> all 0x00 */
#define OV5640_SYSTEM_RESET_01    0x3001  /* default 0x08 */
#define OV5640_SYSTEM_RESET_02    0x3002  /* default 0x1c */
#define OV5640_SYSTEM_RESET_03    0x3003  /* default 0x00 */
#define OV5640_SYSTEM_CLK_EN_00   0x3004  /* default 0xcf -> all 0xff */
#define OV5640_SYSTEM_CLK_EN_01   0x3005  /* default 0xf7 */
#define OV5640_SYSTEM_CLK_EN_02   0x3006  /* default 0xe3 -> all 0xc3 */
#define OV5640_SYSTEM_CLK_EN_03   0x3007  /* default 0xff */
#define OV5640_SYSTEM_CTRL        0x3008  /* default 0x02 all -> (0x82, 0x42, 0x02) */
#define OV5640_CHIP_ID_H          0x300a  /* default 0x56 */
#define OV5640_CHIP_ID_L          0x300b  /* default 0x40 */
#define OV5640_SYSTEM_MIPI_CTRL   0x300e  /* default 0x58 -> all 0x45 */
#define OV5640_IO_CTRL_01         0x3017  /* default 0x00 */
#define OV5640_IO_CTRL_02         0x3018  /* default 0x00 */
#define OV5640_SYSTEM_CTRL_2D     0x302d  /* default ---- -> all 0x60 !! */
#define OV5640_SYSTEM_CTRL_2E     0x302e  /* default ---- -> all 0x08 !! */
#define OV5640_SC_PLL_CTRL0       0x3034  /* default 0x1a -> all 0x18 MIPI_MODE */
#define OV5640_SC_PLL_CTRL1       0x3035  /* default 0x11 -> 0x11 0x11 0x21 0x14 */
#define OV5640_SC_PLL_CTRL2       0x3036  /* default 0x69 -> 0x7d 0x54 0x70 0x70 */
#define OV5640_SC_PLL_CTRL3       0x3037  /* default 0x03 -> all 0x13 */
#define OV5640_SC_PLL_CTRL5       0x3039  /* not use */

#define SYSTEM_CTRL_SRST    0x80      /* Bit[7]: Software reset */
#define SYSTEM_CTRL_PDOWN   0x40      /* Bit[6]: Software power down */
#define MIPI_MODE_8BIT      0x08      /* Bit[3:0]: MIPI bit mode - 8-bit mode */
#define MIPI_MODE_10BIT     0x0a      /* Bit[3:0]: MIPI bit mode - 10-bit mode */

/* SCCB Control [0x3100 ~ 0x3108] */
#define OV5640_SCCB_SYSTEM_CTRL   0x3103  /* default 0x00 -> all 0x03 */
#define OV5640_SCCB_SCLK          0x3108  /* default 0x16 -> all 0x01 */

/* VCM Control [0x3600 ~ 0x3606] */
#define OV5640_VCM_DEBUG_MODE_0   0x3600  /* default ---- -> all 0x08 */
#define OV5640_VCM_DEBUG_MODE_1   0x3601  /* default ---- -> all 0x33 */

/* Timing Control [0x3800 ~ 0x3821] */
#define OV5640_TIMING_HS_H        0x3800  /* default 0x00 -> 0x00 0x01 0x00 0x00 X_ADDR_ST */
#define OV5640_TIMING_HS_L        0x3801  /* default 0x00 -> 0x00 0x50 0x00 0x00 */
#define OV5640_TIMING_VS_H        0x3802  /* default 0x00 -> 0x00 0x01 0x00 0x00 Y_ADDR_ST */
#define OV5640_TIMING_VS_L        0x3803  /* default 0x00 -> 0x00 0xaa 0x00 0x04 */
#define OV5640_TIMING_HW_H        0x3804  /* default 0x0a -> 0x0a 0x08 0x0a 0x0a X_ADDR_END */
#define OV5640_TIMING_HW_L        0x3805  /* default 0x3f -> 0x3f 0xef 0x3f 0x3f 2623 */
#define OV5640_TIMING_VH_H        0x3806  /* default 0x07 -> 0x07 0x05 0x07 0x07 Y_ADDR_END */
#define OV5640_TIMING_VH_L        0x3807  /* default 0x9f -> 0x9f 0xf9 0x9f 0x9b */
#define OV5640_TIMING_DVPHO_H     0x3808  /* default 0x0a -> 0x0a 0x07 0x05 0x02  width[11:8]  */
#define OV5640_TIMING_DVPHO_L     0x3809  /* default 0x20 -> 0x30 0x90 0x18 0x80  width[7:0]   */
#define OV5640_TIMING_DVPVO_H     0x380a  /* default 0x07 -> 0x07 0x04 0x03 0x01  height[11:8] */
#define OV5640_TIMING_DVPVO_L     0x380b  /* default 0x98 -> 0x9c 0x48 0xcc 0xe0  height[7:0]  */
#define OV5640_TIMING_HTS_H       0x380c  /* default 0x0b -> 0x0b 0x09 0x07 0x07 */
#define OV5640_TIMING_HTS_L       0x380d  /* default 0x1c -> 0x1c 0xc4 0x5e 0x68 */
#define OV5640_TIMING_VTS_H       0x380e  /* default 0x07 -> 0x07 0x04 0x03 0x03 */
#define OV5640_TIMING_VTS_L       0x380f  /* default 0xb0 -> 0xb0 0x60 0xde 0xd8 */
#define OV5640_TIMING_HOFFSET_H   0x3810  /* default 0x00 */
#define OV5640_TIMING_HOFFSET_L   0x3811  /* default 0x10 -> 0x08 0x08 0x06 0x10 */
#define OV5640_TIMING_VOFFSET_H   0x3812  /* default 0x00 */
#define OV5640_TIMING_VOFFSET_L   0x3813  /* default 0x04 -> 0x02 0x04 0x02 0x06 */
#define OV5640_TIMING_X_INC       0x3814  /* default 0x11 -> 0x11 0x11 0x31 0x31 */
#define OV5640_TIMING_Y_INC       0x3815  /* default 0x11 -> 0x11 0x11 0x31 0x31 */
#define OV5640_TIMING_REG20       0x3820  /* default 0x40 -> 0x40 0x40 0x41 0x41  sensor vflip */
#define OV5640_TIMING_REG21       0x3821  /* default 0x00 -> 0x06 0x06 0x07 0x07 */
#define REG20_SENSOR_VFLIP     0x02
#define REG20_SENSOR_ISP_FLIP  0x04
#define REG21_BINNING_EN       0x01
#define REG21_SENSOR_MIRROR    0x02
#define REG21_ISP_MIRROR       0x04

#define OV5640_ADDR_H(x)       ((x >> 8 ) & 0x0f)
#define OV5640_ADDR_L(x)       (x & 0xff)
#define OV5640_WIDTH_H(x)      ((x >> 8 ) & 0x0f)
#define OV5640_WIDTH_L(x)      (x & 0xff)
#define OV5640_HEIGHT_H(x)     ((x >> 8 ) & 0x0f)
#define OV5640_HEIGHT_L(x)     (x & 0xff)
#define OV5640_HTS_H(x)        ((x >> 8 ) & 0x1f)
#define OV5640_HTS_L(x)        (x & 0xff)
#define OV5640_VTS_H(x)        ((x >> 8 ) & 0xff)
#define OV5640_VTS_L(x)        (x & 0xff)
#define OV5640_H_OFFSET_H(x)   ((x >> 8 ) & 0x0F)
#define OV5640_H_OFFSET_L(x)   (x & 0xff)
#define OV5640_V_OFFSET_H(x)   ((x >> 8 ) & 0x03)
#define OV5640_V_OFFSET_L(x)   (x & 0xff)

/* AEC/AGC power down domain control [0x3A00 ~ 0x3A25] */
#define OV5640_AEC_CTRL_0F        0x3a0f  /* default 0x78 -> all 0x30 */
#define OV5640_AEC_CTRL_10        0x3a10  /* default 0x68 -> all 0x28 */
#define OV5640_AEC_CTRL_11        0x3a11  /* default 0xd0 -> all 0x60 */
#define OV5640_AEC_CTRL_1B        0x3a1b  /* default 0x78 -> all 0x30 */
#define OV5640_AEC_CTRL_1E        0x3a1e  /* default 0x68 -> all 0x26 */
#define OV5640_AEC_CTRL_1F        0x3a1f  /* default 0x40 -> all 0x14 */
#define OV5640_AEC_MAX_EXPO60_H   0x3a02  /* default 0x3d -> 0x07 0x04 0x03 0x03 */
#define OV5640_AEC_MAX_EXPO60_L   0x3a03  /* default 0x80 -> 0xb6 0x60 0xd8 0xd8 */
#define OV5640_AEC_B50_STEP_H     0x3a08  /* default 0x01 */
#define OV5640_AEC_B50_STEP_L     0x3a09  /* default 0x27 -> 0x27 0x50 0x27 0x27 */
#define OV5640_AEC_B60_STEP_H     0x3a0a  /* default 0x00 -> 0x00 0x01 0x00 0x00 */
#define OV5640_AEC_B60_STEP_L     0x3a0b  /* default 0xf6 -> 0xf6 0x18 0xf6 0xf6 */
#define OV5640_AEC_CTRL_0D        0x3a0d  /* default 0x08 -> 0x06 0x03 0x03 0x03 */
#define OV5640_AEC_CTRL_0E        0x3a0e  /* default 0x06 -> 0x06 0x04 0x04 0x04 */
#define OV5640_AEC_CTRL_13        0x3a13  /* default 0x40 -> all 0x43 */
#define OV5640_AEC_MAX_EXPO_H     0x3a14  /* default 0x0e -> 0x07 0x04 0x03 0x03 */
#define OV5640_AEC_MAX_EXPO_L     0x3a15  /* default 0x40 -> 0xb0 0xd0 0xd8 0xd8 */
#define OV5640_AEC_GAIN_CEILING_0 0x3a18  /* default 0x03 -> all 0x00 */
#define OV5640_AEC_GAIN_CEILING_1 0x3a19  /* default 0xE0 -> all 0xf8 */

/* 50/60Hz detector control [0x3C00 ~ 0x3C1E] */
#define OV5640_5060_CTRL_01       0x3c01  /* default 0x00 -> all 0x34 */
#define OV5640_5060_CTRL_04       0x3c04  /* default 0x20 -> all 0x28 */
#define OV5640_5060_CTRL_05       0x3c05  /* default 0x70 -> all 0x98 */

#define OV5640_5060_THRESHOLD_1_H 0x3c06  /* default 0x00 -> all 0x00 */
#define OV5640_5060_THRESHOLD_1_L 0x3c07  /* default 0x00 -> all 0x07 0x07 0x07 0x07 0x08 */
#define OV5640_5060_THRESHOLD_2_H 0x3c08  /* default 0x01 -> all 0x00 */
#define OV5640_5060_THRESHOLD_2_L 0x3c09  /* default 0x2c -> all 0x1c */
#define OV5640_5060_SAMPLE_NUM_H  0x3c0a  /* default 0x4e -> all 0x9c */
#define OV5640_5060_SAMPLE_NUM_L  0x3c0b  /* default 0x1f -> all 0x40 */

/* BLC Control [0x4000 ~ 0x4033] */
#define OV5640_BLC_CTRL_01        0x4001  /* default 0x00 -> 0x02 */
#define OV5640_BLC_CTRL_04        0x4004  /* default 0x08 -> 0x06 0x06 0x02 0x02 */

/* MIPI control [0x4800 ~ 0x4837] */
#define OV5640_MIPI_CTRL_00       0x4800  /* default 0x04 */
#define OV5640_MIPI_CTRL_01       0x4801  /* not use */
#define OV5640_MIPI_CTRL_05       0x4805  /* not use */
#define OV5640_MIPI_PCLK_PERIOD   0x4837  /* default 0x10 -> 0x0a 0x0a 0x10 0x44 */

/* Format Control [0x4300 ~ 0x430D] */
#define FMT_CTRL_00               0x4300  /* default 0xF8 -> all 0x32 */

/* Format Control */
#define FMT_YUV422	 0x30
/* Output sequence */
#define OFMT_YUYV        0x00
#define OFMT_YVYU        0x01
#define OFMT_UYVY        0x02
#define OFMT_VYUY        0x03

/* ISP Control [0x5000 ~ 0x5063] */
#define OV5640_ISP_CTRL_00        0x5000  /* default 0x06 -> all 0xa7 */
#define OV5640_ISP_CTRL_01        0x5001  /* default 0x01 -> 0x83 0x83 0x83 0xa3 */
#define OV5640_ISP_MUX_CTRL       0x501F  /* default 0x00 -> all 0x00 (000: ISP YUV422) */
#define OV5640_ISP_DEBUG_25       0x5025  /* default ---- -> all 0x00 */
#define OV5640_ISP_TEST           0x503D  /* default 0x00 */
#define OV5640_ISP_DEBUG_3E       0x503E  /* default ---- */
#define OV5640_ISP_DEBUG_46       0x5046  /* default ---- */

#define ISP_SCALE_DIGITAL     0x80       /* Special digital effect */
#define ISP_SCALE_EN          0x20       /* Scale enable */
#define ISP_AUTO_BALANCE_EN   0x01       /* Auto white balance enable */
#define ISP_COLOR_MATRIX_EN   0x02       /* Color matrix enable */
#define ISP_TEST_EN           0x80
#define ISP_TEST_00           0x00       /* 00: Standard eight color bar */
#define ISP_TEST_01           0x01       /* 01: Gradual change at vertical mode 1 */
#define ISP_TEST_10           0x02       /* 10: Gradual change at horizontal */
#define ISP_TEST_11           0x03       /* 11: Gradual change at vertical mode 2 */
#define ISP_TEST_TRANSPARENT  0x20       /* Transparent */
#define ISP_TEST_ROLLING      0x40       /* Rolling */

/* AWB Control [0x5180 ~ 0x51D0] */
#define OV5640_AWB_CTRL_00        0x5180  /* default 0xff */
#define OV5640_AWB_CTRL_01        0x5181  /* default 0x58 -> all 0xf2 */
#define OV5640_AWB_CTRL_02        0x5182  /* default 0x11 -> all 0x00 */
#define OV5640_AWB_CTRL_03        0x5183  /* default 0x90 -> all 0x14 */
#define OV5640_AWB_CTRL_04        0x5184  /* default 0x25 */
#define OV5640_AWB_CTRL_05        0x5185  /* default 0x24 */
#define OV5640_AWB_CTRL_06        0x5186  /* default ---- -> all 0x09 */
#define OV5640_AWB_CTRL_07        0x5187  /* default ---- -> all 0x09 */
#define OV5640_AWB_CTRL_08        0x5188  /* default ---- -> all 0x09 */
#define OV5640_AWB_CTRL_09        0x5189  /* default ---- -> all 0x75 */
#define OV5640_AWB_CTRL_10        0x518a  /* default ---- -> all 0x54 */
#define OV5640_AWB_CTRL_11        0x518b  /* default ---- -> all 0xe0 */
#define OV5640_AWB_CTRL_12        0x518c  /* default ---- -> all 0xb2 */
#define OV5640_AWB_CTRL_13        0x518d  /* default ---- -> all 0x42 */
#define OV5640_AWB_CTRL_14        0x518e  /* default ---- -> all 0x3d */
#define OV5640_AWB_CTRL_15        0x518f  /* default ---- -> all 0x56 */
#define OV5640_AWB_CTRL_16        0x5190  /* default ---- -> all 0x46 */
#define OV5640_AWB_CTRL_17        0x5191  /* default 0xff -> all 0xf8 */
#define OV5640_AWB_CTRL_18        0x5192  /* default 0x00 -> all 0x04 */
#define OV5640_AWB_CTRL_19        0x5193  /* default 0xf0 -> all 0x70 */
#define OV5640_AWB_CTRL_20        0x5194  /* default 0xf0 */
#define OV5640_AWB_CTRL_21        0x5195  /* default 0xf0 */
#define OV5640_AWB_CTRL_22        0x5196  /* default 0x03 */
#define OV5640_AWB_CTRL_23        0x5197  /* default 0x02 -> all 0x01 */
#define OV5640_AWB_CTRL_24        0x5198  /* default ---- -> all 0x04 */
#define OV5640_AWB_CTRL_25        0x5199  /* default ---- -> all 0x12 */
#define OV5640_AWB_CTRL_26        0x519a  /* default ---- -> all 0x04 */
#define OV5640_AWB_CTRL_27        0x519b  /* default ---- -> all 0x00 */
#define OV5640_AWB_CTRL_28        0x519c  /* default ---- -> all 0x06 */
#define OV5640_AWB_CTRL_29        0x519d  /* default ---- -> all 0x82 */
#define OV5640_AWB_CTRL_30        0x519e  /* default 0x02 -> all 0x38 */

/* CIP Control [0x5300 ~ 0x530F] */
#define OV5640_CIP_SH_MT_THRES_1  0x5300  /* default 0x08 */
#define OV5640_CIP_SH_MT_THRES_2  0x5301  /* default 0x48 -> all 0x30 */
#define OV5640_CIP_SH_MT_OFFSET_1 0x5302  /* default 0x18 -> all 0x10 */
#define OV5640_CIP_SH_MT_OFFSET_2 0x5303  /* default 0x0e -> all 0x00 */
#define OV5640_CIP_DNS_THRES_1    0x5304  /* default 0x08 */
#define OV5640_CIP_DNS_THRES_2    0x5305  /* default 0x48 -> all 0x30 */
#define OV5640_CIP_DNS_OFFSET_1   0x5306  /* default 0x09 -> all 0x08 */
#define OV5640_CIP_DNS_OFFSET_2   0x5307  /* default 0x16 */
#define OV5640_CIP_SH_TH_THRES_1  0x5309  /* default 0x08 */
#define OV5640_CIP_SH_TH_THRES_2  0x530a  /* default 0x48 -> all 0x30 */
#define OV5640_CIP_SH_TH_OFFSET_1 0x530b  /* default 0x04 */
#define OV5640_CIP_SH_TH_OFFSET_2 0x530c  /* default 0x06 */

/* CMX Control [0x5380 ~ 0x538B] */
#define OV5640_CMX1               0x5381  /* default 0x20 -> all 0x1e */
#define OV5640_CMX2               0x5382  /* default 0x64 -> all 0x5b */
#define OV5640_CMX3               0x5383  /* default 0x08 */
#define OV5640_CMX4               0x5384  /* default 0x30 -> all 0x0a */
#define OV5640_CMX5               0x5385  /* default 0x80 -> all 0x7e */
#define OV5640_CMX6               0x5386  /* default 0xc0 -> all 0x88 */
#define OV5640_CMX7               0x5387  /* default 0xa0 -> all 0x7c */
#define OV5640_CMX8               0x5388  /* default 0x98 -> all 0x6c */
#define OV5640_CMX9               0x5389  /* default 0x08 -> all 0x10 */
#define OV5640_CMX_SIGN_0         0x538A  /* default 0x01 */
#define OV5640_CMX_SIGN_1         0x538B  /* default 0x98 */

/* Gamma Control [0x5480 ~ 0x5490] */
#define OV5640_GAMMA_CTRL_00      0x5480  /* default 0x00 -> all 0x01 */
#define OV5640_GAMMA_YST_00       0x5481  /* default 0x26 -> all 0x08 */
#define OV5640_GAMMA_YST_01       0x5482  /* default 0x35 -> all 0x14 */

#define OV5640_GAMMA_YST_02       0x5483  /* default 0x48 -> all 0x28 */
#define OV5640_GAMMA_YST_03       0x5484  /* default 0x57 -> all 0x51 */
#define OV5640_GAMMA_YST_04       0x5485  /* default 0x63 -> all 0x65 */
#define OV5640_GAMMA_YST_05       0x5486  /* default 0x6e -> all 0x71 */

#define OV5640_GAMMA_YST_06       0x5487  /* default 0x77 -> all 0x7d */
#define OV5640_GAMMA_YST_07       0x5488  /* default 0x80 -> all 0x87 */
#define OV5640_GAMMA_YST_08       0x5489  /* default 0x88 -> all 0x91 */
#define OV5640_GAMMA_YST_09       0x548a  /* default 0x96 -> all 0x9a */
#define OV5640_GAMMA_YST_0A       0x548b  /* default 0xa3 -> all 0xaa */
#define OV5640_GAMMA_YST_0B       0x548c  /* default 0xaf -> all 0xb8 */
#define OV5640_GAMMA_YST_0C       0x548d  /* default 0xc5 -> all 0xcd */
#define OV5640_GAMMA_YST_0D       0x548e  /* default 0xd7 -> all 0xdd */
#define OV5640_GAMMA_YST_0E       0x548f  /* default 0xe8 -> all 0xea */
#define OV5640_GAMMA_YST_0F       0x5490  /* default 0x0f -> all 0x1d */

/* SDE Control [0x5580 ~ 0x558C] */
#define OV5640_SDE_CTRL_0         0x5580  /* default 0x00 -> all 0x02 */
#define OV5640_SDE_CTRL_3         0x5583  /* default 0x40 */
#define OV5640_SDE_CTRL_4         0x5584  /* default 0x40 -> all 0x10 */
#define OV5640_SDE_CTRL_9         0x5589  /* default 0x01 -> all 0x10 */
#define OV5640_SDE_CTRL_10        0x558a  /* default 0x01 -> all 0x00 */
#define OV5640_SDE_CTRL_11        0x558b  /* default 0xff -> all 0xf8 */

/* LENC Control [0x5800 ~ 0x5849] */
#define OV5640_LENC_GMTRX_00      0x5800  /* default 0x10 -> all 0x23 */
#define OV5640_LENC_GMTRX_01      0x5801  /* default 0x10 -> all 0x14 */
#define OV5640_LENC_GMTRX_02      0x5802  /* default 0x10 -> all 0x0f */
#define OV5640_LENC_GMTRX_03      0x5803  /* default 0x10 -> all 0x0f */
#define OV5640_LENC_GMTRX_04      0x5804  /* default 0x10 -> all 0x12 */
#define OV5640_LENC_GMTRX_05      0x5805  /* default 0x10 -> all 0x26 */
#define OV5640_LENC_GMTRX_10      0x5806  /* default 0x10 -> all 0x0c */
#define OV5640_LENC_GMTRX_11      0x5807  /* default 0x08 */
#define OV5640_LENC_GMTRX_12      0x5808  /* default 0x08 -> all 0x05 */
#define OV5640_LENC_GMTRX_13      0x5809  /* default 0x08 -> all 0x05 */
#define OV5640_LENC_GMTRX_14      0x580a  /* default 0x08 -> all */
#define OV5640_LENC_GMTRX_15      0x580b  /* default 0x10 -> all 0x0d */
#define OV5640_LENC_GMTRX_20      0x580c  /* default 0x10 -> all 0x08 */
#define OV5640_LENC_GMTRX_21      0x580d  /* default 0x08 -> all 0x03 */
#define OV5640_LENC_GMTRX_22      0x580e  /* default 0x00 */
#define OV5640_LENC_GMTRX_23      0x580f  /* default 0x00 */
#define OV5640_LENC_GMTRX_24      0x5810  /* default 0x08 -> all 0x03 */
#define OV5640_LENC_GMTRX_25      0x5811  /* default 0x10 -> all 0x09 */
#define OV5640_LENC_GMTRX_30      0x5812  /* default 0x10 -> all 0x07 */
#define OV5640_LENC_GMTRX_31      0x5813  /* default 0x08 -> all 0x03 */
#define OV5640_LENC_GMTRX_32      0x5814  /* default 0x00 */
#define OV5640_LENC_GMTRX_33      0x5815  /* default 0x00 -> all 0x01 */
#define OV5640_LENC_GMTRX_34      0x5816  /* default 0x08 -> all 0x03 */
#define OV5640_LENC_GMTRX_35      0x5817  /* default 0x10 -> all 0x08 */
#define OV5640_LENC_GMTRX_40      0x5818  /* default 0x10 -> all 0x0d */
#define OV5640_LENC_GMTRX_41      0x5819  /* default 0x08 */
#define OV5640_LENC_GMTRX_42      0x581a  /* default 0x08 -> all 0x05 */
#define OV5640_LENC_GMTRX_43      0x581b  /* default 0x08 -> all 0x06 */
#define OV5640_LENC_GMTRX_44      0x581c  /* default 0x08 */
#define OV5640_LENC_GMTRX_45      0x581d  /* default 0x10 -> all 0x0e */
#define OV5640_LENC_GMTRX_50      0x581e  /* default 0x10 -> all 0x29 */
#define OV5640_LENC_GMTRX_51      0x581f  /* default 0x10 -> all 0x17 */
#define OV5640_LENC_GMTRX_52      0x5820  /* default 0x10 -> all 0x11 */
#define OV5640_LENC_GMTRX_53      0x5821  /* default 0x10 -> all 0x11 */
#define OV5640_LENC_GMTRX_54      0x5822  /* default 0x10 -> all 0x15 */
#define OV5640_LENC_GMTRX_55      0x5823  /* default 0x10 -> all 0x28 */
#define OV5640_LENC_BRMATRX_00    0x5824  /* default 0xaa -> all 0x46 */
#define OV5640_LENC_BRMATRX_01    0x5825  /* default 0xaa -> all 0x26 */
#define OV5640_LENC_BRMATRX_02    0x5826  /* default 0xaa -> all 0x08 */
#define OV5640_LENC_BRMATRX_03    0x5827  /* default 0xaa -> all 0x26 */
#define OV5640_LENC_BRMATRX_04    0x5828  /* default 0xaa -> all 0x64 */
#define OV5640_LENC_BRMATRX_05    0x5829  /* default 0xaa -> all 0x26 */
#define OV5640_LENC_BRMATRX_06    0x582a  /* default 0x99 -> all 0x24 */
#define OV5640_LENC_BRMATRX_07    0x582b  /* default 0x99 -> all 0x22 */
#define OV5640_LENC_BRMATRX_08    0x582c  /* default 0x99 -> all 0x24 */
#define OV5640_LENC_BRMATRX_09    0x582d  /* default 0xaa -> all 0x24 */
#define OV5640_LENC_BRMATRX_20    0x582e  /* default 0xaa -> all 0x06 */
#define OV5640_LENC_BRMATRX_21    0x582f  /* default 0x99 -> all 0x22 */
#define OV5640_LENC_BRMATRX_22    0x5830  /* default 0x88 -> all 0x40 */
#define OV5640_LENC_BRMATRX_23    0x5831  /* default 0x99 -> all 0x42 */
#define OV5640_LENC_BRMATRX_24    0x5832  /* default 0xaa -> all 0x24 */
#define OV5640_LENC_BRMATRX_30    0x5833  /* default 0xaa -> all 0x26 */
#define OV5640_LENC_BRMATRX_31    0x5834  /* default 0x99 -> all 0x24 */
#define OV5640_LENC_BRMATRX_32    0x5835  /* default 0x99 -> all 0x22 */
#define OV5640_LENC_BRMATRX_33    0x5836  /* default 0x99 -> all 0x22 */
#define OV5640_LENC_BRMATRX_34    0x5837  /* default 0xaa -> all 0x26 */
#define OV5640_LENC_BRMATRX_40    0x5838  /* default 0xaa -> all 0x44 */
#define OV5640_LENC_BRMATRX_41    0x5839  /* default 0xaa -> all 0x24 */
#define OV5640_LENC_BRMATRX_42    0x583a  /* default 0xaa -> all 0x26 */
#define OV5640_LENC_BRMATRX_43    0x583b  /* default 0xaa -> all 0x28 */
#define OV5640_LENC_BRMATRX_44    0x583c  /* default 0xaa -> all 0x42 */
#define OV5640_LENC_BR_OFFSET     0x583d  /* default 0x88 -> all 0xce */


static struct ov5640_reg mode_2592x1944[] = {
	/* PLL Control MIPI bit rate/lane = 672MHz, 16-bit mode.
	 * Output size: 2592x1944 (0, 0) - (2623, 1951),
	 * Line Length = 2844, Frame Length = 1968
         * Scaling method: full resolution (dummy 16 pixel
         * horizontal, 8 lines) 2608x1952 with dummy
	 */

	{OV5640_SC_PLL_CTRL0, 0x10 | MIPI_MODE_8BIT },
	{OV5640_SC_PLL_CTRL1, 0x11},
	{OV5640_SC_PLL_CTRL2, 0x7d},            /* default 0x69  PLL multipier */
	{OV5640_SC_PLL_CTRL3, 0x03 | (1<<4) },  /* default 0x03  PLL divided by 2 */

	{OV5640_MIPI_CTRL_00, 0x04},
	{OV5640_MIPI_PCLK_PERIOD, 0x0a},

	{OV5640_ISP_CTRL_00, 0xa7},
	{OV5640_ISP_CTRL_01, 0x83},
	{OV5640_ISP_MUX_CTRL, 0x00},

        /* Timing Control */
	{OV5640_TIMING_HS_H, OV5640_ADDR_H(0) },
	{OV5640_TIMING_HS_L, OV5640_ADDR_L(0) },
	{OV5640_TIMING_VS_H, OV5640_ADDR_H(0) },
	{OV5640_TIMING_VS_L, OV5640_ADDR_L(0) },
	{OV5640_TIMING_HW_H, OV5640_ADDR_H(2623) },
	{OV5640_TIMING_HW_L, OV5640_ADDR_L(2623) },
	{OV5640_TIMING_VH_H, OV5640_ADDR_H(1951) },
	{OV5640_TIMING_VH_L, OV5640_ADDR_L(1951) },
	{OV5640_TIMING_DVPHO_H, OV5640_WIDTH_H(2592) },
	{OV5640_TIMING_DVPHO_L, OV5640_WIDTH_L(2592) },
	{OV5640_TIMING_DVPVO_H, OV5640_HEIGHT_H(1944) },
	{OV5640_TIMING_DVPVO_L, OV5640_HEIGHT_L(1944) },
	{OV5640_TIMING_HTS_H, OV5640_HTS_H(2844) },
	{OV5640_TIMING_HTS_L, OV5640_HTS_L(2844) },
	{OV5640_TIMING_VTS_H, OV5640_VTS_H(1968) },
	{OV5640_TIMING_VTS_L, OV5640_VTS_L(1968) },
	{OV5640_TIMING_HOFFSET_H, OV5640_H_OFFSET_H(16) }, // (2623-2592-1)/2
	{OV5640_TIMING_HOFFSET_L, OV5640_H_OFFSET_L(16) },
	{OV5640_TIMING_VOFFSET_H, OV5640_V_OFFSET_H(4) },  // (1951-1944-1)/2
	{OV5640_TIMING_VOFFSET_L, OV5640_V_OFFSET_L(4) },
	{OV5640_TIMING_X_INC, 0x11},
	{OV5640_TIMING_Y_INC, 0x11},
	{OV5640_TIMING_REG20, 0x40},
	{OV5640_TIMING_REG21, 0},

        /* magic registers */
	{0x3612, 0x2b},
	{0x3618, 0x04},
	{0x3708, 0x64},
	{0x3709, 0x12},
	{0x370c, 0x00},

	/* AEC/AGC Power Down Domain Control */
	{OV5640_AEC_MAX_EXPO60_H, 0x07},
	{OV5640_AEC_MAX_EXPO60_L, 0xb0},
	{OV5640_AEC_B50_STEP_H, 0x01},
	{OV5640_AEC_B50_STEP_L, 0x27},
	{OV5640_AEC_B60_STEP_H, 0x00},
	{OV5640_AEC_B60_STEP_L, 0xf6},
	{OV5640_AEC_CTRL_0E, 0x06},
	{OV5640_AEC_CTRL_0D, 0x08},
	{OV5640_AEC_MAX_EXPO_H, 0x07},
	{OV5640_AEC_MAX_EXPO_L, 0xb0},
	{OV5640_AEC_CTRL_13, 0x43},
	{OV5640_AEC_GAIN_CEILING_0, 0x00},
	{OV5640_AEC_GAIN_CEILING_1, 0xf8},
	{OV5640_AEC_CTRL_0F, 0x30},
	{OV5640_AEC_CTRL_10, 0x28},
	{OV5640_AEC_CTRL_1B, 0x30},
	{OV5640_AEC_CTRL_1E, 0x26},
	{OV5640_AEC_CTRL_11, 0x60},
	{OV5640_AEC_CTRL_1F, 0x14},

	{OV5640_SYSTEM_CTRL, 0x02},
	{OV5640_TABLE_END, 0x0000}
};

static struct ov5640_reg mode_1920x1080[] = {
	/* PLL Control MIPI bit rate/lane = 672MHz, 16-bit mode.
	 * Output size: 1936x1088 (336, 426) - (2287, 1529),
	 * Line Length = 2500, Frame Length = 1120.
         * Scaling method: cropping from full resolution
         * 1936x1088 with dummy pixels
	 */

	{OV5640_SC_PLL_CTRL0, 0x10 | MIPI_MODE_8BIT },
	{OV5640_SC_PLL_CTRL1, 0x11},
	{OV5640_SC_PLL_CTRL2, 0x54},            /* default 0x69  PLL multipier */
	{OV5640_SC_PLL_CTRL3, 0x03 | (1<<4) },  /* default 0x03  PLL divided by 2 */

	{OV5640_MIPI_CTRL_00, 0x04},
	{OV5640_MIPI_PCLK_PERIOD, 0x0a},

	{OV5640_ISP_CTRL_00, 0xa7},
	{OV5640_ISP_CTRL_01, 0x83},
	{OV5640_ISP_MUX_CTRL, 0x00},

        /* Timing Control */
	{OV5640_TIMING_HS_H, OV5640_ADDR_H(336) },
	{OV5640_TIMING_HS_L, OV5640_ADDR_L(336) },
	{OV5640_TIMING_VS_H, OV5640_ADDR_H(426) },
	{OV5640_TIMING_VS_L, OV5640_ADDR_L(426) },
	{OV5640_TIMING_HW_H, OV5640_ADDR_H(2287) },
	{OV5640_TIMING_HW_L, OV5640_ADDR_L(2287) },
	{OV5640_TIMING_VH_H, OV5640_ADDR_H(1529) },
	{OV5640_TIMING_VH_L, OV5640_ADDR_L(1529) },
	{OV5640_TIMING_DVPHO_H, OV5640_WIDTH_H(1920) }, //1936
	{OV5640_TIMING_DVPHO_L, OV5640_WIDTH_L(1920) },
	{OV5640_TIMING_DVPVO_H, OV5640_HEIGHT_H(1080) }, //1088
	{OV5640_TIMING_DVPVO_L, OV5640_HEIGHT_L(1080) },
	{OV5640_TIMING_HTS_H, OV5640_HTS_H(2500) },
	{OV5640_TIMING_HTS_L, OV5640_HTS_L(2500) },
	{OV5640_TIMING_VTS_H, OV5640_VTS_H(1120) },
	{OV5640_TIMING_VTS_L, OV5640_VTS_L(1120) },
	{OV5640_TIMING_HOFFSET_H, OV5640_H_OFFSET_H(15) }, // (2287-336-1936-1)/2
	{OV5640_TIMING_HOFFSET_L, OV5640_H_OFFSET_L(15) },
	{OV5640_TIMING_VOFFSET_H, OV5640_V_OFFSET_H(11) }, // (1529-426-1088-1)/2
	{OV5640_TIMING_VOFFSET_L, OV5640_V_OFFSET_L(11) },
	{OV5640_TIMING_X_INC, 0x11},
	{OV5640_TIMING_Y_INC, 0x11},
	{OV5640_TIMING_REG20, 0x40},
	{OV5640_TIMING_REG21, 0},

        /* magic registers */
	{0x3612, 0x2b},
	{0x3618, 0x04},
	{0x3708, 0x64},
	{0x3709, 0x12},
	{0x370c, 0x00},

	/* AEC/AGC Power Down Domain Control */
	{OV5640_AEC_MAX_EXPO60_H, 0x04},
	{OV5640_AEC_MAX_EXPO60_L, 0x60},
	{OV5640_AEC_B50_STEP_H, 0x01},
	{OV5640_AEC_B50_STEP_L, 0x50},
	{OV5640_AEC_B60_STEP_H, 0x01},
	{OV5640_AEC_B60_STEP_L, 0x18},
	{OV5640_AEC_CTRL_0E, 0x03},
	{OV5640_AEC_CTRL_0D, 0x04},
	{OV5640_AEC_MAX_EXPO_H, 0x04},
	{OV5640_AEC_MAX_EXPO_L, 0x60},
	{OV5640_AEC_CTRL_13, 0x43},
	{OV5640_AEC_GAIN_CEILING_0, 0x00},
	{OV5640_AEC_GAIN_CEILING_1, 0xf8},
	{OV5640_AEC_CTRL_0F, 0x30},
	{OV5640_AEC_CTRL_10, 0x28},
	{OV5640_AEC_CTRL_1B, 0x30},
	{OV5640_AEC_CTRL_1E, 0x26},
	{OV5640_AEC_CTRL_11, 0x60},
	{OV5640_AEC_CTRL_1F, 0x14},

	{OV5640_SYSTEM_CTRL, 0x02},
	{OV5640_TABLE_END, 0x0000}
};

static struct ov5640_reg mode_1280x960[] = {
	/* PLL Control MIPI bit rate/lane = 448MHz, 16-bit mode.
	 * Output size: 1304x972 (0, 0) - (2623, 1951),
	 * Line Length = 1886, Frame Length = 990.
         * Scaling method: subsampling in vertical and horizontal
         * 1296x968 supports 2x2 binning
	 */

	{OV5640_SC_PLL_CTRL0, 0x10 | MIPI_MODE_8BIT },
	{OV5640_SC_PLL_CTRL1, 0x21},
	{OV5640_SC_PLL_CTRL2, 0x70},            /* default 0x69  PLL multipier */
	{OV5640_SC_PLL_CTRL3, 0x03 | (1<<4) },  /* default 0x03  PLL divided by 2 */

	{OV5640_MIPI_CTRL_00, 0x04},
	{OV5640_MIPI_PCLK_PERIOD, 0x10},

	{OV5640_ISP_CTRL_00, 0xa7},
	{OV5640_ISP_CTRL_01, 0x83},
	{OV5640_ISP_MUX_CTRL, 0x00},

        /* Timing Control */
	{OV5640_TIMING_HS_H, OV5640_ADDR_H(0) },
	{OV5640_TIMING_HS_L, OV5640_ADDR_L(0) },
	{OV5640_TIMING_VS_H, OV5640_ADDR_H(0) },
	{OV5640_TIMING_VS_L, OV5640_ADDR_L(0) },
	{OV5640_TIMING_HW_H, OV5640_ADDR_H(2623) },
	{OV5640_TIMING_HW_L, OV5640_ADDR_L(2623) },
	{OV5640_TIMING_VH_H, OV5640_ADDR_H(1951) },
	{OV5640_TIMING_VH_L, OV5640_ADDR_L(1951) },
	{OV5640_TIMING_DVPHO_H, OV5640_WIDTH_H(1280) },
	{OV5640_TIMING_DVPHO_L, OV5640_WIDTH_L(1280) },
	{OV5640_TIMING_DVPVO_H, OV5640_HEIGHT_H(960) },
	{OV5640_TIMING_DVPVO_L, OV5640_HEIGHT_L(960) },
	{OV5640_TIMING_HTS_H, OV5640_HTS_H(1886) },
	{OV5640_TIMING_HTS_L, OV5640_HTS_L(1886) },
	{OV5640_TIMING_VTS_H, OV5640_VTS_H(990) },
	{OV5640_TIMING_VTS_L, OV5640_VTS_L(990) },
	{OV5640_TIMING_HOFFSET_H, OV5640_H_OFFSET_H(31) }, // (2623-0-(1280*2)-1)/2
	{OV5640_TIMING_HOFFSET_L, OV5640_H_OFFSET_L(31) },
	{OV5640_TIMING_VOFFSET_H, OV5640_V_OFFSET_H(15) }, // (1951-0-(960*2)-1)/2
	{OV5640_TIMING_VOFFSET_L, OV5640_V_OFFSET_L(15) },
	{OV5640_TIMING_X_INC, 0x11 | (3<<4) },
	{OV5640_TIMING_Y_INC, 0x11 | (3<<4) },
	{OV5640_TIMING_REG20, 0x40},
	{OV5640_TIMING_REG21, REG21_BINNING_EN},

        /* magic registers */
	{0x3612, 0x29},
	{0x3618, 0x00},
	{0x3708, 0x62},
	{0x3709, 0x52},
	{0x370c, 0x03},

	/* AEC/AGC Power Down Domain Control */
	{OV5640_AEC_MAX_EXPO60_H, 0x03},
	{OV5640_AEC_MAX_EXPO60_L, 0xd8},
	{OV5640_AEC_B50_STEP_H, 0x01},
	{OV5640_AEC_B50_STEP_L, 0x27},
	{OV5640_AEC_B60_STEP_H, 0x00},
	{OV5640_AEC_B60_STEP_L, 0xf6},
	{OV5640_AEC_CTRL_0E, 0x03},
	{OV5640_AEC_CTRL_0D, 0x04},
	{OV5640_AEC_MAX_EXPO_H, 0x03},
	{OV5640_AEC_MAX_EXPO_L, 0xd8},
	{OV5640_AEC_CTRL_13, 0x43},
	{OV5640_AEC_GAIN_CEILING_0, 0x00},
	{OV5640_AEC_GAIN_CEILING_1, 0xf8},
	{OV5640_AEC_CTRL_0F, 0x30},
	{OV5640_AEC_CTRL_10, 0x28},
	{OV5640_AEC_CTRL_1B, 0x30},
	{OV5640_AEC_CTRL_1E, 0x26},
	{OV5640_AEC_CTRL_11, 0x60},
	{OV5640_AEC_CTRL_1F, 0x14},

	{OV5640_SYSTEM_CTRL, 0x02},
	{OV5640_TABLE_END, 0x0000}
};

static struct ov5640_reg mode_640x480[] = {
	/* PLL Control MIPI bit rate/lane = ?MHz, 16-bit mode.
	 * Output size: 640x480 (0, 4) - (2623, 1951),
	 * Line Length = 1896, Frame Length = 984.
         * Scaling method: subsampling from 1280x960
         * 648x484 with dummy
         * supports 2x2 binning
	 */

	{OV5640_SC_PLL_CTRL0, 0x10 | MIPI_MODE_8BIT },
	{OV5640_SC_PLL_CTRL1, 0x14},
	{OV5640_SC_PLL_CTRL2, 0x70},            /* default 0x69  PLL multipier */
	{OV5640_SC_PLL_CTRL3, 0x03 | (1<<4) },  /* default 0x03  PLL divided by 2 */

	{OV5640_MIPI_CTRL_00, 0x04},
	{OV5640_MIPI_PCLK_PERIOD, 0x44},

	{OV5640_ISP_CTRL_00, 0xa7},
	{OV5640_ISP_CTRL_01, 0x83 | ISP_SCALE_EN},
	{OV5640_ISP_MUX_CTRL, 0x00},

        /* Timing Control */
	{OV5640_TIMING_HS_H, OV5640_ADDR_H(0) },
	{OV5640_TIMING_HS_L, OV5640_ADDR_L(0) },
	{OV5640_TIMING_VS_H, OV5640_ADDR_H(0) },
	{OV5640_TIMING_VS_L, OV5640_ADDR_L(4) },
	{OV5640_TIMING_HW_H, OV5640_ADDR_H(2623) },
	{OV5640_TIMING_HW_L, OV5640_ADDR_L(2623) },
	{OV5640_TIMING_VH_H, OV5640_ADDR_H(1947) },
	{OV5640_TIMING_VH_L, OV5640_ADDR_L(1947) },
	{OV5640_TIMING_DVPHO_H, OV5640_WIDTH_H(640) },
	{OV5640_TIMING_DVPHO_L, OV5640_WIDTH_L(640) },
	{OV5640_TIMING_DVPVO_H, OV5640_HEIGHT_H(480) },
	{OV5640_TIMING_DVPVO_L, OV5640_HEIGHT_L(480) },
	{OV5640_TIMING_HTS_H, OV5640_HTS_H(1896) },
	{OV5640_TIMING_HTS_L, OV5640_HTS_L(1896) },
	{OV5640_TIMING_VTS_H, OV5640_VTS_H(984) },
	{OV5640_TIMING_VTS_L, OV5640_VTS_L(984) },
	{OV5640_TIMING_HOFFSET_H, OV5640_H_OFFSET_H(31) }, // (2623-0-(640*2*2)-1)/2
	{OV5640_TIMING_HOFFSET_L, OV5640_H_OFFSET_L(31) },
	{OV5640_TIMING_VOFFSET_H, OV5640_V_OFFSET_H(11) }, // (1947-4-(480*2*2)-1)/2
	{OV5640_TIMING_VOFFSET_L, OV5640_V_OFFSET_L(11) },
	{OV5640_TIMING_X_INC, 0x11 | (3<<4) },
	{OV5640_TIMING_Y_INC, 0x11 | (3<<4) },
	{OV5640_TIMING_REG20, 0x40},
	{OV5640_TIMING_REG21, REG21_BINNING_EN},

        /* magic registers */
	{0x3612, 0x29},
	{0x3618, 0x00},
	{0x3708, 0x64},
	{0x3709, 0x52},
	{0x370c, 0x03},

	/* AEC/AGC Power Down Domain Control */
	{OV5640_AEC_MAX_EXPO60_H, 0x03},
	{OV5640_AEC_MAX_EXPO60_L, 0xd8},
	{OV5640_AEC_B50_STEP_H, 0x01},
	{OV5640_AEC_B50_STEP_L, 0x27},
	{OV5640_AEC_B60_STEP_H, 0x00},
	{OV5640_AEC_B60_STEP_L, 0xf6},
	{OV5640_AEC_CTRL_0E, 0x03},
	{OV5640_AEC_CTRL_0D, 0x04},
	{OV5640_AEC_MAX_EXPO_H, 0x03},
	{OV5640_AEC_MAX_EXPO_L, 0xd8},
	{OV5640_AEC_CTRL_13, 0x43},
	{OV5640_AEC_GAIN_CEILING_0, 0x00},
	{OV5640_AEC_GAIN_CEILING_1, 0xf8},
	{OV5640_AEC_CTRL_0F, 0x30},
	{OV5640_AEC_CTRL_10, 0x28},
	{OV5640_AEC_CTRL_1B, 0x30},
	{OV5640_AEC_CTRL_1E, 0x26},
	{OV5640_AEC_CTRL_11, 0x60},
	{OV5640_AEC_CTRL_1F, 0x14},

	{OV5640_SYSTEM_CTRL, 0x02},
	{OV5640_TABLE_END, 0x0000},
};

static struct ov5640_reg mode_common_registers[] = {

        /* SCCB Control */
	{OV5640_SCCB_SYSTEM_CTRL, 0x03},                   /* Clock from PLL */
	{OV5640_SCCB_SCLK, 0x01},                          /* PCLK = pll_clki, SCLK = pll_clki/2 */

        /* System Control */
	{OV5640_SYSTEM_CTRL, 0x02 | SYSTEM_CTRL_SRST },    /* Software reset */
	{OV5640_TABLE_WAIT_MS, 5},
	{OV5640_SYSTEM_CTRL, 0x02 | SYSTEM_CTRL_PDOWN },   /* Software power down */
	{OV5640_SYSTEM_RESET_00, 0x00},
	{OV5640_SYSTEM_RESET_01, 0x00},
	{OV5640_SYSTEM_RESET_02, 0x1c},
	{OV5640_SYSTEM_CLK_EN_00, 0xff},
	{OV5640_SYSTEM_CLK_EN_02, 0xc3},
	{OV5640_SYSTEM_MIPI_CTRL, 0x45},

	{OV5640_SYSTEM_CTRL_2D, 0x60},  /* magic values */
	{OV5640_SYSTEM_CTRL_2E, 0x08},  /* magic values */

        /* Format Control */
	{FMT_CTRL_00, FMT_YUV422 | OFMT_UYVY },

        /* VCM Control */
/*	{OV5640_VCM_DEBUG_MODE_0, 0x08},*/
/*	{OV5640_VCM_DEBUG_MODE_1, 0x33},*/

        /* 50/60Hz detector control */
	{OV5640_5060_CTRL_01, 0x34},
	{OV5640_5060_CTRL_04, 0x28},
	{OV5640_5060_CTRL_05, 0x98},
	{OV5640_5060_THRESHOLD_1_H, 0x00},
	{OV5640_5060_THRESHOLD_1_L, 0x07},
	{OV5640_5060_THRESHOLD_2_H, 0x00},
	{OV5640_5060_THRESHOLD_2_L, 0x1c},
	{OV5640_5060_SAMPLE_NUM_H, 0x9c},
	{OV5640_5060_SAMPLE_NUM_L, 0x40},

        /* BLC Control */
	{OV5640_BLC_CTRL_01, 0x02},
	{OV5640_BLC_CTRL_04, 0x06},

	/* AWB Control */
	{OV5640_AWB_CTRL_00, 0xff},
	{OV5640_AWB_CTRL_01, 0xf2},
	{OV5640_AWB_CTRL_02, 0x00},
	{OV5640_AWB_CTRL_03, 0x14},
	{OV5640_AWB_CTRL_04, 0x25},
	{OV5640_AWB_CTRL_05, 0x24},
	{OV5640_AWB_CTRL_06, 0x09},
	{OV5640_AWB_CTRL_07, 0x09},
	{OV5640_AWB_CTRL_08, 0x09},
	{OV5640_AWB_CTRL_09, 0x75},
	{OV5640_AWB_CTRL_10, 0x54},
	{OV5640_AWB_CTRL_11, 0xe0},
	{OV5640_AWB_CTRL_12, 0xb2},
	{OV5640_AWB_CTRL_13, 0x42},
	{OV5640_AWB_CTRL_14, 0x3d},
	{OV5640_AWB_CTRL_15, 0x56},
	{OV5640_AWB_CTRL_16, 0x46},
	{OV5640_AWB_CTRL_17, 0xf8},
	{OV5640_AWB_CTRL_18, 0x04},
	{OV5640_AWB_CTRL_19, 0x70},
	{OV5640_AWB_CTRL_20, 0xf0},
	{OV5640_AWB_CTRL_21, 0xf0},
	{OV5640_AWB_CTRL_22, 0x03},
	{OV5640_AWB_CTRL_23, 0x01},
	{OV5640_AWB_CTRL_24, 0x04},
	{OV5640_AWB_CTRL_25, 0x12},
	{OV5640_AWB_CTRL_26, 0x04},
	{OV5640_AWB_CTRL_27, 0x00},
	{OV5640_AWB_CTRL_28, 0x06},
	{OV5640_AWB_CTRL_29, 0x82},
	{OV5640_AWB_CTRL_30, 0x38},

	/* CMX Control */
	{OV5640_CMX1, 0x1e},
	{OV5640_CMX2, 0x5b},
	{OV5640_CMX3, 0x08},
	{OV5640_CMX4, 0x0a},
	{OV5640_CMX5, 0x7e},
	{OV5640_CMX6, 0x88},
	{OV5640_CMX7, 0x7c},
	{OV5640_CMX8, 0x6c},
	{OV5640_CMX9, 0x10},
	{OV5640_CMX_SIGN_0, 0x01},
	{OV5640_CMX_SIGN_1, 0x98},

	/* CIP Control */
	{OV5640_CIP_SH_MT_THRES_1, 0x08},
	{OV5640_CIP_SH_MT_THRES_2, 0x30},
	{OV5640_CIP_SH_MT_OFFSET_1, 0x10},
	{OV5640_CIP_SH_MT_OFFSET_2, 0x00},
	{OV5640_CIP_DNS_THRES_1, 0x08},
	{OV5640_CIP_DNS_THRES_2, 0x30},
	{OV5640_CIP_DNS_OFFSET_1, 0x08},
	{OV5640_CIP_DNS_OFFSET_2, 0x16},
	{OV5640_CIP_SH_TH_THRES_1, 0x08},
	{OV5640_CIP_SH_TH_THRES_2, 0x30},
	{OV5640_CIP_SH_TH_OFFSET_1, 0x04},
	{OV5640_CIP_SH_TH_OFFSET_2, 0x06},

	/* Gamma Control */
	{OV5640_GAMMA_CTRL_00, 0x01},
	{OV5640_GAMMA_YST_00, 0x08},
	{OV5640_GAMMA_YST_01, 0x14},
	{OV5640_GAMMA_YST_02, 0x28},
	{OV5640_GAMMA_YST_03, 0x51},
	{OV5640_GAMMA_YST_04, 0x65},
	{OV5640_GAMMA_YST_05, 0x71},
	{OV5640_GAMMA_YST_06, 0x7d},
	{OV5640_GAMMA_YST_07, 0x87},
	{OV5640_GAMMA_YST_08, 0x91},
	{OV5640_GAMMA_YST_09, 0x9a},
	{OV5640_GAMMA_YST_0A, 0xaa},
	{OV5640_GAMMA_YST_0B, 0xb8},
	{OV5640_GAMMA_YST_0C, 0xcd},
	{OV5640_GAMMA_YST_0D, 0xdd},
	{OV5640_GAMMA_YST_0E, 0xea},
	{OV5640_GAMMA_YST_0F, 0x1d},

	/* SDE Control */
	{OV5640_SDE_CTRL_0, 0x02},
	{OV5640_SDE_CTRL_3, 0x40},
	{OV5640_SDE_CTRL_4, 0x10},
	{OV5640_SDE_CTRL_9, 0x10},
	{OV5640_SDE_CTRL_10, 0x00},
	{OV5640_SDE_CTRL_11, 0xf8},

	/* LENC Control */
	{OV5640_LENC_GMTRX_00, 0x23},
	{OV5640_LENC_GMTRX_01, 0x14},
	{OV5640_LENC_GMTRX_02, 0x0f},
	{OV5640_LENC_GMTRX_03, 0x0f},
	{OV5640_LENC_GMTRX_04, 0x12},
	{OV5640_LENC_GMTRX_05, 0x26},
	{OV5640_LENC_GMTRX_10, 0x0c},
	{OV5640_LENC_GMTRX_11, 0x08},
	{OV5640_LENC_GMTRX_12, 0x05},
	{OV5640_LENC_GMTRX_13, 0x05},
	{OV5640_LENC_GMTRX_14, 0x08},
	{OV5640_LENC_GMTRX_15, 0x0d},
	{OV5640_LENC_GMTRX_20, 0x08},
	{OV5640_LENC_GMTRX_21, 0x03},
	{OV5640_LENC_GMTRX_22, 0x00},
	{OV5640_LENC_GMTRX_23, 0x00},
	{OV5640_LENC_GMTRX_24, 0x03},
	{OV5640_LENC_GMTRX_25, 0x09},
	{OV5640_LENC_GMTRX_30, 0x07},
	{OV5640_LENC_GMTRX_31, 0x03},
	{OV5640_LENC_GMTRX_32, 0x00},
	{OV5640_LENC_GMTRX_33, 0x01},
	{OV5640_LENC_GMTRX_34, 0x03},
	{OV5640_LENC_GMTRX_35, 0x08},
	{OV5640_LENC_GMTRX_40, 0x0d},
	{OV5640_LENC_GMTRX_41, 0x08},
	{OV5640_LENC_GMTRX_42, 0x05},
	{OV5640_LENC_GMTRX_43, 0x06},
	{OV5640_LENC_GMTRX_44, 0x08},
	{OV5640_LENC_GMTRX_45, 0x0e},
	{OV5640_LENC_GMTRX_50, 0x29},
	{OV5640_LENC_GMTRX_51, 0x17},
	{OV5640_LENC_GMTRX_52, 0x11},
	{OV5640_LENC_GMTRX_53, 0x11},
	{OV5640_LENC_GMTRX_54, 0x15},
	{OV5640_LENC_GMTRX_55, 0x28},
	{OV5640_LENC_BRMATRX_00, 0x46},
	{OV5640_LENC_BRMATRX_01, 0x26},
	{OV5640_LENC_BRMATRX_02, 0x08},
	{OV5640_LENC_BRMATRX_03, 0x26},
	{OV5640_LENC_BRMATRX_04, 0x64},
	{OV5640_LENC_BRMATRX_05, 0x26},
	{OV5640_LENC_BRMATRX_06, 0x24},
	{OV5640_LENC_BRMATRX_07, 0x22},
	{OV5640_LENC_BRMATRX_08, 0x24},
	{OV5640_LENC_BRMATRX_09, 0x24},
	{OV5640_LENC_BRMATRX_20, 0x06},
	{OV5640_LENC_BRMATRX_21, 0x22},
	{OV5640_LENC_BRMATRX_22, 0x40},
	{OV5640_LENC_BRMATRX_23, 0x42},
	{OV5640_LENC_BRMATRX_24, 0x24},
	{OV5640_LENC_BRMATRX_30, 0x26},
	{OV5640_LENC_BRMATRX_31, 0x24},
	{OV5640_LENC_BRMATRX_32, 0x22},
	{OV5640_LENC_BRMATRX_33, 0x22},
	{OV5640_LENC_BRMATRX_34, 0x26},
	{OV5640_LENC_BRMATRX_40, 0x44},
	{OV5640_LENC_BRMATRX_41, 0x24},
	{OV5640_LENC_BRMATRX_42, 0x26},
	{OV5640_LENC_BRMATRX_43, 0x28},
	{OV5640_LENC_BRMATRX_44, 0x42},
	{OV5640_LENC_BR_OFFSET, 0xce},

        /* magic registers */
	{0x3620, 0x52},
	{0x3621, 0xe0},
	{0x3622, 0x01},
	{0x3630, 0x36},
	{0x3631, 0x0e},
	{0x3632, 0xe2},
	{0x3633, 0x12},
	{0x3634, 0x40},
	{0x3635, 0x13},
	{0x3636, 0x03},
	{0x3703, 0x5a},
	{0x3704, 0xa0},
	{0x3705, 0x1a},
	{0x370b, 0x60},
	{0x3715, 0x78},
	{0x3717, 0x01},
	{0x371b, 0x20},
	{0x3731, 0x12},
	{0x3901, 0x0a},
	{0x3905, 0x02},
        {0x3906, 0x10},
	{0x471c, 0x50},

	{OV5640_TABLE_END, 0x0000},
};

enum {
	OV5640_MODE_640x480,
	OV5640_MODE_1280x960,
	OV5640_MODE_1920x1080,
	OV5640_MODE_2592x1944,
	OV5640_SIZE_LAST,
};

static struct ov5640_reg *mode_table[] = {
	mode_640x480,
	mode_1280x960,
	mode_1920x1080,
	mode_2592x1944,
};

static int pwn_gpio, rst_gpio;
static int test_pattern;

module_param(test_pattern, int, 0644);

static struct ov5640_reg tp_cbars[] = {
	{OV5640_ISP_TEST, ISP_TEST_EN | ISP_TEST_00 | ISP_TEST_TRANSPARENT | ISP_TEST_ROLLING },
	{OV5640_TABLE_END, 0x0000}
};

#define to_ov5640(sd)		container_of(sd, struct ov5640_priv, subdev)

#define SIZEOF_I2C_TRANSBUF 32

struct ov5640_priv {
	struct v4l2_subdev		subdev;
	struct media_pad		pads[MIPI_CSI2_SENS_VCX_PADS_NUM];
	struct v4l2_mbus_framefmt	mf;

	int				ident;
	u16				chip_id;
	u8				revision;
	bool				on;

	int mode;

	struct i2c_client *client;
	u8 i2c_trans_buf[SIZEOF_I2C_TRANSBUF];
};

static const struct v4l2_frmsize_discrete ov5640_frmsizes[] = {
	{640, 480},
	{1280, 960},
	{1920, 1080},
	{2592, 1944},
};

static uint32_t ov5640_codes[] = {
	MEDIA_BUS_FMT_UYVY8_2X8,
};

static int ov5640_find_mode(u32 width, u32 height)
{
	int i;

	for (i = 0; i < OV5640_SIZE_LAST; i++) {
		if ((ov5640_frmsizes[i].width >= width) &&
		    (ov5640_frmsizes[i].height >= height))
			break;
	}

	/* If not found, select biggest */
	if (i >= OV5640_SIZE_LAST)
		i = OV5640_SIZE_LAST - 1;

	return i;
}

static int ov5640_read_reg(struct i2c_client *client, u16 addr, u8 *val)
{
	int err;
	struct i2c_msg msg[2];
	unsigned char data[3];

	if (!client->adapter)
		return -ENODEV;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = data;

	/* high byte goes out first */
	data[0] = (u8) (addr >> 8);
	data[1] = (u8) (addr & 0xff);

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = data + 2;

	err = i2c_transfer(client->adapter, msg, 2);

	if (err != 2)
		return -EINVAL;

	*val = data[2];

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov5640_write_reg(struct i2c_client *client, u16 addr, u8 value)
{
	int count;
	struct i2c_msg msg[1];
	unsigned char data[4];

	if (!client->adapter)
		return -ENODEV;

	data[0] = addr;
	data[1] = (u8) (addr & 0xff);
	data[2] = value;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 3;
	msg[0].buf = data;

	count = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (count == ARRAY_SIZE(msg)) {
		return 0;
	}
	dev_err(&client->dev,
		"ov5840: i2c transfer failed, addr: %x, value: %02x\n",
		addr, (u32)value);
	return -EIO;
}
#endif

static int ov5640_write_bulk_reg(struct i2c_client *client, u8 *data, int len)
{
	int err;
	struct i2c_msg msg;

	if (!client->adapter)
		return -ENODEV;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = len;
	msg.buf = data;

	err = i2c_transfer(client->adapter, &msg, 1);
	if (err == 1)
		return 0;

	dev_err(&client->dev, "ov5640: i2c transfer failed at %x\n",
		(int)data[0] << 8 | data[1]);

	return err;
}

static int ov5640_write_table(struct ov5640_priv *priv,
			      struct ov5640_reg table[])
{
	int err;
	struct ov5640_reg *next, *n_next;
	u8 *b_ptr = priv->i2c_trans_buf;
	unsigned int buf_filled = 0;
	u16 val;

	for (next = table; next->addr != OV5640_TABLE_END; next++) {
		if (next->addr == OV5640_TABLE_WAIT_MS) {
			msleep(next->val);
			continue;
		}

		val = next->val;

		if (!buf_filled) {
			b_ptr = priv->i2c_trans_buf;
			*b_ptr++ = next->addr >> 8;
			*b_ptr++ = next->addr & 0xff;
			buf_filled = 2;
		}
		*b_ptr++ = val;
		buf_filled++;

		n_next = next + 1;
		if (n_next->addr != OV5640_TABLE_END &&
			n_next->addr != OV5640_TABLE_WAIT_MS &&
			buf_filled < SIZEOF_I2C_TRANSBUF &&
			n_next->addr == next->addr + 1) {
			continue;
		}

		err = ov5640_write_bulk_reg(priv->client,
			priv->i2c_trans_buf, buf_filled);
		if (err)
			return err;

		buf_filled = 0;
	}
	return 0;
}

static void ov5640_set_default_fmt(struct ov5640_priv *priv)
{
	struct v4l2_mbus_framefmt *mf = &priv->mf;

	mf->width = ov5640_frmsizes[OV5640_MODE_1920x1080].width;
	mf->height = ov5640_frmsizes[OV5640_MODE_1920x1080].height;
	mf->code = MEDIA_BUS_FMT_UYVY8_2X8;
	mf->field = V4L2_FIELD_NONE;
	mf->colorspace = V4L2_COLORSPACE_SRGB;
}

/* Start/Stop streaming from the device */
static int ov5640_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov5640_priv *priv = to_ov5640(sd);
	int ret = 0;

	if (!enable) {
		ov5640_set_default_fmt(priv);
		return 0;
	}

	ret = ov5640_write_table(priv, mode_common_registers);
	if (ret)
		return ret;

	ret = ov5640_write_table(priv, mode_table[priv->mode]);
	if (ret)
		return ret;

	if (test_pattern == 1) {
		ret = ov5640_write_table(priv, tp_cbars);
		if (ret)
			return ret;
	}

	return ret;
}

static int ov5640_try_fmt(struct v4l2_subdev *sd,
			  struct v4l2_mbus_framefmt *mf)
{
	int mode;

	mode = ov5640_find_mode(mf->width, mf->height);
	mf->width = ov5640_frmsizes[mode].width;
	mf->height = ov5640_frmsizes[mode].height;

	mf->field = V4L2_FIELD_NONE;
	mf->code = MEDIA_BUS_FMT_UYVY8_2X8;
	mf->colorspace = V4L2_COLORSPACE_SRGB;

	return 0;
}

static int ov5640_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(ov5640_codes))
		return -EINVAL;

	code->code = ov5640_codes[code->index];
	return 0;
}

static int ov5640_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct ov5640_priv *priv = to_ov5640(sd);
	struct v4l2_mbus_framefmt *mf = &format->format;

	if (format->pad) {
		return -EINVAL;
	}

	memcpy(mf, &priv->mf, sizeof(struct v4l2_mbus_framefmt));

	return 0;
}

/* set the format we will capture in */
static int ov5640_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct ov5640_priv *priv = to_ov5640(sd);
	int ret;

	ret = ov5640_try_fmt(sd, mf);
	if (ret < 0)
		return ret;

	priv->mode = ov5640_find_mode(mf->width, mf->height);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	memcpy(&priv->mf, mf, sizeof(struct v4l2_mbus_framefmt));

	return 0;
}

static int ov5640_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov5640_priv *sensor = to_ov5640(sd);

	sensor->on = on;

	return 0;
}

static int ov5640_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	return 0;
}

/* Get chip identification */
static int ov5640_g_chip_ident(struct v4l2_subdev *sd,
			       struct v4l2_dbg_chip_ident *id)
{
	struct ov5640_priv *priv = to_ov5640(sd);

	id->ident = priv->ident;
	id->revision = priv->revision;

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov5640_get_register(struct v4l2_subdev *sd,
			       struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	u8 val;

	if (reg->reg & ~0xffff)
		return -EINVAL;

	reg->size = 2;

	ret = ov5640_read_reg(client, reg->reg, &val);
	if (ret)
		return ret;

	reg->val = (__u64)val;

	return ret;
}

static int ov5640_set_register(struct v4l2_subdev *sd,
			       struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->reg & ~0xffff || reg->val & ~0xff)
		return -EINVAL;

	return ov5640_write_reg(client, reg->reg, reg->val);
}
#endif

static int ov5640_get_frame_desc(struct v4l2_subdev *sd, unsigned int pad,
				  struct v4l2_mbus_frame_desc *fd)
{
	return 0;
}

static int ov5640_set_frame_desc(struct v4l2_subdev *sd,
					unsigned int pad,
					struct v4l2_mbus_frame_desc *fd)
{
	return 0;
}

static int ov5640_enum_framesizes(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= OV5640_SIZE_LAST)
		return -EINVAL;

	fse->min_width = fse->max_width = ov5640_frmsizes[fse->index].width;
	fse->min_height = fse->max_height = ov5640_frmsizes[fse->index].height;

	return 0;
}

static int ov5640_enum_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index > 1)
		return -EINVAL;

	/* Force 30 fps */
	fie->interval.numerator   = 1;
	fie->interval.denominator = 30;

	return 0;
}

static const struct v4l2_subdev_pad_ops ov5640_pad_ops = {
	.enum_mbus_code        = ov5640_enum_mbus_code,
	.set_fmt               = ov5640_set_fmt,
	.get_fmt               = ov5640_get_fmt,
	.get_frame_desc        = ov5640_get_frame_desc,
	.set_frame_desc        = ov5640_set_frame_desc,
	.enum_frame_size       = ov5640_enum_framesizes,
	.enum_frame_interval   = ov5640_enum_frame_interval,
};

static struct v4l2_subdev_video_ops ov5640_video_ops = {
	.s_stream		= ov5640_s_stream,
	.querystd 		= ov5640_querystd,
};

static struct v4l2_subdev_core_ops ov5640_core_ops = {
	.g_chip_ident		= ov5640_g_chip_ident,
	.s_power		= ov5640_s_power,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register		= ov5640_get_register,
	.s_register		= ov5640_set_register,
#endif
};

static struct v4l2_subdev_ops ov5640_subdev_ops = {
	.core			= &ov5640_core_ops,
	.video			= &ov5640_video_ops,
	.pad                    = &ov5640_pad_ops,
};

int ov5640_link_setup(struct media_entity *entity,
			  const struct media_pad *local,
			  const struct media_pad *remote, u32 flags)
{
	return 0;
}

struct media_entity_operations ov5640_entity_ops = {
	.link_setup = ov5640_link_setup,
};


static inline void ov5640_power_down(int enable)
{
	if (pwn_gpio < 0)
		return;

	if (!enable)
		gpio_set_value_cansleep(pwn_gpio, 0);
	else
		gpio_set_value_cansleep(pwn_gpio, 1);

	msleep(2);
}

static void ov5640_reset(void)
{
	if (rst_gpio < 0 || pwn_gpio < 0)
		return;

	/* camera reset */
	gpio_set_value(rst_gpio, 1);

	/* camera power dowmn */
	gpio_set_value(pwn_gpio, 1);
	msleep(5);

	gpio_set_value(rst_gpio, 0);
	msleep(1);

	gpio_set_value(pwn_gpio, 0);
	msleep(5);

	gpio_set_value(rst_gpio, 1);
	msleep(5);
}

/*
 * i2c_driver function
 */
static int ov5640_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct pinctrl *pinctrl;
	struct device *dev = &client->dev;
	struct v4l2_subdev *sd;
	int retval;
	u8 chip_id_high, chip_id_low;

	struct ov5640_priv *priv;

	priv = devm_kzalloc(&client->dev, sizeof(struct ov5640_priv),
				GFP_KERNEL);
	if (!priv) {
		dev_err(&client->dev, "Failed to allocate private data!\n");
		return -ENOMEM;
	}

	/* init using default mode */
	priv->mode = 2;
	ov5640_set_default_fmt(priv);

	/* ov5640 pinctrl */
	pinctrl = devm_pinctrl_get_select_default(dev);
	if (IS_ERR(pinctrl))
		dev_warn(dev, "no pin available\n");

	/* request power down pin */
	pwn_gpio = of_get_named_gpio(dev->of_node, "pwn-gpios", 0);
	if (!gpio_is_valid(pwn_gpio)) {
		dev_warn(dev, "no sensor pwdn pin available");
	} else {
		retval = devm_gpio_request_one(dev, pwn_gpio, GPIOF_OUT_INIT_HIGH,
						"ov5640_mipi_pwdn");
		if (retval < 0) {
			dev_warn(dev, "Failed to set power pin\n");
			dev_warn(dev, "retval=%d\n", retval);
			return retval;
		}
	}

	/* request reset pin */
	rst_gpio = of_get_named_gpio(dev->of_node, "rst-gpios", 0);
	if (!gpio_is_valid(rst_gpio))
		dev_warn(dev, "no sensor reset pin available");
	else {
		retval = devm_gpio_request_one(dev, rst_gpio, GPIOF_OUT_INIT_HIGH,
						"ov5640_mipi_reset");
		if (retval < 0) {
			dev_warn(dev, "Failed to set reset pin\n");
			return retval;
		}
	}

	priv->client = client;

	ov5640_reset();

	ov5640_power_down(0);

	retval = ov5640_read_reg(client, OV5640_CHIP_ID_H, &chip_id_high);
	if (retval < 0 || chip_id_high != 0x56) {
		pr_warning("camera ov5640_mipi is not found\n");
		return -ENODEV;
	}
	retval = ov5640_read_reg(client, OV5640_CHIP_ID_L, &chip_id_low);
	if (retval < 0 || chip_id_low != 0x40) {
		pr_warning("camera ov5640_mipi is not found\n");
		return -ENODEV;
	}

	sd = &priv->subdev;
	v4l2_i2c_subdev_init(sd, client, &ov5640_subdev_ops);

	ov5640_s_stream(sd, 1);

	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	priv->pads[MIPI_CSI2_SENS_VC0_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	priv->pads[MIPI_CSI2_SENS_VC1_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	priv->pads[MIPI_CSI2_SENS_VC2_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	priv->pads[MIPI_CSI2_SENS_VC3_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	retval = media_entity_pads_init(&sd->entity, MIPI_CSI2_SENS_VCX_PADS_NUM,
					priv->pads);
	if (retval < 0)
		return retval;

	sd->entity.ops = &ov5640_entity_ops;
	retval = v4l2_async_register_subdev(&priv->subdev);

	if (retval < 0)
		dev_err(&client->dev, "%s--Async register failed, ret=%d\n", __func__, retval);

	pr_info("camera ov5640_mipi is found, ret: %d\n", retval);
	return retval;
}

static int ov5640_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id ov5640_id[] = {
	{"ov5640_mipi", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov5640_id);

static struct i2c_driver ov5640_i2c_driver = {
	.driver = {
		.name = "ov5640_mipi_nv",
	},
	.probe    = ov5640_probe,
	.remove   = ov5640_remove,
	.id_table = ov5640_id,
};

static int __init ov5640_module_init(void)
{
	return i2c_add_driver(&ov5640_i2c_driver);
}

static void __exit ov5640_module_exit(void)
{
	i2c_del_driver(&ov5640_i2c_driver);
}

module_init(ov5640_module_init);
module_exit(ov5640_module_exit);

MODULE_DESCRIPTION("SoC Camera driver for OmniVision OV5640");
MODULE_AUTHOR("Andrew Chew <achew@nvidia.com>");
MODULE_LICENSE("GPL v2");
