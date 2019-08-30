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
#include <dlfcn.h>

#include <android-config.h>
#include <sync/sync.h>
#include <hybris/hwcomposerwindow/hwcomposer.h>
#include <hybris/hwc2/hwc2_compatibility_layer.h>

#include "driver.h"

typedef struct
{
    struct HWC2EventListener listener;
    ScrnInfoPtr pScrn;
} HwcProcs_v20;

void hwc2_callback_vsync(HWC2EventListener* listener, int32_t sequenceId,
                         hwc2_display_t display, int64_t timestamp)
{
}

void hwc2_callback_hotplug(HWC2EventListener* listener, int32_t sequenceId,
                           hwc2_display_t display, bool connected,
                           bool primaryDisplay)
{
    ScrnInfoPtr pScrn = ((HwcProcs_v20*) listener)->pScrn;
	HWCPtr hwc = HWCPTR(pScrn);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "onHotplugReceived(%d, %" PRIu64 ", %s, %s)",
           sequenceId, display,
           connected ? "connected" : "disconnected",
           primaryDisplay ? "primary\n" : "external\n");

    hwc2_compat_device_on_hotplug(hwc->hwc2Device, display, connected);
}

void hwc2_callback_refresh(HWC2EventListener* listener, int32_t sequenceId,
                           hwc2_display_t display)
{
}

Bool hwc_hwcomposer2_init(ScrnInfoPtr pScrn)
{
	HWCPtr hwc = HWCPTR(pScrn);
	int err;
    static int composerSequenceId = 0;
    
    HwcProcs_v20* procs = malloc(sizeof(HwcProcs_v20));
    procs->listener.on_vsync_received = hwc2_callback_vsync;
    procs->listener.on_hotplug_received = hwc2_callback_hotplug;
    procs->listener.on_refresh_received = hwc2_callback_refresh;
    procs->pScrn = pScrn;

    hwc2_compat_device_t* hwc2_device = hwc->hwc2Device = hwc2_compat_device_new(false);
    assert(hwc2_device);

	//hwc_set_power_mode(pScrn, HWC_DISPLAY_PRIMARY, 1);

	hwc2_compat_device_register_callback(hwc2_device, &procs->listener,
        composerSequenceId++);

    for (int i = 0; i < 5 * 1000; ++i) {
        // Wait at most 5s for hotplug events
        if ((hwc->hwc2_primary_display =
            hwc2_compat_device_get_display_by_id(hwc2_device, 0)))
            break;
        usleep(1000);
    }
	assert(hwc->hwc2_primary_display);

	HWC2DisplayConfig *config = hwc2_compat_display_get_active_config(hwc->hwc2_primary_display);
	assert(config);

	hwc->hwcWidth = config->width;
	hwc->hwcHeight = config->height;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "width: %i height: %i\n", config->width, config->height);

	hwc2_compat_layer_t* layer = hwc->hwc2_primary_layer =
        hwc2_compat_display_create_layer(hwc->hwc2_primary_display);

    hwc2_compat_layer_set_composition_type(layer, HWC2_COMPOSITION_CLIENT);
    hwc2_compat_layer_set_blend_mode(layer, HWC2_BLEND_MODE_NONE);
    hwc2_compat_layer_set_source_crop(layer, 0.0f, 0.0f, hwc->hwcWidth, hwc->hwcHeight);
    hwc2_compat_layer_set_display_frame(layer, 0, 0, hwc->hwcWidth, hwc->hwcHeight);
    hwc2_compat_layer_set_visible_region(layer, 0, 0, hwc->hwcWidth, hwc->hwcHeight);

	return TRUE;
}

void hwc_hwcomposer2_close(ScrnInfoPtr pScrn)
{
}

void hwc_present_hwcomposer2(void *user_data, struct ANativeWindow *window,
								struct ANativeWindowBuffer *buffer)
{
	ScrnInfoPtr pScrn = (ScrnInfoPtr)user_data;
	HWCPtr hwc = HWCPTR(pScrn);
	static int lastPresentFence = -1;

	uint32_t numTypes = 0;
    uint32_t numRequests = 0;
    int displayId = 0;
    hwc2_error_t error = HWC2_ERROR_NONE;

    int acquireFenceFd = HWCNativeBufferGetFence(buffer);
	int syncBeforeSet = 0;

    if (syncBeforeSet && acquireFenceFd >= 0) {
        sync_wait(acquireFenceFd, -1);
        close(acquireFenceFd);
        acquireFenceFd = -1;
    }

	hwc2_compat_display_t* hwcDisplay = hwc->hwc2_primary_display;

    error = hwc2_compat_display_validate(hwcDisplay, &numTypes,
                                                    &numRequests);
    if (error != HWC2_ERROR_NONE && error != HWC2_ERROR_HAS_CHANGES) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "prepare: validate failed for display %d: %d", displayId, error);
        return;
    }

    if (numTypes || numRequests) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "prepare: validate required changes for display %d: %d",
               displayId, error);
        return;
    }

    error = hwc2_compat_display_accept_changes(hwcDisplay);
    if (error != HWC2_ERROR_NONE) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "prepare: acceptChanges failed: %d", error);
        return;
    }

    hwc2_compat_display_set_client_target(hwcDisplay, /* slot */0, buffer,
                                          acquireFenceFd,
                                          HAL_DATASPACE_UNKNOWN);

    int presentFence = -1;
    hwc2_compat_display_present(hwcDisplay, &presentFence);

    if (lastPresentFence != -1) {
        sync_wait(lastPresentFence, -1);
        close(lastPresentFence);
    }

    lastPresentFence = presentFence != -1 ? dup(presentFence) : -1;

    HWCNativeBufferSetFence(buffer, presentFence);
}
