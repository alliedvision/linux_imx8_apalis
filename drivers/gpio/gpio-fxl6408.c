/*
 *  Copyright (C) 2016 Broadcom Limited.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 */

/**
 * DOC: FXL6408 I2C to GPIO expander.
 *
 * This chip has has 8 GPIO lines out of it, and is controlled by an
 * I2C bus (a pair of lines), providing 4x expansion of GPIO lines.
 * It also provides an interrupt line out for notifying of
 * statechanges.
 *
 * Any preconfigured state will be left in place until the GPIO lines
 * get activated.  At power on, everything is treated as an input.
 *
 * Documentation can be found at:
 * https://www.fairchildsemi.com/datasheets/FX/FXL6408.pdf
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/of_platform.h>

#define FXL6408_DEVICE_ID		0x01
# define FXL6408_RST_INT		BIT(1)
# define FXL6408_SW_RST			BIT(0)

/* Bits set here indicate that the GPIO is an output. */
#define FXL6408_IO_DIR			0x03
/* Bits set here, when the corresponding bit of IO_DIR is set, drive
 * the output high instead of low.
 */
#define FXL6408_OUTPUT			0x05
/* Bits here make the output High-Z, instead of the OUTPUT value. */
#define FXL6408_OUTPUT_HIGH_Z		0x07
/* Bits here define the expected input state of the GPIO.
 * INTERRUPT_STAT bits will be set when the INPUT transitions away
 * from this value.
 */
#define FXL6408_INPUT_DEFAULT_STATE	0x09
/* Bits here enable either pull up or pull down according to
 * FXL6408_PULL_DOWN.
 */
#define FXL6408_PULL_ENABLE		0x0b
/* Bits set here (when the corresponding PULL_ENABLE is set) enable a
 * pull-up instead of a pull-down.
 */
#define FXL6408_PULL_UP			0x0d
/* Returns the current status (1 = HIGH) of the input pins. */
#define FXL6408_INPUT_STATUS		0x0f
/* Mask of pins which can generate interrupts. */
#define FXL6408_INTERRUPT_MASK		0x11
/* Mask of pins which have generated an interrupt.  Cleared on read. */
#define FXL6408_INTERRUPT_STAT		0x13

struct fxl6408_chip {
	struct gpio_chip gpio_chip;
	struct i2c_client *client;
	struct mutex i2c_lock;

	/* Caches of register values so we don't have to read-modify-write. */
	u8 reg_io_dir;
	u8 reg_output;
};

static int fxl6408_gpio_direction_input(struct gpio_chip *gc, unsigned off)
{
	struct fxl6408_chip *chip = gpiochip_get_data(gc);

	mutex_lock(&chip->i2c_lock);
	chip->reg_io_dir &= ~BIT(off);
	i2c_smbus_write_byte_data(chip->client, FXL6408_IO_DIR,
				  chip->reg_io_dir);
	mutex_unlock(&chip->i2c_lock);

	return 0;
}

static int fxl6408_gpio_direction_output(struct gpio_chip *gc,
		unsigned off, int val)
{
	struct fxl6408_chip *chip = gpiochip_get_data(gc);

	mutex_lock(&chip->i2c_lock);
	chip->reg_io_dir |= BIT(off);
	i2c_smbus_write_byte_data(chip->client, FXL6408_IO_DIR,
				  chip->reg_io_dir);
	mutex_unlock(&chip->i2c_lock);

	return 0;
}

static int fxl6408_gpio_get_direction(struct gpio_chip *gc, unsigned off)
{
	struct fxl6408_chip *chip = gpiochip_get_data(gc);

	return (chip->reg_io_dir & BIT(off)) == 0;
}

static int fxl6408_gpio_get_value(struct gpio_chip *gc, unsigned off)
{
	struct fxl6408_chip *chip = gpiochip_get_data(gc);
	u8 reg;

	if (fxl6408_gpio_get_direction(gc, off) == 0)
	{
		reg = chip->reg_output;
	} else {
		mutex_lock(&chip->i2c_lock);
		reg = i2c_smbus_read_byte_data(chip->client, FXL6408_INPUT_STATUS);
		mutex_unlock(&chip->i2c_lock);
	}
	return (reg & BIT(off)) != 0;
}

