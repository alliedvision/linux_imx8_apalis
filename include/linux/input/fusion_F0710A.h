/* linux/input/fusion_F0710A.h
 *
 * Platform data for Fusion F0710A driver
 *
 * Copyright (c) 2013 Toradex AG (stefan.agner@toradex.ch)
 *
 *  For licencing details see kernel-base/COPYING
 */

#ifndef __LINUX_I2C_FUSION_F0710A_H
#define __LINUX_I2C_FUSION_F0710A_H

/* Board specific touch screen initial values */
struct fusion_f0710a_init_data {
	int	(*pinmux_fusion_pins)(void);
	int	gpio_int;
	int	gpio_reset;
};

#endif /*  __LINUX_I2C_FUSION_F0710A_H */
