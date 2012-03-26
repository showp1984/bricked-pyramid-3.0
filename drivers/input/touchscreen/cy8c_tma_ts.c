/* drivers/input/touchscreen/cy8c_tma_ts.c
 *
 * Copyright (C) 2010 HTC Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/cy8c_tma_ts.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>

#define CY8C_I2C_RETRY_TIMES 10

struct cy8c_ts_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	uint16_t version;
	uint8_t id;
	uint16_t intr;
	int (*power) (int on);
	int (*wake)(void);
	uint8_t unlock_attr;
	uint8_t unlock_page;
	struct early_suspend early_suspend;
	uint8_t debug_log_level;
	uint8_t orient;
	uint8_t timeout;
	uint8_t interval;
	uint8_t diag_command;
	uint8_t first_pressed;
	int pre_finger_data[2];
	uint8_t x_channel;
	uint8_t y_channel;
	uint8_t finger_count;
	uint16_t finger_id;
	uint8_t p_finger_count;
	uint16_t p_finger_id;
	uint16_t *filter_level;
	uint8_t grip_suppression;
	uint8_t ambiguous_state;
	uint8_t auto_reset;
	uint32_t sameFilter[3];
	int (*reset)(void);
	uint8_t flag_htc_event;
	uint8_t suspend;
};

static struct cy8c_ts_data *private_ts;

#ifdef CONFIG_HAS_EARLYSUSPEND
static void cy8c_ts_early_suspend(struct early_suspend *h);
static void cy8c_ts_late_resume(struct early_suspend *h);
#endif

static int cy8c_init_panel(struct cy8c_ts_data *ts);
static int cy8c_reset_baseline(void);

static DEFINE_MUTEX(cy8c_mutex);

int i2c_cy8c_read(struct i2c_client *client, uint8_t addr, uint8_t *data, uint8_t length)
{
	int retry;
	struct cy8c_ts_data *ts = private_ts;

	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = &addr,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = data,
		}
	};

	for (retry = 0; retry < CY8C_I2C_RETRY_TIMES; retry++) {
		if (i2c_transfer(client->adapter, msg, 2) == 2)
			break;
		if (ts->wake)
			ts->wake();
		else
			msleep(10);
	}
	if (retry == CY8C_I2C_RETRY_TIMES) {
		printk(KERN_INFO "i2c_read_block retry over %d\n",
			CY8C_I2C_RETRY_TIMES);

		return -EIO;
	}
	return 0;

}

int i2c_cy8c_write(struct i2c_client *client, uint8_t addr, uint8_t *data, uint8_t length)
{
	int retry, loop_i;
	struct cy8c_ts_data *ts = private_ts;
	uint8_t buf[length + 1];

	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = length + 1,
			.buf = buf,
		}
	};

	buf[0] = addr;
	for (loop_i = 0; loop_i < length; loop_i++)
		buf[loop_i + 1] = data[loop_i];

	for (retry = 0; retry < CY8C_I2C_RETRY_TIMES; retry++) {
		if (i2c_transfer(client->adapter, msg, 1) == 1)
			break;
		if (ts->wake)
			ts->wake();
		else
			msleep(10);
	}

	if (retry == CY8C_I2C_RETRY_TIMES) {
		printk(KERN_ERR "i2c_write_block retry over %d\n",
			CY8C_I2C_RETRY_TIMES);

		return -EIO;
	}
	return 0;

}

int i2c_cy8c_write_byte_data(struct i2c_client *client, uint8_t addr, uint8_t value)
{
	return i2c_cy8c_write(client, addr, &value, 1);;
}

static int cy8c_data_toggle(struct cy8c_ts_data *ts)
{
	uint8_t buf = 0;
	int ret = 0;
	ret = i2c_cy8c_read(ts->client, 0x00, &buf, 1);
	if (!ret)
		return i2c_cy8c_write_byte_data(ts->client, 0x00, buf ^= BIT(7));

	return -1;
}

static ssize_t cy8c_vendor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct cy8c_ts_data *ts_data;
	ts_data = private_ts;
	sprintf(buf, "%s_x%4.4X_%2.2X\n", CYPRESS_TMA_NAME,
		ts_data->version, ts_data->id);
	ret = strlen(buf);
	return ret;
}

static DEVICE_ATTR(vendor, S_IRUGO, cy8c_vendor_show, NULL);

static ssize_t cy8c_attn_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0, value = 0;
	struct cy8c_ts_data *ts_data;
	ts_data = private_ts;

	value = gpio_get_value(ts_data->intr);
	sprintf(buf, "attn = %X\n", value);

	ret = strlen(buf);
	return ret;
}

static DEVICE_ATTR(attn, S_IRUGO, cy8c_attn_show, NULL);

static long cy8c_reg_addr;

static ssize_t cy8c_register_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;
	uint8_t ptr[1] = { 0 };
	struct cy8c_ts_data *ts_data;
	ts_data = private_ts;
	if (ts_data->wake)
		ts_data->wake();
	if (i2c_cy8c_read(ts_data->client, cy8c_reg_addr, ptr, 1) < 0) {
		printk(KERN_ERR "%s: read fail\n", __func__);
		return ret;
	}
	ret += sprintf(buf, "addr: %ld, data: %d\n", cy8c_reg_addr, ptr[0]);
	return ret;
}

static ssize_t cy8c_register_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	struct cy8c_ts_data *ts_data;
	char buf_tmp[4];
	long write_da;

	ts_data = private_ts;
	memset(buf_tmp, 0x0, sizeof(buf_tmp));
	if ((buf[0] == 'r' || buf[0] == 'w') && buf[1] == ':' &&
		(buf[5] == ':' || buf[5] == '\n')) {
		memcpy(buf_tmp, buf + 2, 3);
		ret = strict_strtol(buf_tmp, 16, &cy8c_reg_addr);
		printk(KERN_DEBUG "%s: set cy8c_reg_addr is: %ld\n",
						__func__, cy8c_reg_addr);
		if (buf[0] == 'w' && buf[5] == ':' && buf[9] == '\n') {
			memcpy(buf_tmp, buf + 6, 3);
			ret = strict_strtol(buf_tmp, 10, &write_da);
			printk(KERN_DEBUG "write addr: 0x%lX, data: 0x%lX\n",
						cy8c_reg_addr, write_da);
			if (ts_data->wake)
				ts_data->wake();
			ret = i2c_cy8c_write_byte_data(ts_data->client,
						cy8c_reg_addr, write_da);
			if (ret < 0) {
				printk(KERN_ERR "%s: write fail(%d)\n",
								__func__, ret);
			}
		}
	}

	return count;
}

static DEVICE_ATTR(register, (S_IWUSR|S_IRUGO),
	cy8c_register_show, cy8c_register_store);


static ssize_t cy8c_debug_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cy8c_ts_data *ts_data;
	size_t count = 0;
	ts_data = private_ts;

	count += sprintf(buf, "%d\n", ts_data->debug_log_level);

	return count;
}

static ssize_t cy8c_debug_level_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct cy8c_ts_data *ts_data;
	ts_data = private_ts;
	if (buf[0] >= '0' && buf[0] <= '9' && buf[1] == '\n')
		ts_data->debug_log_level = buf[0] - '0';

	return count;
}

static DEVICE_ATTR(debug_level, (S_IWUSR|S_IRUGO),
	cy8c_debug_level_show, cy8c_debug_level_dump);

static ssize_t cy8c_htc_event_read(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct cy8c_ts_data *ts_data;
	size_t count = 0;
	ts_data = private_ts;

	count += sprintf(buf, "%d\n", ts_data->flag_htc_event);

	return count;
}

static ssize_t cy8c_htc_event_write(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct cy8c_ts_data *ts_data;
	int ret = 0;
	long value;
	ts_data = private_ts;

	ret = strict_strtol(buf, 10, &value);
	ts_data->flag_htc_event = value;

	return count;
}

static DEVICE_ATTR(htc_event, (S_IWUSR|S_IRUGO), cy8c_htc_event_read, cy8c_htc_event_write);

static ssize_t cy8c_diag_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cy8c_ts_data *ts_data;
	size_t count = 0;
	uint8_t data[250];
	uint8_t loop_i;
	uint16_t num_nodes;
	ts_data = private_ts;
	memset(data, 0x0, sizeof(data));
	num_nodes = ts_data->x_channel * ts_data->y_channel;

	if (ts_data->diag_command < 4 || ts_data->diag_command > 7)
		return count;

	disable_irq(ts_data->client->irq);
	if (ts_data->wake)
		ts_data->wake();
	i2c_cy8c_read(ts_data->client, 0x00, &data[0], 1);
	i2c_cy8c_write_byte_data(ts_data->client, 0x00,
		(data[0] & 0x8F) | (ts_data->diag_command << 4));
	msleep(80);
	for (loop_i = 0; loop_i < 20; loop_i++) {
		if (!gpio_get_value(ts_data->intr))
			break;
		msleep(20);
	}
	printk(KERN_DEBUG "[%d] change mode to %d\n", loop_i, ts_data->diag_command);
	count += sprintf(buf, "Channel: %d * %d\n", ts_data->x_channel, ts_data->y_channel);

	i2c_cy8c_read(ts_data->client, 0x00, &data[0], 7 + num_nodes);
	if ((data[1] & 0x10) == 0x10) {
		cy8c_init_panel(ts_data);
		i2c_cy8c_read(ts_data->client, 0x00, &data[0], 7 + num_nodes);
	}
	if (ts_data->diag_command == 6) {
		if ((data[1] & 0x40) == 0x40)
			count += sprintf(buf + count, "Global IDAC:\n");
		else
			count += sprintf(buf + count, "Local IDAC:\n");
	} else if (ts_data->diag_command == 7) {
		if ((data[1] & 0x40) == 0x40)
			count += sprintf(buf + count, "RAW count:\n");
		else
			count += sprintf(buf + count, "Baseline:\n");
	}
	for (loop_i = 7; loop_i < 7 + num_nodes; loop_i++) {
		count += sprintf(buf + count, "%5d", data[loop_i]);
		if ((loop_i - 6) % ts_data->y_channel == 0)
			count += sprintf(buf + count, "\n");
	}
	count += sprintf(buf + count, "\n");
	i2c_cy8c_read(ts_data->client, 0x00, &data[0], 1);
	i2c_cy8c_write_byte_data(ts_data->client, 0x00, ((data[0] ^= BIT(7)) & 0x8F));
	msleep(40);
	enable_irq(ts_data->client->irq);

	return count;
}

static ssize_t cy8c_diag_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct cy8c_ts_data *ts_data;
	ts_data = private_ts;

	if (buf[0] >= 0x31 && buf[0] <= 0x34)
		ts_data->diag_command = buf[0] - 0x2D;
	else
		ts_data->diag_command = 0;
	printk(KERN_INFO "ts_data->diag_command = %X\n",  ts_data->diag_command);
	return count;
}

static DEVICE_ATTR(diag, (S_IWUSR|S_IRUGO),
	cy8c_diag_show, cy8c_diag_dump);

static ssize_t cy8c_cal_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cy8c_ts_data *ts_data;
	size_t count = 0;
	uint8_t diag_cmd = 7;
	uint8_t data[256];
	int i, loop_i;
	uint16_t num_nodes;
	ts_data = private_ts;
	memset(data, 0x0, sizeof(data));
	num_nodes = ts_data->x_channel * ts_data->y_channel;

	disable_irq(ts_data->client->irq);
	if (ts_data->wake)
		ts_data->wake();
	i2c_cy8c_read(ts_data->client, 0x00, &data[0], 1);
	i2c_cy8c_write_byte_data(ts_data->client, 0x00,
		(data[0] & 0x8F) | (diag_cmd << 4));
	msleep(80);
	for (loop_i = 0; loop_i < 20; loop_i++) {
		if (!gpio_get_value(ts_data->intr))
			break;
		msleep(20);
	}

	i2c_cy8c_read(ts_data->client, 0x00, &data[0], 1);
	printk(KERN_INFO "[cal_show]change mode to 0x%X\n", data[0]);

	i2c_cy8c_read(ts_data->client, 0x00, &data[0], 7 + num_nodes);
	if ((data[1] & 0x40) == 0x40) {
		for (i = 0; i < 10; i++) {
			i2c_cy8c_write_byte_data(ts_data->client, 0x00,
				((data[0] ^= BIT(7)) & 0x8F) | (diag_cmd << 4));
			msleep(80);
			for (loop_i = 0; loop_i < 20; loop_i++) {
				if (!gpio_get_value(ts_data->intr))
					break;
				msleep(20);
			}

			i2c_cy8c_read(ts_data->client, 0x00, &data[0], 2);
			if (!(data[1] & 0x40))
				break;
			msleep(10);
		}
		i2c_cy8c_read(ts_data->client, 0x00, &data[0], 7 + num_nodes);
	}

	for (i = 7; i < 7 + num_nodes; i++) {
		count += sprintf(buf + count, "%5d", data[i]);
		if ((i - 6) % ts_data->y_channel == 0)
			count += sprintf(buf + count, "\n");
	}
	count += sprintf(buf + count, "\n");

	/* Enter operation mode */
	i2c_cy8c_read(ts_data->client, 0x00, data, 1);
	if ((data[0] & 0x70) == 0x70)
		i2c_cy8c_write_byte_data(ts_data->client, 0x00, ((data[0] ^= BIT(7)) & 0x8F));
	mdelay(64);

	i2c_cy8c_read(ts_data->client, 0x00, &data[0], 2);
	printk(KERN_INFO "[cal_show]change mode to 0x%X 0x%X\n", data[0], data[1]);
	enable_irq(ts_data->client->irq);
	return count;
}