static void fxl6408_gpio_set_value(struct gpio_chip *gc, unsigned off, int val)
{
	struct fxl6408_chip *chip = gpiochip_get_data(gc);

	mutex_lock(&chip->i2c_lock);

	if (val)
		chip->reg_output |= BIT(off);
	else
		chip->reg_output &= ~BIT(off);

	i2c_smbus_write_byte_data(chip->client, FXL6408_OUTPUT,
				  chip->reg_output);
	mutex_unlock(&chip->i2c_lock);
}


static void fxl6408_gpio_set_multiple(struct gpio_chip *gc,
		unsigned long *mask, unsigned long *bits)
{
	struct fxl6408_chip *chip = gpiochip_get_data(gc);
	mutex_lock(&chip->i2c_lock);
	chip->reg_output = (chip->reg_output & ~mask[0]) | bits[0];
	i2c_smbus_write_byte_data(chip->client, FXL6408_OUTPUT,
				  chip->reg_output);
	mutex_unlock(&chip->i2c_lock);
}

static int fxl6408_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct fxl6408_chip *chip;
	struct gpio_chip *gc;
	unsigned int val;
	int ret;
	u8 device_id;

	/* Check the device ID register to see if it's responding.
	 * This also clears RST_INT as a side effect, so we won't get
	 * the "we've been power cycled" interrupt once we enable
	 * interrupts.
	 */
	device_id = i2c_smbus_read_byte_data(client, FXL6408_DEVICE_ID);
	if (device_id < 0) {
		dev_err(dev, "FXL6408 probe returned %d\n", device_id);
		return device_id;
	} else if (device_id >> 5 != 5) {
		dev_err(dev, "FXL6408 probe returned DID: 0x%02x\n", device_id);
		return -ENODEV;
	}

	/* Disable High-Z of outputs, so that our OUTPUT updates
	 * actually take effect.
	 */
	i2c_smbus_write_byte_data(client, FXL6408_OUTPUT_HIGH_Z, 0);

	chip = devm_kzalloc(dev, sizeof(struct fxl6408_chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	chip->client = client;
	mutex_init(&chip->i2c_lock);

	/* if configured, set initial output state and direction,
	 * otherwise read them from the chip */
	if (of_property_read_u32(dev->of_node, "inital_io_dir", &val)) {
		chip->reg_io_dir = i2c_smbus_read_byte_data(client, FXL6408_IO_DIR);
	} else {
		chip->reg_io_dir = val & 0xff;
		i2c_smbus_write_byte_data(client, FXL6408_IO_DIR, chip->reg_io_dir);
	}
	if (of_property_read_u32(dev->of_node, "inital_output", &val)) {
		chip->reg_output = i2c_smbus_read_byte_data(client, FXL6408_OUTPUT);
	} else {
		chip->reg_output = val & 0xff;
		i2c_smbus_write_byte_data(client, FXL6408_OUTPUT, chip->reg_output);
	}

	gc = &chip->gpio_chip;
	gc->direction_input  = fxl6408_gpio_direction_input;
	gc->direction_output = fxl6408_gpio_direction_output;
	gc->get_direction = fxl6408_gpio_get_direction;
	gc->get = fxl6408_gpio_get_value;
	gc->set = fxl6408_gpio_set_value;
	gc->set_multiple = fxl6408_gpio_set_multiple;
	gc->can_sleep = true;

	gc->base =-1;
	gc->ngpio = 8;
	gc->label = chip->client->name;
	gc->parent = dev;
	gc->owner = THIS_MODULE;

	ret = gpiochip_add_data(gc, chip);
	if (ret)
		return ret;

	i2c_set_clientdata(client, chip);
	return 0;
}

static int fxl6408_remove(struct i2c_client *client)
{
	struct fxl6408_chip *chip = i2c_get_clientdata(client);

	gpiochip_remove(&chip->gpio_chip);

	return 0;
}

static const struct of_device_id fxl6408_dt_ids[] = {
	{ .compatible = "fcs,fxl6408" },
	{ }
};

MODULE_DEVICE_TABLE(of, fxl6408_dt_ids);

static const struct i2c_device_id fxl6408_id[] = {
	{ "fxl6408", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, fxl6408_id);

static struct i2c_driver fxl6408_driver = {
	.driver = {
		.name	= "fxl6408",
		.of_match_table = fxl6408_dt_ids,
	},
	.probe		= fxl6408_probe,
	.remove		= fxl6408_remove,
	.id_table	= fxl6408_id,
};

module_i2c_driver(fxl6408_driver);

MODULE_AUTHOR("Eric Anholt <eric@anholt.net>");
MODULE_DESCRIPTION("GPIO expander driver for FXL6408");
MODULE_LICENSE("GPL");
