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
#include <linux/i2c.h>

#include <drm/drm_of.h>
#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_probe_helper.h>

#define hdmi_mutex_lock(x)	mutex_lock(x)
#define hdmi_mutex_unlock(x)	mutex_unlock(x)

#define HDMI_TX_PIXEL_MAXRATE	2376000

/**
 * struct xlnx_hdmi - Xilinx HDMI core
 * @dev: device structure
 * @base: device I/O memory for register access
 * @encoder: the drm encoder structure
 * @connector: the drm connector structure
 * @ddc: i2c ddc handle
 * @hdmi_mutex: mutex to lock hdmi structure
 * @dpms: current dpms state
 */
struct xlnx_hdmi {
	struct device *dev;
	void __iomem *base;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct i2c_adapter *ddc;
	/* to lock hdmi */
	struct mutex hdmi_mutex;
	int dpms;
};

static inline struct
xlnx_hdmi *encoder_to_hdmi(struct drm_encoder *encoder)
{
	return container_of(encoder, struct xlnx_hdmi, encoder);
}

static inline struct
xlnx_hdmi *connector_to_hdmi(struct drm_connector *connector)
{
	return container_of(connector, struct xlnx_hdmi, connector);
}

static const struct drm_edid *xlnx_hdmi_edid_read(struct xlnx_hdmi *hdmi,
						struct drm_connector *connector)
{
	const struct drm_edid *drm_edid;
	const struct edid *edid;

	if (!hdmi->ddc)
		return NULL;

	drm_edid = drm_edid_read_ddc(connector, hdmi->ddc);
	if (!drm_edid) {
		dev_dbg(hdmi->dev, "failed to get edid\n");
		return NULL;
	}

	/*
	 * FIXME: This should use connector->display_info.is_hdmi and
	 * connector->display_info.has_audio from a path that has read the EDID
	 * and called drm_edid_connector_update().
	 */
	edid = drm_edid_raw(drm_edid);

	dev_dbg(hdmi->dev, "got edid: width[%d] x height[%d]\n",
		edid->width_cm, edid->height_cm);

	return drm_edid;
}

/* DRM connector functions */
static enum drm_connector_status
xlnx_hdmi_connector_detect(struct drm_connector *connector, bool force)
{
	// TODO: connector detect function
	return connector_status_connected;
}

static void xlnx_hdmi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	connector->dev = NULL;
}

static const struct drm_connector_funcs xlnx_hdmi_connector_funcs = {
	.dpms			= drm_helper_connector_dpms,
	.detect			= xlnx_hdmi_connector_detect,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= xlnx_hdmi_connector_destroy,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
	.reset			= drm_atomic_helper_connector_reset,
};

/* DRM connector helper functions */
static int
xlnx_hdmi_connector_mode_valid(struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	enum drm_mode_status status = MODE_OK;

	drm_mode_debug_printmodeline(mode);

	/* no interlace */
	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		status = MODE_NO_INTERLACE;
	}

	/* no doublescan */
	if (mode->flags & DRM_MODE_FLAG_DBLSCAN) {
		status = MODE_NO_DBLESCAN;
	}

	/* pixel clock too high for sink? */
	if (mode->clock > HDMI_TX_PIXEL_MAXRATE)
		status = MODE_CLOCK_HIGH;

	return status;
}

static int xlnx_hdmi_connector_get_modes(struct drm_connector *connector)
{
	struct xlnx_hdmi *hdmi = connector_to_hdmi(connector);
	const struct drm_edid *drm_edid;
	int ret;

	drm_edid = xlnx_hdmi_edid_read(hdmi, connector);

	drm_edid_connector_update(connector, drm_edid);

	if (!drm_edid) {
		dev_info(hdmi->dev, "no edid, assume <= 1024x768 works\n");
		return 0;
	}

	ret = drm_edid_connector_add_modes(connector);
	drm_edid_free(drm_edid);

	return ret;
}

static struct drm_encoder *
xlnx_hdmi_connector_best_encoder(struct drm_connector *connector)
{
	struct xlnx_hdmi *hdmi = connector_to_hdmi(connector);

	return &hdmi->encoder;
}

static struct
drm_connector_helper_funcs xlnx_hdmi_connector_helper_funcs = {
	.get_modes = xlnx_hdmi_connector_get_modes,
	.best_encoder = xlnx_hdmi_connector_best_encoder,
	.mode_valid = xlnx_hdmi_connector_mode_valid,
};

/* DRM encoder functions */
static void xlnx_hdmi_encoder_dpms(struct drm_encoder *encoder, int dpms)
{
	struct xlnx_hdmi *hdmi = encoder_to_hdmi(encoder);

	hdmi_mutex_lock(&hdmi->hdmi_mutex);
	hdmi->dpms = dpms;
	hdmi_mutex_unlock(&hdmi->hdmi_mutex);
}