static ssize_t cy8c_cal_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct cy8c_ts_data *ts_data;
	uint8_t cal_command[3] = {0x20, 0x00, 0x00};
	uint8_t data[3] = {0};
	uint8_t loop_i;
	ts_data = private_ts;

	if (buf[0] == 0x31) {
		if (ts_data->wake)
			ts_data->wake();
		i2c_cy8c_read(ts_data->client, 0x00, data, 2);
		if ((data[1] & 0x10) == 0x10) {
			printk(KERN_INFO "Bootloader mode to OP mode2\n");
			cy8c_init_panel(ts_data);
		}

		disable_irq(ts_data->client->irq);

		if (!i2c_cy8c_read(ts_data->client, 0x00, data, 3)) {
			if ((data[2] & 0x0F) >= 1) {
				printk(KERN_INFO "[cal_store]Number of touches %d\n", data[2] & 0x0F);
				enable_irq(ts_data->client->irq);
				return count;
			}
		}

		/* Enter System mode */
		if (!i2c_cy8c_read(ts_data->client, 0x00, &data[0], 1)) {
			i2c_cy8c_write_byte_data(ts_data->client, 0x00,
				(data[0] & 0x8F) | (1 << 4));
			mdelay(64);

			i2c_cy8c_write(ts_data->client, 0x02, &cal_command[0], 3);
			cy8c_data_toggle(ts_data);
			mdelay(500);
			for (loop_i = 0; loop_i < 100; loop_i++) {
				i2c_cy8c_read(ts_data->client, 0x00, data, 3);
				if (data[1] == 0x86) {
					printk(KERN_INFO "[cal_store][%d]status return 0x%X\n",
						loop_i + 1, data[1]);
					break;
				} else
					mdelay(10);
			}
		}
		/* Enter operation mode */
		if (!i2c_cy8c_read(ts_data->client, 0x00, data, 1)) {
			if ((data[0] & 0x70) == 0x10) {
				i2c_cy8c_write_byte_data(ts_data->client, 0x00,
					((data[0] ^= BIT(7)) & 0x8F));
			}
			mdelay(64);
		}

		if (!i2c_cy8c_read(ts_data->client, 0x00, &data[0], 2))
			printk(KERN_INFO "[cal_store]change mode to 0x%X 0x%X\n", data[0], data[1]);

		enable_irq(ts_data->client->irq);
	}
	return count;
}

