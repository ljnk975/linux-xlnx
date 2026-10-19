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
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>

#include <drm/drm_of.h>
#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>

#include <uapi/linux/media-bus-format.h>

#define hdmi_mutex_lock(x)	mutex_lock(x)
#define hdmi_mutex_unlock(x)	mutex_unlock(x)

#define HDMI_TX_PIXEL_MAXRATE	150000

/* VTC register offsets and bit masks */
#define HDMI_TX_VTC_BASE		0x0

#define HDMI_TX_VTC_CTL			0x000
#define HDMI_TX_VTC_CTL_MASK			GENMASK(18, 8)
#define HDMI_TX_VTC_RST			BIT(31)
#define HDMI_TX_VTC_CTL_GE			BIT(2)
#define HDMI_TX_VTC_CTL_RU			BIT(1)

#define HDMI_TX_VTC_GASIZE_F0			0x060
#define HDMI_TX_VTC_ACTIVE_SIZE_MASK		GENMASK(13, 0)

#define HDMI_TX_VTC_GFENC			0x068
#define HDMI_TX_VTC_GFENC_MASK			BIT(6)

#define HDMI_TX_VTC_GPOL			0x06c
#define HDMI_TX_VTC_GPOL_FIELD_ID_POL		BIT(6)
#define HDMI_TX_VTC_ACTIVE_CHROMA_POL		BIT(5)
#define HDMI_TX_VTC_ACTIVE_VIDEO_POL		BIT(4)
#define HDMI_TX_VTC_HSYNC_POL			BIT(3)
#define HDMI_TX_VTC_VSYNC_POL			BIT(2)
#define HDMI_TX_VTC_HBLANK_POL			BIT(1)
#define HDMI_TX_VTC_VBLANK_POL			BIT(0)
#define HDMI_TX_VTC_GPOL_MASK		(HDMI_TX_VTC_VBLANK_POL |\
					 HDMI_TX_VTC_HBLANK_POL |\
					 HDMI_TX_VTC_VSYNC_POL |\
					 HDMI_TX_VTC_HSYNC_POL |\
					 HDMI_TX_VTC_ACTIVE_VIDEO_POL |\
					 HDMI_TX_VTC_ACTIVE_CHROMA_POL)

#define HDMI_TX_VTC_INT_GPOL_MASK	(HDMI_TX_VTC_GPOL_FIELD_ID_POL |\
					 HDMI_TX_VTC_ACTIVE_CHROMA_POL |\
					 HDMI_TX_VTC_ACTIVE_VIDEO_POL)

#define HDMI_TX_VTC_GHSIZE			0x070
#define HDMI_TX_VTC_GHSIZE_FRAME_HSIZE		GENMASK(13, 0)

#define HDMI_TX_VTC_GVSIZE			0x074
#define HDMI_TX_VTC_FIELD1_VSIZE_SHIFT		16
#define HDMI_TX_VTC_GVSIZE_FRAME_VSIZE		GENMASK(13, 0)

#define HDMI_TX_VTC_GHSYNC			0x078
#define HDMI_TX_VTC_GH1BPSTART_SHIFT		16
#define HDMI_TX_VTC_GHSYNC_END_MASK		GENMASK(29, 16)
#define HDMI_TX_VTC_GHSYNC_START_MASK		GENMASK(13, 0)

#define HDMI_TX_VTC_GVBHOFF			0x07c
#define HDMI_TX_VTC_F0VSYNC_HEND_SHIFT		16
#define HDMI_TX_VTC_F0VBLANK_HEND_MASK		GENMASK(29, 16)
#define HDMI_TX_VTC_F0VBLANK_HSTART_MASK	GENMASK(13, 0)

#define HDMI_TX_VTC_GVSYNC			0x080
#define HDMI_TX_VTC_F0_VSYNC_VEND_MASK		GENMASK(29, 16)
#define HDMI_TX_VTC_F0_VSYNC_VSTART_MASK	GENMASK(13, 0)

#define HDMI_TX_VTC_GVSHOFF			0x084
#define HDMI_TX_VTC_GVBHOFF_F1			0x088
#define HDMI_TX_VTC_GVSYNC_F1			0x08c
#define HDMI_TX_VTC_GVSHOFF_F1			0x090
#define HDMI_TX_VTC_GASIZE_F1			0x094

