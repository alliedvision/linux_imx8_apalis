/*
 *  "fusion_F0710A"  touchscreen driver
 *	
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <asm/irq.h>
#include <linux/gpio.h>
#include <linux/input/fusion_F0710A.h>
#include <linux/input/mt.h>
#include <linux/slab.h>
#include <linux/of_gpio.h>


#include "fusion_F0710A.h"

#define DRV_NAME		"fusion_F0710A"
#define MAX_TOUCHES 2

static struct fusion_F0710A_data fusion_F0710A;

static unsigned short normal_i2c[] = { fusion_F0710A_I2C_SLAVE_ADDR, I2C_CLIENT_END };

static int fusion_F0710A_write_u8(u8 addr, u8 data) 
{
	return i2c_smbus_write_byte_data(fusion_F0710A.client, addr, data);
}

static int fusion_F0710A_read_u8(u8 addr)
{
	return i2c_smbus_read_byte_data(fusion_F0710A.client, addr);
}

static int fusion_F0710A_read_block(u8 addr, u8 len, u8 *data)
{
	u8 msgbuf0[1] = { addr };
	u16 slave = fusion_F0710A.client->addr;
	u16 flags = fusion_F0710A.client->flags;
	struct i2c_msg msg[2] = { { slave, flags, 1, msgbuf0 },
				  { slave, flags | I2C_M_RD, len, data }
	};

	return i2c_transfer(fusion_F0710A.client->adapter, msg, ARRAY_SIZE(msg));
}

static int fusion_F0710A_register_input(void)
{
	int ret;
	struct input_dev *dev;

	dev = fusion_F0710A.input = input_allocate_device();
	if (dev == NULL)
		return -ENOMEM;

	dev->name = "fusion_F0710A";

	set_bit(EV_KEY, dev->evbit);
	set_bit(EV_ABS, dev->evbit);
	set_bit(EV_SYN, dev->evbit);
	set_bit(BTN_TOUCH, dev->keybit);

	input_mt_init_slots(dev, MAX_TOUCHES, 0);
	input_set_abs_params(dev, ABS_MT_POSITION_X, 0, fusion_F0710A.info.xres-1, 0, 0);
	input_set_abs_params(dev, ABS_MT_POSITION_Y, 0, fusion_F0710A.info.yres-1, 0, 0);
	input_set_abs_params(dev, ABS_MT_PRESSURE, 0, 255, 0, 0);

	input_set_abs_params(dev, ABS_X, 0, fusion_F0710A.info.xres-1, 0, 0);
	input_set_abs_params(dev, ABS_Y, 0, fusion_F0710A.info.yres-1, 0, 0);
	input_set_abs_params(dev, ABS_PRESSURE, 0, 255, 0, 0);

	ret = input_register_device(dev);
	if (ret < 0)
		goto bail1;

	return 0;

bail1:
	input_free_device(dev);
	return ret;
}

static void fusion_F0710A_reset(void)
{
	/* Generate a 0 => 1 edge explicitly, and wait for startup... */
	gpio_set_value(fusion_F0710A.gpio_reset, 0);
	msleep(10);
	gpio_set_value(fusion_F0710A.gpio_reset, 1);
	/* Wait for startup (up to 125ms according to datasheet) */
	msleep(125);
}

#define WC_RETRY_COUNT 		3
static int fusion_F0710A_write_complete(void)
{
	int ret, i;

	for(i=0; i<WC_RETRY_COUNT; i++)
	{
		ret = fusion_F0710A_write_u8(fusion_F0710A_SCAN_COMPLETE, 0);
		if(ret == 0)
			break;
		else {
			dev_warn(&fusion_F0710A.client->dev,
				 "Write complete failed(%d): %d. Resetting controller...\n", i, ret);
			fusion_F0710A_reset();
		}
	}

	return ret;
}

