#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "xf86.h"

#include <android-config.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <malloc.h>
#include <sync/sync.h>

#include "hwcomposer.h"

static gralloc_module_t *gralloc = 0;
static alloc_device_t *alloc = 0;

inline static uint32_t interpreted_version(hw_device_t *hwc_device)
{
	uint32_t version = hwc_device->version;

	if ((version & 0xffff0000) == 0) {
		// Assume header version is always 1
		uint32_t header_version = 1;

		// Legacy version encoding
		version = (version << 16) | header_version;
	}
	return version;
}

HWComposer *HWComposer_Init(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	HWComposer *self;
	int err;

	if (!(self = calloc(1, sizeof(HWComposer)))) {
		xf86DrvMsg(pScreen->myNum, X_INFO, "HWComposer_Init: calloc failed\n");
		return NULL;
	}

	hw_module_t const* module = NULL;
	err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module);
	assert(err == 0);

	gralloc = (gralloc_module_t*) module;
	err = gralloc_open((const hw_module_t *) gralloc, &alloc);

	framebuffer_device_t* fbDev = NULL;
	framebuffer_open(module, &fbDev);

	hw_module_t *hwcModule = 0;

	err = hw_get_module(HWC_HARDWARE_MODULE_ID, (const hw_module_t **) &hwcModule);
	assert(err == 0);

	hwc_composer_device_1_t *hwcDevicePtr = 0;
	err = hwc_open_1(hwcModule, &hwcDevicePtr);
	assert(err == 0);

	self->hwcDevicePtr = hwcDevicePtr;
	hw_device_t *hwcDevice = &hwcDevicePtr->common;

	uint32_t hwc_version = interpreted_version(hwcDevice);

#ifdef HWC_DEVICE_API_VERSION_1_4
	if (hwc_version == HWC_DEVICE_API_VERSION_1_4) {
		hwcDevicePtr->setPowerMode(hwcDevicePtr, 0, HWC_POWER_MODE_NORMAL);
	} else
#endif
#ifdef HWC_DEVICE_API_VERSION_1_5
	if (hwc_version == HWC_DEVICE_API_VERSION_1_5) {
		hwcDevicePtr->setPowerMode(hwcDevicePtr, 0, HWC_POWER_MODE_NORMAL);
	} else
#endif
		hwcDevicePtr->blank(hwcDevicePtr, 0, 0);

	uint32_t configs[5];
	size_t numConfigs = 5;

	err = hwcDevicePtr->getDisplayConfigs(hwcDevicePtr, 0, configs, &numConfigs);
	assert (err == 0);

	int32_t attr_values[2];
	uint32_t attributes[] = { HWC_DISPLAY_WIDTH, HWC_DISPLAY_HEIGHT, HWC_DISPLAY_NO_ATTRIBUTE };

	hwcDevicePtr->getDisplayAttributes(hwcDevicePtr, 0,
			configs[0], attributes, attr_values);

	printf("width: %i height: %i\n", attr_values[0], attr_values[1]);

	size_t size = sizeof(hwc_display_contents_1_t) + 2 * sizeof(hwc_layer_1_t);
	hwc_display_contents_1_t *list = (hwc_display_contents_1_t *) malloc(size);
	self->hwcContents = (hwc_display_contents_1_t **) malloc(HWC_NUM_DISPLAY_TYPES * sizeof(hwc_display_contents_1_t *));
	const hwc_rect_t r = { 0, 0, attr_values[0], attr_values[1] };

	int counter = 0;
	for (; counter < HWC_NUM_DISPLAY_TYPES; counter++)
		self->hwcContents[counter] = list;

	hwc_layer_1_t *layer = &list->hwLayers[0];
	memset(layer, 0, sizeof(hwc_layer_1_t));
	layer->compositionType = HWC_FRAMEBUFFER;
	layer->hints = 0;
	layer->flags = 0;
	layer->handle = 0;
	layer->transform = 0;
	layer->blending = HWC_BLENDING_NONE;
#ifdef HWC_DEVICE_API_VERSION_1_3
	layer->sourceCropf.top = 0.0f;
	layer->sourceCropf.left = 0.0f;
	layer->sourceCropf.bottom = (float) attr_values[1];
	layer->sourceCropf.right = (float) attr_values[0];
#else
	layer->sourceCrop = r;
#endif
	layer->displayFrame = r;
	layer->visibleRegionScreen.numRects = 1;
	layer->visibleRegionScreen.rects = &layer->displayFrame;
	layer->acquireFenceFd = -1;
	layer->releaseFenceFd = -1;
#if (ANDROID_VERSION_MAJOR >= 4) && (ANDROID_VERSION_MINOR >= 3) || (ANDROID_VERSION_MAJOR >= 5)
	// We've observed that qualcomm chipsets enters into compositionType == 6
	// (HWC_BLIT), an undocumented composition type which gives us rendering
	// glitches and warnings in logcat. By setting the planarAlpha to non-
	// opaque, we attempt to force the HWC into using HWC_FRAMEBUFFER for this
	// layer so the HWC_FRAMEBUFFER_TARGET layer actually gets used.
	int tryToForceGLES = getenv("QPA_HWC_FORCE_GLES") != NULL;
	layer->planeAlpha = tryToForceGLES ? 1 : 255;
#endif
#ifdef HWC_DEVICE_API_VERSION_1_5
	layer->surfaceDamage.numRects = 0;
#endif

	self->fblayer = layer = &list->hwLayers[1];
	memset(layer, 0, sizeof(hwc_layer_1_t));
	layer->compositionType = HWC_FRAMEBUFFER_TARGET;
	layer->hints = 0;
	layer->flags = 0;
	layer->handle = 0;
	layer->transform = 0;
	layer->blending = HWC_BLENDING_NONE;
#ifdef HWC_DEVICE_API_VERSION_1_3
	layer->sourceCropf.top = 0.0f;
	layer->sourceCropf.left = 0.0f;
	layer->sourceCropf.bottom = (float) attr_values[1];
	layer->sourceCropf.right = (float) attr_values[0];
#else
	layer->sourceCrop = r;
#endif
	layer->displayFrame = r;
	layer->visibleRegionScreen.numRects = 1;
	layer->visibleRegionScreen.rects = &layer->displayFrame;
	layer->acquireFenceFd = -1;
	layer->releaseFenceFd = -1;
#if (ANDROID_VERSION_MAJOR >= 4) && (ANDROID_VERSION_MINOR >= 3) || (ANDROID_VERSION_MAJOR >= 5)
	layer->planeAlpha = 0xff;
#endif
#ifdef HWC_DEVICE_API_VERSION_1_5
	layer->surfaceDamage.numRects = 0;
#endif

	list->retireFenceFd = -1;
	list->flags = HWC_GEOMETRY_CHANGED;
	list->numHwLayers = 2;

	return self;
}

void HWComposer_Close(ScreenPtr pScreen)
{
}