enum color_formats {
	HDMI_TX_CSF_RGB = 0,
	HDMI_TX_CSF_YCRCB_444 = 1,
	HDMI_TX_CSF_YCRCB_422 = 2,
	HDMI_TX_CSF_YCRCB_420 = 3
};

enum color_depths {
	HDMI_TX_BPC_8 = 8,
	HDMI_TX_BPC_10 = 10,
	HDMI_TX_BPC_12 = 12,
	HDMI_TX_BPC_16 = 16
};

/**
 * struct xlnx_hdmi - Xilinx HDMI core
 * @dev: device structure
 * @base: device I/O memory for register access
 * @encoder: the drm encoder structure
 * @connector: the drm connector structure
 * @pixel_clk: pixel clock handle
 * @reset: reset gpio handle
 * @ddc: i2c ddc handle
 * @hdmi_mutex: mutex to lock hdmi structure
 * @dpms: current dpms state
 * @xvidc_colorfmt: hdmi ip internal colorformat representation
 * @xvidc_colordepth: color depth
 * @tmds_clk: TMDS clock
 * @saved_adjusted_mode: Copy of @drm_crtc_state.adjusted_mode
 */
struct xlnx_hdmi {
	struct device *dev;
	void __iomem *base;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct clk *pixel_clk;
	struct gpio_desc *reset;
	struct i2c_adapter *ddc;
	/* to lock hdmi */
	struct mutex hdmi_mutex;
	int dpms;
	enum color_formats xvidc_colorfmt;
	enum color_depths xvidc_colordepth;
	u64 tmds_clk;
	struct drm_display_mode saved_adjusted_mode;
};

/* video timing controller */
#define xlnx_hdmi_vtc_enable(hdmi) \
	xlnx_hdmi_vtc_writel(hdmi, HDMI_TX_VTC_CTL, HDMI_TX_VTC_CTL_GE)

#define xlnx_hdmi_vtc_disable(hdmi) \
	xlnx_hdmi_vtc_clr(hdmi, HDMI_TX_VTC_CTL, HDMI_TX_VTC_CTL_GE)

static inline void
xlnx_hdmi_vtc_writel(struct xlnx_hdmi *hdmi, u32 offset, u32 val)
{
	writel(val, hdmi->base + HDMI_TX_VTC_BASE + offset);
}

static inline u32
xlnx_hdmi_vtc_readl(struct xlnx_hdmi *hdmi, u32 offset)
{
	return readl(hdmi->base + HDMI_TX_VTC_BASE + offset);
}

static inline void
xlnx_hdmi_vtc_clr(struct xlnx_hdmi *hdmi, u32 offset, u32 clr)
{
	xlnx_hdmi_vtc_writel(hdmi, offset,
			     xlnx_hdmi_vtc_readl(hdmi, offset) & ~clr);
}

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

/**
 * xlnx_hdmi_vtc_set_timing - configure video timing parameters
 * @hdmi: hdmi tx instance
 * @mode: drm display mode
 *
 * Program timing parameters into video timing controller
 * registers.
 *
 * @return: None
 */
static void xlnx_hdmi_vtc_set_timing(struct xlnx_hdmi *hdmi,
				     struct drm_display_mode *mode)
{
	u32 reg;
	u32 htotal, hactive, hsync_start, hbackporch_start;
	u32 vtotal, vactive, vsync_start, vbackporch_start;
	u32 hsync_len, hfront_porch, hback_porch;
	u32 vsync_len, vfront_porch, vback_porch;

	/* vtc reset */
	xlnx_hdmi_vtc_writel(hdmi, HDMI_TX_VTC_CTL, HDMI_TX_VTC_RST);
	reg = xlnx_hdmi_vtc_readl(hdmi, HDMI_TX_VTC_CTL);
	xlnx_hdmi_vtc_writel(hdmi, HDMI_TX_VTC_CTL, reg | HDMI_TX_VTC_CTL_RU);

	hactive = mode->hdisplay;
	hfront_porch = (mode->hsync_start - mode->hdisplay);
	hback_porch = (mode->htotal - mode->hsync_end);
	hsync_len = (mode->hsync_end - mode->hsync_start);
	if (hdmi->xvidc_colorfmt == HDMI_TX_CSF_YCRCB_420) {
		if (hactive & 0x1 || hfront_porch & 0x1 ||
		    hback_porch & 0x1 || hsync_len & 0x1)
			dev_dbg(hdmi->dev, "VTC does not support this timing\n");

		hactive = hactive / 2;
		hfront_porch = hfront_porch / 2;
		hback_porch = hback_porch / 2;
		hsync_len = hsync_len / 2;
	}
	htotal = hactive + hfront_porch + hsync_len + hback_porch;
	hsync_start = hactive + hfront_porch;
	hbackporch_start = hsync_start + hsync_len;