#define DATA_START	fusion_F0710A_DATA_INFO
#define	DATA_END	fusion_F0710A_SEC_TIDTS
#define DATA_LEN	(DATA_END - DATA_START + 1)
#define DATA_OFF(x)	((x) - DATA_START)

static int fusion_F0710A_read_sensor(void)
{
	int ret;
	u8 data[DATA_LEN];

#define DATA(x) (data[DATA_OFF(x)])
	/* To ensure data coherency, read the sensor with a single transaction. */
	ret = fusion_F0710A_read_block(DATA_START, DATA_LEN, data);
	if (ret < 0) {
		dev_err(&fusion_F0710A.client->dev,
			"Read block failed: %d\n", ret);
		
		return ret;
	}

	fusion_F0710A.f_num = DATA(fusion_F0710A_DATA_INFO)&0x03;
	
	fusion_F0710A.y1 = DATA(fusion_F0710A_POS_X1_HI) << 8;
	fusion_F0710A.y1 |= DATA(fusion_F0710A_POS_X1_LO);
	fusion_F0710A.x1 = DATA(fusion_F0710A_POS_Y1_HI) << 8;
	fusion_F0710A.x1 |= DATA(fusion_F0710A_POS_Y1_LO);
	fusion_F0710A.z1 = DATA(fusion_F0710A_FIR_PRESS);
	fusion_F0710A.tip1 = DATA(fusion_F0710A_FIR_TIDTS)&0x0f;
	fusion_F0710A.tid1 = (DATA(fusion_F0710A_FIR_TIDTS)&0xf0)>>4;
	
	
	fusion_F0710A.y2 = DATA(fusion_F0710A_POS_X2_HI) << 8;
	fusion_F0710A.y2 |= DATA(fusion_F0710A_POS_X2_LO);
	fusion_F0710A.x2 = DATA(fusion_F0710A_POS_Y2_HI) << 8;
	fusion_F0710A.x2 |= DATA(fusion_F0710A_POS_Y2_LO);
	fusion_F0710A.z2 = DATA(fusion_F0710A_SEC_PRESS);
	fusion_F0710A.tip2 = DATA(fusion_F0710A_SEC_TIDTS)&0x0f;
	fusion_F0710A.tid2 =(DATA(fusion_F0710A_SEC_TIDTS)&0xf0)>>4;
#undef DATA

	return 0;
}

#define val_cut_max(x, max, reverse)	\
do					\
{					\
	if(x > max)			\
		x = max;		\
	if(reverse)			\
		x = (max) - (x);	\
}					\
while(0)