static DEVICE_ATTR(calibration, (S_IWUSR|S_IRUGO),
	cy8c_cal_show, cy8c_cal_store);

static ssize_t cy8c_unlock_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct cy8c_ts_data *ts_data;
	int unlock = -1;
	ts_data = private_ts;

	if (!sscanf(buf, "%d", &unlock)) {
		printk(KERN_INFO "cy8c_unlock_store sscanf return failed\n");
		return count;
	}

	printk(KERN_INFO "Touch: unlock change to (%d %d %d)\n", unlock, ts_data->finger_count, ts_data->unlock_attr);
	if (unlock == 1 && ts_data->unlock_attr)
		ts_data->unlock_page = 1;

	mutex_lock(&cy8c_mutex);

	if (unlock == 2 && (!ts_data->finger_count)
		&& ts_data->unlock_attr && ts_data->suspend == 0) {
		ts_data->unlock_page = 0;
		cy8c_reset_baseline();
	} else if (unlock == 2 && ts_data->finger_count
		&& ts_data->unlock_attr && ts_data->suspend == 0) {
		ts_data->unlock_page = 0;
	}
	mutex_unlock(&cy8c_mutex);

	return count;
}

static DEVICE_ATTR(unlock, (S_IWUSR|S_IRUGO),
	NULL, cy8c_unlock_store);