	vactive = mode->vdisplay;
	vfront_porch = mode->vsync_start - mode->vdisplay;
	vback_porch = mode->vtotal - mode->vsync_end;
	vsync_len = mode->vsync_end - mode->vsync_start;
	vtotal = vactive + vfront_porch + vsync_len + vback_porch;
	vsync_start = vactive + vfront_porch;
	vbackporch_start = vsync_start + vsync_len;

	xlnx_hdmi_vtc_writel(hdmi, HDMI_TX_VTC_CTL, reg & ~HDMI_TX_VTC_CTL_RU);

	reg = htotal & HDMI_TX_VTC_GHSIZE_FRAME_HSIZE;
	xlnx_hdmi_vtc_writel(hdmi, HDMI_TX_VTC_GHSIZE, reg);

	reg = vtotal & HDMI_TX_VTC_GVSIZE_FRAME_VSIZE;
	reg |= reg << HDMI_TX_VTC_FIELD1_VSIZE_SHIFT;
	xlnx_hdmi_vtc_writel(hdmi, HDMI_TX_VTC_GVSIZE, reg);

	reg = hactive & HDMI_TX_VTC_ACTIVE_SIZE_MASK;
	reg |= (vactive & HDMI_TX_VTC_ACTIVE_SIZE_MASK) <<
		HDMI_TX_VTC_FIELD1_VSIZE_SHIFT;
	xlnx_hdmi_vtc_writel(hdmi, HDMI_TX_VTC_GASIZE_F0, reg);

	reg = hsync_start & HDMI_TX_VTC_GHSYNC_START_MASK;
	reg |= (hbackporch_start << HDMI_TX_VTC_GH1BPSTART_SHIFT) &
		HDMI_TX_VTC_GHSYNC_END_MASK;
	xlnx_hdmi_vtc_writel(hdmi, HDMI_TX_VTC_GHSYNC, reg);

	reg = (vsync_start - 1) & HDMI_TX_VTC_F0_VSYNC_VSTART_MASK;
	reg |= ((vbackporch_start - 1) << HDMI_TX_VTC_FIELD1_VSIZE_SHIFT) &
		HDMI_TX_VTC_F0_VSYNC_VEND_MASK;
	xlnx_hdmi_vtc_writel(hdmi, HDMI_TX_VTC_GVSYNC, reg);
	xlnx_hdmi_vtc_clr(hdmi, HDMI_TX_VTC_GFENC,
		      HDMI_TX_VTC_GFENC_MASK);

	/* Calculate and uppdate Generator VBlank Hori field 0 */
	reg = hactive & HDMI_TX_VTC_F0VBLANK_HSTART_MASK;
	reg |= (hactive << HDMI_TX_VTC_F0VSYNC_HEND_SHIFT) &
		HDMI_TX_VTC_F0VBLANK_HEND_MASK;
	xlnx_hdmi_vtc_writel(hdmi, HDMI_TX_VTC_GVBHOFF, reg);

	/* Calculate and update Generator VSync Hori field 0 */
	reg = hsync_start & HDMI_TX_VTC_F0VBLANK_HSTART_MASK;
	reg |= (hsync_start << HDMI_TX_VTC_F0VSYNC_HEND_SHIFT) &
		HDMI_TX_VTC_F0VBLANK_HEND_MASK;
	xlnx_hdmi_vtc_writel(hdmi, HDMI_TX_VTC_GVSHOFF, reg);

	/* sets all polarities as active high */
	xlnx_hdmi_vtc_writel(hdmi, HDMI_TX_VTC_GPOL, HDMI_TX_VTC_GPOL_MASK);
	/* configure timing source */
	xlnx_hdmi_vtc_writel(hdmi, HDMI_TX_VTC_CTL, HDMI_TX_VTC_CTL_MASK |
			     HDMI_TX_VTC_CTL_RU);
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
	int count = 0;

	drm_edid = xlnx_hdmi_edid_read(hdmi, connector);

	drm_edid_connector_update(connector, drm_edid);

	if (drm_edid) {
		count = drm_edid_connector_add_modes(connector);
		drm_edid_free(drm_edid);
	}