static void fusion_F0710A_wq(struct work_struct *work)
{
	struct input_dev *dev = fusion_F0710A.input;

	if (fusion_F0710A_read_sensor() < 0)
		goto restore_irq;

#ifdef DEBUG
	printk(KERN_DEBUG "tip1, tid1, x1, y1, z1 (%x,%x,%d,%d,%d); tip2, tid2, x2, y2, z2 (%x,%x,%d,%d,%d)\n",
		fusion_F0710A.tip1, fusion_F0710A.tid1, fusion_F0710A.x1, fusion_F0710A.y1, fusion_F0710A.z1,
		fusion_F0710A.tip2, fusion_F0710A.tid2, fusion_F0710A.x2, fusion_F0710A.y2, fusion_F0710A.z2);
#endif /* DEBUG */

	val_cut_max(fusion_F0710A.x1, fusion_F0710A.info.xres-1, fusion_F0710A.info.xy_reverse);
	val_cut_max(fusion_F0710A.y1, fusion_F0710A.info.yres-1, fusion_F0710A.info.xy_reverse);
	val_cut_max(fusion_F0710A.x2, fusion_F0710A.info.xres-1, fusion_F0710A.info.xy_reverse);
	val_cut_max(fusion_F0710A.y2, fusion_F0710A.info.yres-1, fusion_F0710A.info.xy_reverse);

	if (fusion_F0710A.tid1) {
		input_mt_slot(dev, fusion_F0710A.tid1 - 1);
		input_mt_report_slot_state(dev, MT_TOOL_FINGER, fusion_F0710A.tip1);
		if (fusion_F0710A.tip1) {
			input_report_abs(dev, ABS_MT_POSITION_X, fusion_F0710A.x1);
			input_report_abs(dev, ABS_MT_POSITION_Y, fusion_F0710A.y1);
			input_report_abs(dev, ABS_MT_PRESSURE, fusion_F0710A.z1);
		}
	}

	if (fusion_F0710A.tid2) {
		input_mt_slot(dev, fusion_F0710A.tid2 - 1);
		input_mt_report_slot_state(dev, MT_TOOL_FINGER, fusion_F0710A.tip2);
		if (fusion_F0710A.tip2) {
			input_report_abs(dev, ABS_MT_POSITION_X, fusion_F0710A.x2);
			input_report_abs(dev, ABS_MT_POSITION_Y, fusion_F0710A.y2);
			input_report_abs(dev, ABS_MT_PRESSURE, fusion_F0710A.z2);
		}
	}

	input_mt_report_pointer_emulation(dev, false);
	input_sync(dev);

restore_irq:
	enable_irq(fusion_F0710A.client->irq);

	/* Clear fusion_F0710A interrupt */
	fusion_F0710A_write_complete();
}
static DECLARE_WORK(fusion_F0710A_work, fusion_F0710A_wq);

static irqreturn_t fusion_F0710A_interrupt(int irq, void *dev_id)
{
	disable_irq_nosync(fusion_F0710A.client->irq);

	queue_work(fusion_F0710A.workq, &fusion_F0710A_work);

	return IRQ_HANDLED;
}

const static u8* g_ver_product[4] = {
	"10Z8", "70Z7", "43Z6", ""
};

static int of_fusion_F0710A_get_pins(struct device_node *np,
				unsigned int *int_pin, unsigned int *reset_pin)
{
	if (of_gpio_count(np) < 2)
		return -ENODEV;

	*int_pin = of_get_gpio(np, 0);
	*reset_pin = of_get_gpio(np, 1);

	if (!gpio_is_valid(*int_pin) || !gpio_is_valid(*reset_pin)) {
		pr_err("%s: invalid GPIO pins, int=%d/reset=%d\n",
		       np->full_name, *int_pin, *reset_pin);
		return -ENODEV;
	}

	return 0;
}