static ssize_t cy8c_hw_reset(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct cy8c_ts_data *ts_data;
	ts_data = private_ts;

	printk(KERN_INFO "[TP] Reset touch chip!\n");
	if (ts_data)
		if (ts_data->reset)
			ts_data->reset();

	return count;
}

static DEVICE_ATTR(hw_reset, S_IWUSR,
	NULL, cy8c_hw_reset);

static struct kobject *android_touch_kobj;

static int cy8c_touch_sysfs_init(void)
{
	int ret;
	android_touch_kobj = kobject_create_and_add("android_touch", NULL);
	if (android_touch_kobj == NULL) {
		printk(KERN_ERR "%s: subsystem_register failed\n", __func__);
		ret = -ENOMEM;
		return ret;
	}
	ret = sysfs_create_file(android_touch_kobj, &dev_attr_vendor.attr);
	if (ret) {
		printk(KERN_ERR "%s: sysfs_create_file failed\n", __func__);
		return ret;
	}
	cy8c_reg_addr = 0;
	ret = sysfs_create_file(android_touch_kobj, &dev_attr_register.attr);
	if (ret) {
		printk(KERN_ERR "%s: sysfs_create_file failed\n", __func__);
		return ret;
	}
	ret = sysfs_create_file(android_touch_kobj, &dev_attr_debug_level.attr);
	if (ret) {
		printk(KERN_ERR "%s: sysfs_create_file failed\n", __func__);
		return ret;
	}
	ret = sysfs_create_file(android_touch_kobj, &dev_attr_diag.attr);
	if (ret) {
		printk(KERN_ERR "%s: sysfs_create_file failed\n", __func__);
		return ret;
	}
	ret = sysfs_create_file(android_touch_kobj, &dev_attr_calibration.attr);
	if (ret) {
		printk(KERN_ERR "%s: sysfs_create_file failed\n", __func__);
		return ret;
	}
	ret = sysfs_create_file(android_touch_kobj, &dev_attr_htc_event.attr);
	if (ret) {
		printk(KERN_ERR "%s: sysfs_create_file failed\n", __func__);
		return ret;
	}
	ret = sysfs_create_file(android_touch_kobj, &dev_attr_unlock.attr);
	if (ret) {
		printk(KERN_ERR "%s: sysfs_create_file failed\n", __func__);
		return ret;
	}
	ret = sysfs_create_file(android_touch_kobj, &dev_attr_hw_reset.attr);
	if (ret) {
		printk(KERN_ERR "%s: sysfs_create_file failed\n", __func__);
		return ret;
	}
	ret = sysfs_create_file(android_touch_kobj, &dev_attr_attn.attr);
	if (ret) {
		printk(KERN_ERR "%s: sysfs_create_file failed\n", __func__);
		return ret;
	}
	return 0;
}

static void cy8c_touch_sysfs_deinit(void)
{
	sysfs_remove_file(android_touch_kobj, &dev_attr_diag.attr);
	sysfs_remove_file(android_touch_kobj, &dev_attr_debug_level.attr);
	sysfs_remove_file(android_touch_kobj, &dev_attr_register.attr);
	sysfs_remove_file(android_touch_kobj, &dev_attr_vendor.attr);
	sysfs_remove_file(android_touch_kobj, &dev_attr_htc_event.attr);
	sysfs_remove_file(android_touch_kobj, &dev_attr_unlock.attr);
	sysfs_remove_file(android_touch_kobj, &dev_attr_calibration.attr);
	sysfs_remove_file(android_touch_kobj, &dev_attr_hw_reset.attr);
	sysfs_remove_file(android_touch_kobj, &dev_attr_attn.attr);
	kobject_del(android_touch_kobj);
}

