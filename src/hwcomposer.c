#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "xf86.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include <malloc.h>

#include <android-config.h>
#include <sync/sync.h>
#include <hybris/hwcomposerwindow/hwcomposer.h>

#include "driver.h"

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

void hwc_set_power_mode(ScrnInfoPtr pScrn, int disp, int mode)
{
	HWCPtr hwc = HWCPTR(pScrn);
    
	hwc_composer_device_1_t *hwcDevicePtr = hwc->hwcDevicePtr;
	hw_device_t *hwcDevice = &hwcDevicePtr->common;

	uint32_t hwc_version = hwc->hwcVersion = interpreted_version(hwcDevice);

#ifdef HWC_DEVICE_API_VERSION_1_4
	if (hwc_version == HWC_DEVICE_API_VERSION_1_4) {
		hwcDevicePtr->setPowerMode(hwcDevicePtr, disp, (mode) ? HWC_POWER_MODE_NORMAL : HWC_POWER_MODE_OFF);
	} else
#endif
#ifdef HWC_DEVICE_API_VERSION_1_5
	if (hwc_version == HWC_DEVICE_API_VERSION_1_5) {
		hwcDevicePtr->setPowerMode(hwcDevicePtr, disp, (mode) ? HWC_POWER_MODE_NORMAL : HWC_POWER_MODE_OFF);
	} else
#endif
		hwcDevicePtr->blank(hwcDevicePtr, disp, (mode) ? 0 : 1);
}

Bool hwc_hwcomposer_init(ScrnInfoPtr pScrn)
{
	HWCPtr hwc = HWCPTR(pScrn);
	int err;

	hw_module_t const* module = NULL;
	err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module);
	assert(err == 0);

	hwc->gralloc = (gralloc_module_t*) module;
	err = gralloc_open((const hw_module_t *) hwc->gralloc, &hwc->alloc);

	framebuffer_device_t* fbDev = NULL;
	framebuffer_open(module, &fbDev);

	hw_module_t *hwcModule = 0;

	err = hw_get_module(HWC_HARDWARE_MODULE_ID, (const hw_module_t **) &hwcModule);
	assert(err == 0);

	hwc_composer_device_1_t *hwcDevicePtr = 0;
	err = hwc_open_1(hwcModule, &hwcDevicePtr);
	assert(err == 0);

	hwc->hwcDevicePtr = hwcDevicePtr;
	hw_device_t *hwcDevice = &hwcDevicePtr->common;

	hwc_set_power_mode(pScrn, HWC_DISPLAY_PRIMARY, 1);	uint32_t hwc_version = hwc->hwcVersion = interpreted_version(hwcDevice);

	uint32_t configs[5];
	size_t numConfigs = 5;

	err = hwcDevicePtr->getDisplayConfigs(hwcDevicePtr, HWC_DISPLAY_PRIMARY, configs, &numConfigs);
	assert (err == 0);

	int32_t attr_values[2];
	uint32_t attributes[] = { HWC_DISPLAY_WIDTH, HWC_DISPLAY_HEIGHT, HWC_DISPLAY_NO_ATTRIBUTE };

	hwcDevicePtr->getDisplayAttributes(hwcDevicePtr, HWC_DISPLAY_PRIMARY,
			configs[0], attributes, attr_values);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "width: %i height: %i\n", attr_values[0], attr_values[1]);
	hwc->hwcWidth = attr_values[0];
	hwc->hwcHeight = attr_values[1];

	size_t size = sizeof(hwc_display_contents_1_t) + 2 * sizeof(hwc_layer_1_t);
	hwc_display_contents_1_t *list = (hwc_display_contents_1_t *) malloc(size);
	hwc->hwcContents = (hwc_display_contents_1_t **) malloc(HWC_NUM_DISPLAY_TYPES * sizeof(hwc_display_contents_1_t *));
	const hwc_rect_t r = { 0, 0, attr_values[0], attr_values[1] };

	int counter = 0;
	for (; counter < HWC_NUM_DISPLAY_TYPES; counter++)
		hwc->hwcContents[counter] = NULL;
	// Assign the layer list only to the first display,
	// otherwise HWC might freeze if others are disconnected
	hwc->hwcContents[0] = list;

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

	hwc->fblayer = layer = &list->hwLayers[1];
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

	return TRUE;
}

void hwc_hwcomposer_close(ScrnInfoPtr pScrn)
{
}

static void present(void *user_data, struct ANativeWindow *window,
								struct ANativeWindowBuffer *buffer)
{
	ScrnInfoPtr pScrn = (ScrnInfoPtr)user_data;
	HWCPtr hwc = HWCPTR(pScrn);

	hwc_display_contents_1_t **contents = hwc->hwcContents;
	hwc_layer_1_t *fblayer = hwc->fblayer;
	hwc_composer_device_1_t *hwcdevice = hwc->hwcDevicePtr;

	int oldretire = contents[0]->retireFenceFd;
	contents[0]->retireFenceFd = -1;

	fblayer->handle = buffer->handle;
	fblayer->acquireFenceFd = HWCNativeBufferGetFence(buffer);
	fblayer->releaseFenceFd = -1;
	int err = hwcdevice->prepare(hwcdevice, HWC_NUM_DISPLAY_TYPES, contents);
	assert(err == 0);

	err = hwcdevice->set(hwcdevice, HWC_NUM_DISPLAY_TYPES, contents);
	/* in Android, SurfaceFlinger ignores the return value as not all
		display types may be supported */
	HWCNativeBufferSetFence(buffer, fblayer->releaseFenceFd);

	if (oldretire != -1)
	{
		sync_wait(oldretire, -1);
		close(oldretire);
	}
}

struct ANativeWindow *hwc_get_native_window(ScrnInfoPtr pScrn) {
	HWCPtr hwc = HWCPTR(pScrn);
	struct ANativeWindow *win = HWCNativeWindowCreate(hwc->hwcWidth, hwc->hwcHeight, HAL_PIXEL_FORMAT_RGBA_8888, present, pScrn);
	return win;
}

void hwc_toggle_screen_brightness(ScrnInfoPtr pScrn)
{
	HWCPtr hwc = HWCPTR(pScrn);
	struct light_state_t state;
	int brightness;

	if (!hwc->lightsDevice) {
		return;
	}
	brightness = (hwc->dpmsMode == DPMSModeOn) ?
							hwc->screenBrightness : 0;

	state.flashMode = LIGHT_FLASH_NONE;
	state.brightnessMode = BRIGHTNESS_MODE_USER;

	state.color = (int)((0xffU << 24) | (brightness << 16) |
						(brightness << 8) | brightness);
	hwc->lightsDevice->set_light(hwc->lightsDevice, &state);
}