static void xlnx_hdmi_encoder_enable(struct drm_encoder *encoder)
{
	// struct xlnx_hdmi *hdmi = encoder_to_hdmi(encoder);

	xlnx_hdmi_encoder_dpms(encoder, DRM_MODE_DPMS_ON);
	// xlnx_hdmi_vtc_enable(hdmi);

	// xlnx_hdmi_ext_sysrst_deassert(hdmi);
}

static void xlnx_hdmi_encoder_disable(struct drm_encoder *encoder)
{
	// struct xlnx_hdmi *hdmi = encoder_to_hdmi(encoder);

	xlnx_hdmi_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);

	/* Disable the EXT VRST which actually starts the bridge */
	// xlnx_hdmi_ext_sysrst_assert(hdmi);
	// xlnx_hdmi_vtc_disable(hdmi);
}

/**
 * xlnx_hdmi_encoder_atomic_mode_set - drive the HDMI timing parameters
 *
 * @encoder: pointer to Xilinx DRM encoder
 * @crtc_state: DRM crtc state
 * @connector_state: DRM connector state
 *
 * This function derives the HDMI IP timing parameters from the timing
 * values given to timing module.
 */
static void
xlnx_hdmi_encoder_atomic_mode_set(struct drm_encoder *encoder,
				  struct drm_crtc_state *crtc_state,
				  struct drm_connector_state *connector_state)
{
	// TODO: modeset
}

static const struct
drm_encoder_helper_funcs xlnx_hdmi_encoder_helper_funcs = {
	.dpms = xlnx_hdmi_encoder_dpms,
	.enable = xlnx_hdmi_encoder_enable,
	.disable = xlnx_hdmi_encoder_disable,
	.atomic_mode_set = xlnx_hdmi_encoder_atomic_mode_set,
};

static int xlnx_hdmi_create_connector(struct drm_encoder *encoder)
{
	struct xlnx_hdmi *hdmi = encoder_to_hdmi(encoder);
	struct drm_connector *connector = &hdmi->connector;
	int ret;

	connector->polled = DRM_CONNECTOR_POLL_HPD;
	connector->interlace_allowed = true;

	ret = drm_connector_init(encoder->dev, connector,
				 &xlnx_hdmi_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		dev_err(hdmi->dev, "Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector, &xlnx_hdmi_connector_helper_funcs);
	ret = drm_connector_register(connector);
	if (ret) {
		dev_err(hdmi->dev,
			"Failed to register connector (ret=%d)\n", ret);
		return ret;
	}
	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret) {
		dev_err(hdmi->dev,
			"Failed to attach connector (ret=%d)\n", ret);
		return ret;
	}

	return 0;
}

static const struct drm_encoder_funcs xlnx_hdmi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int xlnx_hdmi_bind(struct device *dev, struct device *master,
			  void *data)
{
	struct xlnx_hdmi *hdmi = dev_get_drvdata(dev);
	struct drm_encoder *encoder = &hdmi->encoder;
	struct drm_device *drm_dev = data;
	int ret;

	encoder->possible_crtcs = 1;
	/* initialize encoder */
	drm_encoder_init(drm_dev, encoder, &xlnx_hdmi_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS, NULL);
	drm_encoder_helper_add(encoder, &xlnx_hdmi_encoder_helper_funcs);

	/* create connector */
	ret = xlnx_hdmi_create_connector(encoder);
	if (ret) {
		dev_err(hdmi->dev, "failed create connector, ret = %d\n", ret);
		drm_encoder_cleanup(encoder);
	}

	return ret;
}

static void xlnx_hdmi_unbind(struct device *dev, struct device *master,
			     void *data)
{
	struct xlnx_hdmi *hdmi = dev_get_drvdata(dev);

	xlnx_hdmi_encoder_dpms(&hdmi->encoder, DRM_MODE_DPMS_OFF);
	drm_encoder_cleanup(&hdmi->encoder);
	drm_connector_cleanup(&hdmi->connector);
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
	/* mutex that protects against concurrent access */
	mutex_init(&hdmi->hdmi_mutex);

	return 0;
}

static int xlnx_hdmi_parse_of(struct xlnx_hdmi *hdmi)
{
	struct device_node *node = hdmi->dev->of_node;
	struct device_node *ddc_node;

	ddc_node = of_parse_phandle(node, "ddc-i2c-bus", 0);
	if (ddc_node) {
		hdmi->ddc = of_get_i2c_adapter_by_node(ddc_node);
		of_node_put(ddc_node);
		if (!hdmi->ddc) {
			dev_err(hdmi->dev, "failed to read ddc node\n");
			return -EPROBE_DEFER;
		}
	} else {
		dev_err(hdmi->dev, "no ddc property found\n");
		return -ENODEV;
	}

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