static int cy8c_init_panel(struct cy8c_ts_data *ts)
{
	uint8_t buf = 0, loop_i;
	uint8_t sec_key[] = {0x00, 0xFF, 0xA5, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
	if (i2c_cy8c_write(ts->client, 0x00, sec_key, 11) < 0) {
		printk(KERN_ERR "TOUCH_ERR: init failed to system info mode\n");
		return -1;
	}
	msleep(80);
	for (loop_i = 0; loop_i < 10; loop_i++) {
		if (!gpio_get_value(ts->intr))
			break;
		msleep(10);
	}
	if (!ts->id)
		if (i2c_cy8c_read(ts->client, 0x17, &ts->id, 1) < 0) {
			printk(KERN_ERR "TOUCH_ERR: init failed to check id\n");
			return -1;
		}
	if (ts->timeout)
		i2c_cy8c_write_byte_data(ts->client, 0x1E, ts->timeout);
	if (ts->interval)
		i2c_cy8c_write_byte_data(ts->client, 0x1F, ts->interval);

	if (!i2c_cy8c_read(ts->client, 0x00, &buf, 1)) {
		if ((buf & 0x70) == 0x10)
			i2c_cy8c_write_byte_data(ts->client, 0x00, ((buf ^= BIT(7)) & 0x8F));
		msleep(40);
	}

	return 0;
}

static int cy8c_reset_baseline(void)
{
	struct cy8c_ts_data *ts_data;
	uint8_t data[3] = {0};
	ts_data = private_ts;

	i2c_cy8c_read(ts_data->client, 0x00, data, 2);
	if ((data[1] & 0x10) == 0x10) {
		printk(KERN_INFO "Bootloader mode to OP mode3\n");
		cy8c_init_panel(ts_data);
	}
	i2c_cy8c_read(ts_data->client, 0x1B, &data[0], 1);
	if ((data[0] & 0x01) == 0) {
		i2c_cy8c_write_byte_data(ts_data->client, 0x1B,
		(data[0] | 0x01));
		printk(KERN_INFO "[TOUCH] cy8c reset baseline\n");
		return 0;
	} else {
		printk(KERN_INFO "[TOUCH] cy8c reset baseline bypass\n");
		return 1;
	}
}

static void cy8c_orient(uint16_t *x, uint16_t *y, uint8_t orient)
{
	if (orient & 0x01)
		*x = 1023 - *x;
	if (orient & 0x02)
		*y = 1023 - *y;
	if (orient & 0x04)
		swap(*x, *y);
}

static irqreturn_t cy8c_ts_irq_thread(int irq, void *ptr)
{
	struct cy8c_ts_data *ts = ptr;
	uint8_t buf[32] = {0}, loop_i, loop_j;

	i2c_cy8c_read(ts->client, 0x00, buf, 32);
	if (ts->debug_log_level & 0x1) {
		for (loop_i = 0; loop_i < 32; loop_i++) {
			printk(KERN_INFO "0x%2.2X ", buf[loop_i]);
			if (loop_i % 16 == 15)
				printk("\n");
		}
	}

	if ((buf[1] & 0x10) == 0x10) {
		printk(KERN_INFO "Bootloader mode to OP mode\n");
		if (cy8c_init_panel(ts) < 0)
			printk(KERN_ERR "TOUCH_ERR: %s init failed\n",
			__func__);
	}

	if (buf[2] & 0x10)
		printk(KERN_INFO "[TOUCH] cy8c large object detected\n");
	if ((buf[2] & 0x0F) >= 1) {
		int base = 0x03;
		int report = -1;
		uint16_t finger_data[4][3];
		ts->p_finger_count = ts->finger_count;
		ts->p_finger_id = ts->finger_id;
		ts->finger_count = ((buf[2] & 0x0F) > 4) ? 4 : buf[2] & 0x0F;
		ts->finger_id = buf[8] << 8 | buf[21];
		if (ts->debug_log_level & 0x4)
			printk(KERN_INFO "Finger ID: %X, count: %d\n",
				ts->finger_id, ts->finger_count);
		if (ts->p_finger_count && (ts->finger_count != ts->p_finger_count ||
			ts->finger_id != ts->p_finger_id)) {
			report = 0;

			for (loop_i = ts->p_finger_count < ts->finger_count
				? ts->p_finger_count : ts->finger_count;
				loop_i > 0; loop_i--) {
				for (loop_j = ts->p_finger_count; loop_j > 0; loop_j--) {
					if (ts->debug_log_level & 0x4)
						printk(KERN_INFO "i = %d, j = %d, A = %X, B = %X\n",
							loop_i, loop_j, ((ts->finger_id >> (16 - 4 * loop_i)) & 0x000F),
							((ts->p_finger_id >> (16 - 4 * loop_j)) & 0x000F));

					if (((ts->finger_id >> (16 - 4 * loop_i)) & 0x000F)
						== ((ts->p_finger_id >> (16 - 4 * loop_j)) & 0x000F)) {
						report = loop_i;
						break;
					}
				}
				if (report > 0)
					break;
			}
			if (report >= ts->p_finger_count ||
				report == ts->finger_count || ts->p_finger_count == 0)
				report = -1;
		}
		for (loop_i = 0; loop_i < ts->finger_count; loop_i++) {
			finger_data[loop_i][0] = buf[base] << 8 | buf[base + 1];
			finger_data[loop_i][1] = buf[base + 2] << 8 | buf[base + 3];
			cy8c_orient(&finger_data[loop_i][0], &finger_data[loop_i][1], ts->orient);
			finger_data[loop_i][2] = buf[base + 4];
			if (loop_i % 2 == 1)
				base += 7;
			else
				base += 6;
		}
		if (ts->filter_level[0] &&
			((ts->finger_count > ts->p_finger_count) || report >= 0 || ts->grip_suppression)) {
			for (loop_i = 0; loop_i < ts->finger_count; loop_i++) {
				if ((finger_data[loop_i][0] < ts->filter_level[0] ||
					finger_data[loop_i][0] > ts->filter_level[3]) &&
					!(ts->grip_suppression & BIT(loop_i)) &&
					loop_i >= (report < 0 ? ts->p_finger_count : report)) {
					ts->grip_suppression |= BIT(loop_i);
				} else if ((finger_data[loop_i][0] < ts->filter_level[1] ||
					finger_data[loop_i][0] > ts->filter_level[2]) &&
					(ts->grip_suppression & BIT(loop_i)))
					ts->grip_suppression |= BIT(loop_i);
				else if (finger_data[loop_i][0] > ts->filter_level[1] &&
					finger_data[loop_i][0] < ts->filter_level[2]) {
					ts->grip_suppression &= ~BIT(loop_i);
				}
			}
			ts->ambiguous_state = 0;
			for (loop_i = 0; loop_i < ts->finger_count; loop_i++)
				if (((ts->grip_suppression >> loop_i) & 1) == 1)
					ts->ambiguous_state++;
		}

		if (ts->ambiguous_state == ts->finger_count
			|| ts->ambiguous_state == report) {
			if (ts->flag_htc_event == 0)
				input_mt_sync(ts->input_dev);
			else {
				input_report_abs(ts->input_dev, ABS_MT_AMPLITUDE, 0);
				input_report_abs(ts->input_dev, ABS_MT_POSITION, 1 << 31);
			}
		} else {
		if (report >= 0) {
			if (ts->debug_log_level & 0x4)
				printk(KERN_INFO "Change: %d\n", report);
			if (report == 0) {
				if (ts->flag_htc_event == 0) {
					input_mt_sync(ts->input_dev);
					input_sync(ts->input_dev);
					ts->sameFilter[2] = ts->sameFilter[0] = ts->sameFilter[1] = -1;
				} else {
					input_report_abs(ts->input_dev, ABS_MT_AMPLITUDE, 0);
					input_report_abs(ts->input_dev, ABS_MT_POSITION, 1 << 31);
				}
			} else {
				for (loop_i = 0; loop_i < report; loop_i++) {
					if (!(ts->grip_suppression & BIT(loop_i))) {
						if (ts->flag_htc_event == 0) {
							if (!(finger_data[loop_i][2] == ts->sameFilter[2] &&
								finger_data[loop_i][0] == ts->sameFilter[0] &&
								finger_data[loop_i][1] == ts->sameFilter[1] &&
								(buf[2] & 0x0F) == 1)) {
								input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR,
									finger_data[loop_i][2]);
								input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR,
									finger_data[loop_i][2]);
								input_report_abs(ts->input_dev, ABS_MT_PRESSURE,
									finger_data[loop_i][2]);
								input_report_abs(ts->input_dev, ABS_MT_POSITION_X,
									finger_data[loop_i][0]);
								input_report_abs(ts->input_dev, ABS_MT_POSITION_Y,
									finger_data[loop_i][1]);
								input_mt_sync(ts->input_dev);
								ts->sameFilter[2] = finger_data[loop_i][2];
								ts->sameFilter[0] = finger_data[loop_i][0];
								ts->sameFilter[1] = finger_data[loop_i][1];
							}
						} else {
							input_report_abs(ts->input_dev, ABS_MT_AMPLITUDE,
								finger_data[loop_i][2] << 16 | finger_data[loop_i][2]);
							input_report_abs(ts->input_dev, ABS_MT_POSITION,
								(((report - 1) ==  loop_i) ? BIT(31) : 0)
								| finger_data[loop_i][0] << 16 | finger_data[loop_i][1]);
						}
						if (ts->debug_log_level & 0x2)
							printk(KERN_INFO "Finger %d=> X:%d, Y:%d w:%d, z:%d\n",
								loop_i + 1, finger_data[loop_i][0], finger_data[loop_i][1],
								finger_data[loop_i][2], finger_data[loop_i][2]);
					}
				}
			}
			if (ts->flag_htc_event == 0)
				input_sync(ts->input_dev);

			base = 3;
		}

		for (loop_i = 0; loop_i < ts->finger_count; loop_i++) {
			if (!(ts->grip_suppression & BIT(loop_i))) {
				if (ts->flag_htc_event == 0) {
					if (!(finger_data[loop_i][2] == ts->sameFilter[2] &&
								finger_data[loop_i][0] == ts->sameFilter[0] &&
								finger_data[loop_i][1] == ts->sameFilter[1] &&
								(buf[2] & 0x0F) == 1)) {
						input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR,
							finger_data[loop_i][2]);
						input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR,
							finger_data[loop_i][2]);
						input_report_abs(ts->input_dev, ABS_MT_PRESSURE,
							finger_data[loop_i][2]);
						input_report_abs(ts->input_dev, ABS_MT_POSITION_X,
							finger_data[loop_i][0]);
						input_report_abs(ts->input_dev, ABS_MT_POSITION_Y,
							finger_data[loop_i][1]);
						input_mt_sync(ts->input_dev);
						ts->sameFilter[2] = finger_data[loop_i][2];
						ts->sameFilter[0] = finger_data[loop_i][0];
						ts->sameFilter[1] = finger_data[loop_i][1];
					}
				} else {
					input_report_abs(ts->input_dev, ABS_MT_AMPLITUDE,
						finger_data[loop_i][2] << 16 | finger_data[loop_i][2]);
					input_report_abs(ts->input_dev, ABS_MT_POSITION,
						(((ts->finger_count - 1) ==  loop_i) ? BIT(31) : 0)
						| finger_data[loop_i][0] << 16 | finger_data[loop_i][1]);
				}
				if (ts->debug_log_level & 0x2)
					printk(KERN_INFO "Finger %d=> X:%d, Y:%d w:%d, z:%d\n",
						loop_i + 1, finger_data[loop_i][0], finger_data[loop_i][1],
						finger_data[loop_i][2], finger_data[loop_i][2]);
				}
				if (!ts->first_pressed) {
					ts->first_pressed = 1;
					printk(KERN_INFO "S1@%d,%d\n",
						finger_data[0][0], finger_data[0][1]);
				}
				if (ts->first_pressed == 1) {
					ts->pre_finger_data[0] = finger_data[0][0];
					ts->pre_finger_data[1] = finger_data[0][1];
				}
			}
		}
		if ((ts->unlock_page) &&
			((ts->p_finger_count > ts->finger_count) ||
			(ts->finger_count == 4))) {
			cy8c_reset_baseline();
		}
	} else {
		ts->finger_count = 0;
		ts->p_finger_count = 0;
		ts->p_finger_id = 0x1FFF;
		ts->grip_suppression = 0;
		cy8c_data_toggle(ts);
		if (ts->flag_htc_event == 0) {
			input_mt_sync(ts->input_dev);
			ts->sameFilter[2] = ts->sameFilter[0] = ts->sameFilter[1] = -1;
		} else {
			input_report_abs(ts->input_dev, ABS_MT_AMPLITUDE, 0);
			input_report_abs(ts->input_dev, ABS_MT_POSITION, 1 << 31);
		}
		if (ts->first_pressed == 1) {
			ts->first_pressed = 2;
			printk(KERN_INFO "E%d@%d, %d\n",
				(ts->finger_id >> 12) & 0x000F,
				ts->pre_finger_data[0] , ts->pre_finger_data[1]);
		}

		if (ts->debug_log_level & 0x2)
			printk(KERN_INFO "Finger leave\n");
	}
	if (ts->flag_htc_event == 0) {
		input_report_key(ts->input_dev, BTN_TOUCH, (ts->finger_count > 0)?1:0);
		input_sync(ts->input_dev);
	}

	return IRQ_HANDLED;
}

