// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx FPGA HDMI TX Minimal Subsystem Driver
 *
 * Copyright (C) 2021 Xilinx, Inc.
 *
 * Author: Van Linh
 */

#include <linux/component.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

/**
 * struct xlnx_hdmi - Xilinx HDMI core
 * @dev: device structure
 * @base: device I/O memory for register access
 */
struct xlnx_hdmi {
	struct device *dev;
	void __iomem *base;
};

static int xlnx_hdmi_bind(struct device *dev, struct device *master,
			  void *data)
{
	return 0;
}

static void xlnx_hdmi_unbind(struct device *dev, struct device *master,
			     void *data)
{
}

static const struct component_ops xlnx_hdmi_component_ops = {
	.bind	= xlnx_hdmi_bind,
	.unbind	= xlnx_hdmi_unbind
};

/**
 * xlnx_hdmi_initialize - Initializes the hdmi core
 * @hdmi: HDMI core strcture
 *
 * Return: 0 on success, error code on failure
 */
static int xlnx_hdmi_initialize(struct xlnx_hdmi *hdmi)
{
	return 0;
}

static int xlnx_hdmi_parse_of(struct xlnx_hdmi *hdmi)
{
	return 0;
}

static int xlnx_hdmi_probe(struct platform_device *pdev)
{
	struct xlnx_hdmi *hdmi;
	struct resource *res;
	int ret;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	hdmi->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hdmi->base = devm_ioremap_resource(hdmi->dev, res);
	if (IS_ERR(hdmi->base))
		return PTR_ERR(hdmi->base);

	ret = xlnx_hdmi_parse_of(hdmi);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, hdmi);

	/* initialize hw */
	ret = xlnx_hdmi_initialize(hdmi);
	if (ret) {
		dev_err(hdmi->dev, "hdmi initialization failed\n");
		return ret;
	}

	return component_add(hdmi->dev, &xlnx_hdmi_component_ops);
}

static void xlnx_hdmi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &xlnx_hdmi_component_ops);
}

static const struct of_device_id xlnx_hdmi_of_match[] = {
	{ .compatible = "xlnx,v-hdmi-tx-min-v1.0" },
	{ /* end of table */ },
};

MODULE_DEVICE_TABLE(of, xlnx_hdmi_of_match);

static struct platform_driver xlnx_hdmi_driver = {
	.probe			= xlnx_hdmi_probe,
	.remove			= xlnx_hdmi_remove,
	.driver			= {
		.name		= "xlnx-hdmi",
		.of_match_table	= xlnx_hdmi_of_match,
	},
};

module_platform_driver(xlnx_hdmi_driver);

MODULE_AUTHOR("Van Linh");
MODULE_DESCRIPTION("Xilinx DRM KMS HDMI Minimal Driver");
MODULE_LICENSE("GPL");
