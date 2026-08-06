// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi SIM tray status driver
 * Ported from the 5.10 xaga kernel to 6.12 (GPIO descriptor API).
 */

#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct simtray_data {
	struct device *dev;
	struct gpio_desc *status_gpio;
};

static ssize_t simtray_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct simtray_data *data = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", gpiod_get_value(data->status_gpio));
}
static DEVICE_ATTR(status, 0444, simtray_status_show, NULL);

static int simtray_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct simtray_data *data;

	data = devm_kzalloc(dev, sizeof(struct simtray_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->status_gpio = devm_gpiod_get_optional(dev, "status", GPIOD_IN);
	if (IS_ERR(data->status_gpio))
		return PTR_ERR(data->status_gpio);
	if (!data->status_gpio)
		return -EINVAL;

	ret = device_create_file(dev, &dev_attr_status);
	if (ret < 0) {
		dev_err(dev, "Failed to create sysfs node.\n");
		return ret;
	}

	data->dev = dev;
	platform_set_drvdata(pdev, data);

	return 0;
}

static void simtray_remove(struct platform_device *pdev)
{
	device_remove_file(&pdev->dev, &dev_attr_status);
}

static const struct of_device_id simtray_of_match[] = {
	{ .compatible = "xiaomi,simtray-status", },
	{},
};
MODULE_DEVICE_TABLE(of, simtray_of_match);

static struct platform_driver simtray_status_driver = {
	.driver = {
		.name = "simtray-status",
		.of_match_table = simtray_of_match,
	},
	.probe = simtray_probe,
	.remove_new = simtray_remove,
};

module_platform_driver(simtray_status_driver);
MODULE_AUTHOR("Tao Jun<taojun@xiaomi.com>");
MODULE_DESCRIPTION("Xiaomi SIM tray status");
MODULE_LICENSE("GPL");