static int cy8c_ts_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct cy8c_ts_data *ts;
	struct cy8c_i2c_platform_data *pdata;
	int ret = 0;
	uint8_t buf[6] = {0};
	printk(KERN_DEBUG "%s: enter\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "need I2C_FUNC_I2C\n");
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}

	ts = kzalloc(sizeof(struct cy8c_ts_data), GFP_KERNEL);
	if (ts == NULL) {
		dev_err(&client->dev, "allocate cy8c_ts_data failed\n");
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}

	ts->client = client;
	i2c_set_clientdata(client, ts);
	pdata = client->dev.platform_data;
	private_ts = ts;
	if (pdata)
		ts->power = pdata->power;
	if (ts->power) {
		ret = ts->power(1);
		msleep(10);
		if (ret < 0) {
			dev_err(&client->dev, "power on failed\n");
			goto err_power_failed;
		}
	}
	ret = i2c_cy8c_read(ts->client, 0x01, buf, 2);
	if (ret < 0) {
		dev_info(&client->dev, "No Cypress chip found\n");
		goto err_detect_failed;
	}
	if ((buf[0] & 0x10) != 0x10) {
		i2c_cy8c_write_byte_data(ts->client, 0x00, 0x01);
		msleep(200);
		i2c_cy8c_read(ts->client, 0x01, buf, 2);
	}

	ret = i2c_cy8c_read(ts->client, 0x0B, &buf[2], 4);
	if (ret < 0) {
		dev_err(&client->dev, "TOUCH_ERR: Cypress chip abnormal\n");
		goto err_detect_failed;
	}
	dev_info(&client->dev, "buf: %x, %x, %x, %x\n", buf[0], buf[1], buf[2], buf[3]);

	ts->version = buf[2] << 8 | buf[3];
	if ((buf[4] + buf[5]) <= 32) {
		ts->x_channel = buf[4];
		ts->y_channel = buf[5];
	} else {
		ts->x_channel = 21;
		ts->y_channel = 11;
	}
	dev_info(&client->dev,
		"application verion = %X, x_channel = %X, y_channel = %X\n",
		ts->version, ts->x_channel, ts->y_channel);
	if (cy8c_init_panel(ts) < 0)
		printk(KERN_ERR "TOUCH_ERR: %s init failed\n",
		__func__);

	if (pdata) {
		while (pdata->version > ts->version)
			pdata++;
		ts->intr = pdata->gpio_irq;
		ts->orient = pdata->orient;
		ts->timeout = pdata->timeout;
		ts->interval = pdata->interval;
		ts->unlock_attr = pdata->unlock_attr;
		if (ts->unlock_attr)
			ts->unlock_page = 1;
		dev_info(&client->dev, "orient: %d\n", ts->orient);
		ts->wake = pdata->wake;
		ts->filter_level = pdata->filter_level;
		ts->auto_reset = pdata->auto_reset;
		ts->reset = pdata->reset;
		if (ts->wake)
			gpio_request(ts->intr, "touch-intr");
	}

	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		dev_err(&client->dev, "TOUCH_ERR: Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}
	ts->input_dev->name = "cy8c-touchscreen";
	ts->input_dev->id.version = ts->version;
	ts->input_dev->mtsize = 4;/* Initialize buffer with maximum 4 fingers at the same time */

	set_bit(EV_SYN, ts->input_dev->evbit);
	set_bit(EV_ABS, ts->input_dev->evbit);
	set_bit(EV_KEY, ts->input_dev->evbit);

	set_bit(KEY_BACK, ts->input_dev->keybit);
	set_bit(KEY_HOME, ts->input_dev->keybit);
	set_bit(KEY_MENU, ts->input_dev->keybit);
	set_bit(KEY_SEARCH, ts->input_dev->keybit);
	set_bit(BTN_TOUCH, ts->input_dev->keybit);
	if (pdata) {
		printk(KERN_INFO "input_set_abs_params: mix_x %d, max_x %d, min_y %d, max_y %d\n",
			pdata->abs_x_min, pdata->abs_x_max, pdata->abs_y_min, pdata->abs_y_max);

		input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X,
			pdata->abs_x_min, pdata->abs_x_max, 0, 0);
		input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y,
			pdata->abs_y_min, pdata->abs_y_max, 0, 0);
		input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR,
			pdata->abs_pressure_min, pdata->abs_pressure_max, 0, 0);
		input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR,
			pdata->abs_width_min, pdata->abs_width_max, 0, 0);
		input_set_abs_params(ts->input_dev, ABS_MT_PRESSURE,
			pdata->abs_pressure_min, pdata->abs_pressure_max, 0, 0);

		input_set_abs_params(ts->input_dev, ABS_MT_AMPLITUDE,
			0, ((pdata->abs_pressure_max << 16) | pdata->abs_width_max), 0, 0);
		input_set_abs_params(ts->input_dev, ABS_MT_POSITION,
			0, (BIT(31) | (pdata->abs_x_max << 16) | pdata->abs_y_max), 0, 0);
	}

	ret = input_register_device(ts->input_dev);
	if (ret) {
		dev_err(&client->dev,
			"TOUCH_ERR: Unable to register %s input device\n",
			ts->input_dev->name);
		goto err_input_register_device_failed;
	}

	ret = request_threaded_irq(client->irq, NULL, cy8c_ts_irq_thread,
			  IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "cy8c_ts", ts);
	if (ret != 0) {
		dev_err(&client->dev, "TOUCH_ERR: request_irq failed\n");
		dev_err(&client->dev, "TOUCH_ERR: don't support method without irq\n");
		goto err_input_register_device_failed;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_STOP_DRAWING + 1;
	ts->early_suspend.suspend = cy8c_ts_early_suspend;
	ts->early_suspend.resume = cy8c_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

	dev_info(&client->dev, "Start touchscreen %s in %s mode\n",
		 ts->input_dev->name, "interrupt");

	ts->flag_htc_event = 0;
	cy8c_touch_sysfs_init();

	return 0;

err_input_register_device_failed:
	input_free_device(ts->input_dev);

err_input_dev_alloc_failed:
	gpio_free(ts->intr);
err_detect_failed:
err_power_failed:
	kfree(ts);
err_alloc_data_failed:
err_check_functionality_failed:
	return ret;
}