static int fusion_F0710A_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct device_node *np = i2c->dev.of_node;
	struct fusion_f0710a_init_data *pdata = i2c->dev.platform_data;
	int ret;
	u8 ver_product, ver_id;
	u32 version;

	if (np != NULL) {
		pdata = i2c->dev.platform_data =
			devm_kzalloc(&i2c->dev, sizeof(*pdata), GFP_KERNEL);
		if (pdata == NULL) {
			dev_err(&i2c->dev, "No platform data for Fusion driver\n");
			return -ENODEV;
		}
		/* the dtb did the pinmuxing for us */
		pdata->pinmux_fusion_pins = NULL;
		ret = of_fusion_F0710A_get_pins(i2c->dev.of_node,
				&pdata->gpio_int, &pdata->gpio_reset);
		if (ret)
			return ret;
	}
	else if (pdata == NULL) {
		dev_err(&i2c->dev, "No platform data for Fusion driver\n");
		return -ENODEV;
	}

	/* Request pinmuxing, if necessary */
	if (pdata->pinmux_fusion_pins != NULL) {
		ret = pdata->pinmux_fusion_pins();
		if (ret < 0) {
			dev_err(&i2c->dev, "muxing GPIOs failed\n");
			return -ENODEV;
		}
	}

	if ((gpio_request(pdata->gpio_int, "Fusion pen down interrupt") == 0) &&
	    (gpio_direction_input(pdata->gpio_int) == 0)) {
		gpio_export(pdata->gpio_int, 0);
	} else {
		dev_warn(&i2c->dev, "Could not obtain GPIO for Fusion pen down\n");
		return -ENODEV;
	}

	if ((gpio_request(pdata->gpio_reset, "Fusion reset") == 0) &&
	    (gpio_direction_output(pdata->gpio_reset, 1) == 0)) {
		fusion_F0710A.gpio_reset = pdata->gpio_reset;
		fusion_F0710A_reset();
		gpio_export(pdata->gpio_reset, 0);
	} else {
		dev_warn(&i2c->dev, "Could not obtain GPIO for Fusion reset\n");
		ret = -ENODEV;
		goto bail0;
	}

	/* Use Pen Down GPIO as sampling interrupt */
	i2c->irq = gpio_to_irq(pdata->gpio_int);
	irq_set_irq_type(i2c->irq, IRQ_TYPE_LEVEL_HIGH);

	if(!i2c->irq)
	{
		dev_err(&i2c->dev, "fusion_F0710A irq < 0 \n");
		ret = -ENOMEM;
		goto bail1;
	}

	/* Attach the I2C client */
	fusion_F0710A.client =  i2c;
	i2c_set_clientdata(i2c, &fusion_F0710A);

	dev_info(&i2c->dev, "Touchscreen registered with bus id (%d) with slave address 0x%x\n",
			i2c_adapter_id(fusion_F0710A.client->adapter),	fusion_F0710A.client->addr);

	/* Read out a lot of registers */
	ret = fusion_F0710A_read_u8(fusion_F0710A_VIESION_INFO_LO);
	if (ret < 0) {
		dev_err(&i2c->dev, "query failed: %d\n", ret);
		goto bail1;
	}
	ver_product = (((u8)ret) & 0xc0) >> 6;
	version = (10 + ((((u32)ret)&0x30) >> 4)) * 100000;
	version += (((u32)ret)&0xf) * 1000;
	/* Read out a lot of registers */
	ret = fusion_F0710A_read_u8(fusion_F0710A_VIESION_INFO);
		if (ret < 0) {
		dev_err(&i2c->dev, "query failed: %d\n", ret);
		goto bail1;
	}
	ver_id = ((u8)(ret) & 0x6) >> 1;
	version += ((((u32)ret) & 0xf8) >> 3) * 10;
	version += (((u32)ret) & 0x1) + 1; /* 0 is build 1, 1 is build 2 */
	dev_info(&i2c->dev, "version product %s(%d)\n", g_ver_product[ver_product] ,ver_product);
	dev_info(&i2c->dev, "version id %s(%d)\n", ver_id ? "1.4" : "1.0", ver_id);
	dev_info(&i2c->dev, "version series (%d)\n", version);

	switch(ver_product)
	{
	case fusion_F0710A_VIESION_07: /* 7 inch */
		fusion_F0710A.info.xres = fusion_F0710A07_XMAX;
		fusion_F0710A.info.yres = fusion_F0710A07_YMAX;
		fusion_F0710A.info.xy_reverse = fusion_F0710A07_REV;
		break;
	case fusion_F0710A_VIESION_43: /* 4.3 inch */
		fusion_F0710A.info.xres = fusion_F0710A43_XMAX;
		fusion_F0710A.info.yres = fusion_F0710A43_YMAX;
		fusion_F0710A.info.xy_reverse = fusion_F0710A43_REV;
		break;
	default: /* fusion_F0710A_VIESION_10 10 inch */
		fusion_F0710A.info.xres = fusion_F0710A10_XMAX;
		fusion_F0710A.info.yres = fusion_F0710A10_YMAX;
		fusion_F0710A.info.xy_reverse = fusion_F0710A10_REV;
		break;
	}

	/* Register the input device. */
	ret = fusion_F0710A_register_input();
	if (ret < 0) {
		dev_err(&i2c->dev, "can't register input: %d\n", ret);
		goto bail1;
	}

	/* Create a worker thread */
	fusion_F0710A.workq = create_singlethread_workqueue(DRV_NAME);
	if (fusion_F0710A.workq == NULL) {
		dev_err(&i2c->dev, "can't create work queue\n");
		ret = -ENOMEM;
		goto bail2;
	}


	/* Register for the interrupt and enable it. Our handler will
	*  start getting invoked after this call. */
	ret = request_irq(i2c->irq, fusion_F0710A_interrupt, IRQF_TRIGGER_RISING,
	i2c->name, &fusion_F0710A);
	if (ret < 0) {
		dev_err(&i2c->dev, "can't get irq %d: %d\n", i2c->irq, ret);
		goto bail3;
	}
	/* clear the irq first */
	ret = fusion_F0710A_write_u8(fusion_F0710A_SCAN_COMPLETE, 0);
	if (ret < 0) {
		dev_err(&i2c->dev, "Clear irq failed: %d\n", ret);
		goto bail4;
	}

	return 0;

