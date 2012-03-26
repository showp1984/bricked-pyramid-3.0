/*
 * Copyright (C) 2011 HTC, Inc.
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

static struct platform_driver android_platform_driver = {
	.driver = { .name = "android_usb" },
};

static ssize_t show_usb_function_switch(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t store_usb_function_switch(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return 0;
}

static DEVICE_ATTR(usb_function_switch, 0664,
		show_usb_function_switch, store_usb_function_switch);

static struct attribute *android_htc_usb_attributes[] = {
	&dev_attr_usb_function_switch.attr,
	NULL,
};

static const struct attribute_group htc_attr_group = {
	.attrs = android_htc_usb_attributes,
};

static int __devinit android_probe(struct platform_device *pdev)
{
	int err;
	err = sysfs_create_group(&pdev->dev.kobj, &htc_attr_group);
	if (err) {
		pr_err("%s: failed to create HTC USB devices\n", __func__);
	}

	return 0;
}