static int cy8c_ts_remove(struct i2c_client *client)
{
	struct cy8c_ts_data *ts = i2c_get_clientdata(client);

	cy8c_touch_sysfs_deinit();

	unregister_early_suspend(&ts->early_suspend);

	free_irq(client->irq, ts);

	input_unregister_device(ts->input_dev);
	gpio_free(ts->intr);
	kfree(ts);

	return 0;
}

static int cy8c_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct cy8c_ts_data *ts = i2c_get_clientdata(client);
	uint8_t buf[2] = {0};

	disable_irq_nosync(client->irq);

	if (!ts->p_finger_count) {
		if (ts->wake)
			ts->wake();
		if (!gpio_get_value(ts->intr))
			cy8c_data_toggle(ts);
	}
	if (!i2c_cy8c_read(ts->client, 0x00, buf, 2))
		printk(KERN_INFO "%s: %x, %x\n", __func__, buf[0], buf[1]);

	ts->first_pressed = 0;
	ts->grip_suppression = 0;
	ts->p_finger_count = 0;
	ts->finger_count = 0;
	ts->unlock_page = 0;

	if ((buf[1] & 0x10) == 0x10)
		if (cy8c_init_panel(ts) < 0)
			printk(KERN_ERR "TOUCH_ERR: %s init failed\n",
			__func__);
	if (buf[0] & 0x70)
		i2c_cy8c_write_byte_data(ts->client, 0x00, buf[0] & 0x8F);
	mutex_lock(&cy8c_mutex);
	i2c_cy8c_write_byte_data(ts->client, 0x00, (buf[0] & 0x8F) | 0x02);
	ts->suspend = 1;
	mutex_unlock(&cy8c_mutex);

	return 0;
}

