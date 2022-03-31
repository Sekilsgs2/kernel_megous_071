// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/of.h>

struct userspace_consumer_data {
	struct mutex lock;
	bool enabled;
	struct regulator *supply;
};

static ssize_t reg_show_state(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct userspace_consumer_data *data = dev_get_drvdata(dev);

	if (data->enabled)
		return sprintf(buf, "enabled\n");

	return sprintf(buf, "disabled\n");
}

static ssize_t reg_set_state(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct userspace_consumer_data *data = dev_get_drvdata(dev);
	bool enabled;
	int ret;

	/*
	 * sysfs_streq() doesn't need the \n's, but we add them so the strings
	 * will be shared with show_state(), above.
	 */
	if (sysfs_streq(buf, "enabled\n") || sysfs_streq(buf, "1"))
		enabled = true;
	else if (sysfs_streq(buf, "disabled\n") || sysfs_streq(buf, "0"))
		enabled = false;
	else {
		dev_err(dev, "Configuring invalid mode\n");
		return count;
	}

	mutex_lock(&data->lock);
	if (enabled != data->enabled) {
		if (enabled)
			ret = regulator_enable(data->supply);
		else
			ret = regulator_disable(data->supply);

		if (ret == 0)
			data->enabled = enabled;
		else
			dev_err(dev, "Failed to configure state: %d\n", ret);
	}
	mutex_unlock(&data->lock);

	return count;
}

static DEVICE_ATTR(state, 0644, reg_show_state, reg_set_state);

static struct attribute *attributes[] = {
	&dev_attr_state.attr,
	NULL,
};

static const struct attribute_group attr_group = {
	.attrs	= attributes,
};

static int regulator_userspace_consumer_probe(struct platform_device *pdev)
{
	struct userspace_consumer_data *drvdata;
	int ret;

	drvdata = devm_kzalloc(&pdev->dev,
			       sizeof(struct userspace_consumer_data),
			       GFP_KERNEL);
	if (drvdata == NULL)
		return -ENOMEM;

	mutex_init(&drvdata->lock);

	drvdata->supply = devm_regulator_get(&pdev->dev, "controlled");
	if (IS_ERR(drvdata->supply)) {
		ret = PTR_ERR(drvdata->supply);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to get supply: %d\n", ret);
		return ret;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &attr_group);
	if (ret != 0)
		return ret;

	platform_set_drvdata(pdev, drvdata);

	return 0;
}

static int regulator_userspace_consumer_remove(struct platform_device *pdev)
{
	struct userspace_consumer_data *data = platform_get_drvdata(pdev);

	sysfs_remove_group(&pdev->dev.kobj, &attr_group);

	if (data->enabled)
		regulator_disable(data->supply);

	return 0;
}

static const struct of_device_id ids_of_match[] = {
	{ .compatible = "custom,reg-userspace-consumer", },
	{},
};
MODULE_DEVICE_TABLE(of, ids_of_match);

static struct platform_driver regulator_userspace_consumer_driver = {
	.probe		= regulator_userspace_consumer_probe,
	.remove		= regulator_userspace_consumer_remove,
	.driver		= {
		.name		= "reg-userspace-consumer-of",
		.of_match_table = of_match_ptr(ids_of_match),
	},
};

module_platform_driver(regulator_userspace_consumer_driver);

MODULE_AUTHOR("Mike Rapoport <mike@compulab.co.il>");
MODULE_DESCRIPTION("Userspace consumer for voltage and current regulators");
MODULE_LICENSE("GPL");