	dev_dbg(hdmi->dev, "xlnx_hdmi_connector_get_modes %d\n", count);

	return count;
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
	struct xlnx_hdmi *hdmi = encoder_to_hdmi(encoder);

	xlnx_hdmi_encoder_dpms(encoder, DRM_MODE_DPMS_ON);
	xlnx_hdmi_vtc_enable(hdmi);
}

static void xlnx_hdmi_encoder_disable(struct drm_encoder *encoder)
{
	struct xlnx_hdmi *hdmi = encoder_to_hdmi(encoder);

	xlnx_hdmi_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);
	xlnx_hdmi_vtc_disable(hdmi);

	/* Assert HDMI TXCore reset */
	gpiod_set_value(hdmi->reset, 1);
}

/**
 * xlnx_hdmi_find_media_bus - finds drm_fourcc equivalent format
 * @hdmi: pointer to HDMI TX core instance
 * @drm_fourcc: drm fourcc code
 *
 */
static void
xlnx_hdmi_find_media_bus(struct xlnx_hdmi *hdmi,
				       u32 drm_fourcc)
{
	switch (drm_fourcc) {
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_BGR888:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
	case MEDIA_BUS_FMT_RGB888_1X24:
		hdmi->xvidc_colordepth = HDMI_TX_BPC_8;
		hdmi->xvidc_colorfmt = HDMI_TX_CSF_RGB;
		break;
	case DRM_FORMAT_XBGR2101010:
		hdmi->xvidc_colordepth = HDMI_TX_BPC_10;
		hdmi->xvidc_colorfmt = HDMI_TX_CSF_RGB;
		break;
	case DRM_FORMAT_VUY888:
	case DRM_FORMAT_XVUY8888:
	case DRM_FORMAT_Y8:
	case DRM_FORMAT_YUV444:
	case MEDIA_BUS_FMT_VUY8_1X24:
		hdmi->xvidc_colordepth = HDMI_TX_BPC_8;
		hdmi->xvidc_colorfmt = HDMI_TX_CSF_YCRCB_444;
		break;
	case DRM_FORMAT_XVUY2101010:
	case DRM_FORMAT_Y10:
	case DRM_FORMAT_X403:
	case MEDIA_BUS_FMT_VUY10_1X30:
		hdmi->xvidc_colordepth = HDMI_TX_BPC_10;
		hdmi->xvidc_colorfmt = HDMI_TX_CSF_YCRCB_444;
		break;
	/* TODO: Fix using DRM and media formats in same switch case */
	case DRM_FORMAT_X423:
	case MEDIA_BUS_FMT_VUY12_1X36:
		hdmi->xvidc_colordepth = HDMI_TX_BPC_12;
		hdmi->xvidc_colorfmt = HDMI_TX_CSF_YCRCB_444;
		break;
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_NV16:
	case MEDIA_BUS_FMT_UYVY8_1X16:
		hdmi->xvidc_colordepth = HDMI_TX_BPC_8;
		hdmi->xvidc_colorfmt = HDMI_TX_CSF_YCRCB_422;
		break;
	case DRM_FORMAT_XV20:
		hdmi->xvidc_colordepth = HDMI_TX_BPC_10;
		hdmi->xvidc_colorfmt = HDMI_TX_CSF_YCRCB_422;
		break;
	case DRM_FORMAT_NV12:
	case MEDIA_BUS_FMT_VYYUYY8_1X24:
		hdmi->xvidc_colordepth = HDMI_TX_BPC_8;
		hdmi->xvidc_colorfmt = HDMI_TX_CSF_YCRCB_420;
		break;
	case DRM_FORMAT_XV15:
		hdmi->xvidc_colordepth = HDMI_TX_BPC_10;
		hdmi->xvidc_colorfmt = HDMI_TX_CSF_YCRCB_420;
		break;
	default:
		dev_err(hdmi->dev, "Unknown drm fmt: %d\n", drm_fourcc);
		hdmi->xvidc_colordepth = HDMI_TX_BPC_8;
		hdmi->xvidc_colorfmt = HDMI_TX_CSF_RGB;
	}
}