static int cy8c_ts_resume(struct i2c_client *client)
{
	struct cy8c_ts_data *ts = i2c_get_clientdata(client);
	uint8_t buf[2] = {0};

	if (ts->wake)
		ts->wake();
	ts->suspend = 0;

	if (!i2c_cy8c_read(ts->client, 0x00, buf, 2))
		printk(KERN_INFO "%s: %x, %x\n", __func__, buf[0], buf[1]);
	else if (ts->auto_reset && ts->reset) {
		printk(KERN_INFO "[TP]For PVT device, auto reset for recovery.\n");
		ts->reset();
		if (!i2c_cy8c_read(ts->client, 0x00, buf, 2))
			printk(KERN_INFO "%s: %x, %x\n", __func__, buf[0], buf[1]);
	}

	if ((buf[1] & 0x10) == 0x10)
		if (cy8c_init_panel(ts) < 0)
			printk(KERN_ERR "TOUCH_ERR: %s init failed\n",
			__func__);

	i2c_cy8c_write_byte_data(ts->client, 0x00, (buf[0] & 0x8F) | 0x04);

	ts->unlock_page = 1;

	enable_irq(client->irq);

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void cy8c_ts_early_suspend(struct early_suspend *h)
{
	struct cy8c_ts_data *ts;
	ts = container_of(h, struct cy8c_ts_data, early_suspend);
	cy8c_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void cy8c_ts_late_resume(struct early_suspend *h)
{
	struct cy8c_ts_data *ts;
	ts = container_of(h, struct cy8c_ts_data, early_suspend);
	cy8c_ts_resume(ts->client);
}
#endif

static const struct i2c_device_id cy8c_ts_i2c_id[] = {
	{CYPRESS_TMA_NAME, 0},
	{}
};

static struct i2c_driver cy8c_ts_driver = {
	.id_table = cy8c_ts_i2c_id,
	.probe = cy8c_ts_probe,
	.remove = cy8c_ts_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend = cy8c_ts_suspend,
	.resume = cy8c_ts_resume,
#endif
	.driver = {
		.name = CYPRESS_TMA_NAME,
		.owner = THIS_MODULE,
	},
};

static int __devinit cy8c_ts_init(void)
{
	printk(KERN_INFO "%s: enter\n", __func__);

	return i2c_add_driver(&cy8c_ts_driver);
}

static void __exit cy8c_ts_exit(void)
{
	i2c_del_driver(&cy8c_ts_driver);
}

module_init(cy8c_ts_init);
module_exit(cy8c_ts_exit);

MODULE_DESCRIPTION("Cypress TMA Touchscreen Driver");
MODULE_LICENSE("GPL");