bail4:
	free_irq(i2c->irq, &fusion_F0710A);

bail3:
	destroy_workqueue(fusion_F0710A.workq);
	fusion_F0710A.workq = NULL;

bail2:
	input_unregister_device(fusion_F0710A.input);
bail1:
	gpio_free(pdata->gpio_reset);
bail0:
	gpio_free(pdata->gpio_int);

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int fusion_F0710A_suspend(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	disable_irq(i2c->irq);
	flush_workqueue(fusion_F0710A.workq);

	return 0;
}

static int fusion_F0710A_resume(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	enable_irq(i2c->irq);

	return 0;
}
#endif

static int fusion_F0710A_remove(struct i2c_client *i2c)
{
	struct fusion_f0710a_init_data *pdata = i2c->dev.platform_data;

	gpio_free(pdata->gpio_int);
	gpio_free(pdata->gpio_reset);
	destroy_workqueue(fusion_F0710A.workq);
	free_irq(i2c->irq, &fusion_F0710A);
	input_unregister_device(fusion_F0710A.input);
	i2c_set_clientdata(i2c, NULL);

	dev_info(&i2c->dev, "driver removed\n");
	
	return 0;
}

static struct i2c_device_id fusion_F0710A_id[] = {
	{"fusion_F0710A", 0},
	{},
};


static const struct of_device_id fusion_F0710A_dt_ids[] = {
	{
		.compatible = "touchrevolution,fusion-f0710a",
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, fusion_F0710A_dt_ids);

static const struct dev_pm_ops fusion_F0710A_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fusion_F0710A_suspend, fusion_F0710A_resume)
};

static struct i2c_driver fusion_F0710A_i2c_drv = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= DRV_NAME,
		.pm		= &fusion_F0710A_pm_ops,
		.of_match_table = fusion_F0710A_dt_ids,
	},
	.probe          = fusion_F0710A_probe,
	.remove         = fusion_F0710A_remove,
	.id_table       = fusion_F0710A_id,
	.address_list   = normal_i2c,
};

static int __init fusion_F0710A_init( void )
{
	int ret;

	memset(&fusion_F0710A, 0, sizeof(fusion_F0710A));

	/* Probe for fusion_F0710A on I2C. */
	ret = i2c_add_driver(&fusion_F0710A_i2c_drv);
	if (ret < 0) {
		printk(KERN_WARNING DRV_NAME " can't add i2c driver: %d\n", ret);
	}

	return ret;
}

static void __exit fusion_F0710A_exit( void )
{
	i2c_del_driver(&fusion_F0710A_i2c_drv);
}
module_init(fusion_F0710A_init);
module_exit(fusion_F0710A_exit);

MODULE_DESCRIPTION("fusion_F0710A Touchscreen Driver");
MODULE_LICENSE("GPL");