static u64 xlnx_hdmi_get_tmdsclk(struct xlnx_hdmi *hdmi, struct drm_display_mode *adjusted_mode)
{
	u64 tmdsclk;

	tmdsclk = adjusted_mode->clock * 1000;
	if (hdmi->xvidc_colorfmt == HDMI_TX_CSF_YCRCB_420)
		tmdsclk = tmdsclk >> 1;

	if (hdmi->xvidc_colorfmt != HDMI_TX_CSF_YCRCB_422) {
		switch (hdmi->xvidc_colordepth) {
		case HDMI_TX_BPC_10:
			tmdsclk = (tmdsclk * 5) >> 2;
			break;
		case HDMI_TX_BPC_12:
			tmdsclk = (tmdsclk * 3) >> 1;
			break;
		case HDMI_TX_BPC_16:
			tmdsclk = tmdsclk << 1;
			break;
		default:
			break;
		}
	}

	return tmdsclk;
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
	struct xlnx_hdmi *hdmi = encoder_to_hdmi(encoder);
	struct drm_display_mode *mode = &crtc_state->mode;
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	int ret;
	u32 drm_fourcc;

	drm_mode_copy(&hdmi->saved_adjusted_mode, &crtc_state->adjusted_mode);
	dev_dbg(hdmi->dev, "mode->clock = %d\n", mode->clock * 1000);
	dev_dbg(hdmi->dev, "mode->crtc_clock = %d\n", mode->crtc_clock * 1000);
	dev_dbg(hdmi->dev, "mode->pvsync = %d\n",
		!!(mode->flags & DRM_MODE_FLAG_PVSYNC));
	dev_dbg(hdmi->dev, "mode->phsync = %d\n",
		!!(mode->flags & DRM_MODE_FLAG_PHSYNC));
	dev_dbg(hdmi->dev, "mode->hsync_end = %d\n", mode->hsync_end);
	dev_dbg(hdmi->dev, "mode->hsync_start = %d\n", mode->hsync_start);
	dev_dbg(hdmi->dev, "mode->vsync_end = %d\n", mode->vsync_end);
	dev_dbg(hdmi->dev, "mode->vsync_start = %d\n", mode->vsync_start);
	dev_dbg(hdmi->dev, "mode->hdisplay = %d\n", mode->hdisplay);
	dev_dbg(hdmi->dev, "mode->vdisplay = %d\n", mode->vdisplay);
	dev_dbg(hdmi->dev, "mode->htotal = %d\n", mode->htotal);
	dev_dbg(hdmi->dev, "mode->vtotal = %d\n", mode->vtotal);
	dev_dbg(hdmi->dev, "mode->vrefresh = %d\n", drm_mode_vrefresh(mode));
	dev_dbg(hdmi->dev, "mode->flags = %d interlace = %d\n", mode->flags,
		!!(mode->flags & DRM_MODE_FLAG_INTERLACE));

	drm_fourcc = encoder->crtc->primary->state->fb->format->format;

	xlnx_hdmi_find_media_bus(hdmi, drm_fourcc);
	dev_dbg(hdmi->dev, "xvidc_colorfmt = %d\n", hdmi->xvidc_colorfmt);
	dev_dbg(hdmi->dev, "xvidc_colordepth = %d\n", hdmi->xvidc_colordepth);

	hdmi->tmds_clk = xlnx_hdmi_get_tmdsclk(hdmi, adjusted_mode);
	dev_dbg(hdmi->dev, "tmds_clk = %llu\n", hdmi->tmds_clk);

	/* Deassert HDMI TXCore reset */
	gpiod_set_value(hdmi->reset, 0);

	ret = clk_set_rate(hdmi->pixel_clk, adjusted_mode->clock * 1000);
	if (ret < 0)
		dev_err(hdmi->dev, "failed to set pixel clock rate: %d\n", ret);

	xlnx_hdmi_vtc_set_timing(hdmi, adjusted_mode);
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
	int ret;

	hdmi->pixel_clk = devm_clk_get(hdmi->dev, "pixel_clk");
	if (IS_ERR(hdmi->pixel_clk)) {
		ret = PTR_ERR(hdmi->pixel_clk);
		dev_err(hdmi->dev, "failed to get pixel_clk %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(hdmi->pixel_clk);
	if (ret) {
		dev_err(hdmi->dev, "unable to enable pixel_clk %d\n", ret);
		return ret;
	}

	hdmi->reset = devm_gpiod_get(hdmi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(hdmi->reset)) {
		ret = PTR_ERR(hdmi->reset);
		dev_err(hdmi->dev, "Failed to get reset line %d\n", ret);
		return ret;
	}

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
	{ .compatible = "xlnx,v-hdmi-tx-min-1.0" },
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
