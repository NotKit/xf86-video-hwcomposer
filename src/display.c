#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86.h>
#include "xf86Crtc.h"

#include "driver.h"

Bool hwc_lights_init(ScrnInfoPtr pScrn)
{
	HWCPtr hwc = HWCPTR(pScrn);
	hwc->lightsDevice = NULL;
	hw_module_t *lightsModule = NULL;
	struct light_device_t *lightsDevice = NULL;

	/* Use 255 as default */
	hwc->screenBrightness = 255;

	if (hw_get_module(LIGHTS_HARDWARE_MODULE_ID, (const hw_module_t **)&lightsModule) != 0) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to get lights module\n");
		return FALSE;
	}

	if (lightsModule->methods->open(lightsModule, LIGHT_ID_BACKLIGHT, (hw_device_t **)&lightsDevice) != 0) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to create lights device\n");
		return FALSE;
	}

	hwc->lightsDevice = lightsDevice;
	return TRUE;
}

static Bool
hwc_xf86crtc_resize(ScrnInfoPtr pScrn, int width, int height)
{
    ScreenPtr pScreen = pScrn->pScreen;

    if (pScrn->virtualX == width && pScrn->virtualY == height)
        return TRUE;

    /* We don't support real resizing yet */
    return FALSE;
}

static const xf86CrtcConfigFuncsRec hwc_xf86crtc_config_funcs = {
    hwc_xf86crtc_resize
};

static void hwcomposer_crtc_dpms(xf86CrtcPtr crtc, int mode)
{
}

static Bool hwcomposer_set_mode_major(xf86CrtcPtr crtc, DisplayModePtr mode, Rotation rotation, int x, int y)
{
    crtc->mode = *mode;
    crtc->x = x;
    crtc->y = y;
    crtc->rotation = rotation;

    return TRUE;
}

static void
hwc_set_cursor_colors(xf86CrtcPtr crtc, int bg, int fg)
{

}

static void
hwc_set_cursor_position(xf86CrtcPtr crtc, int x, int y)
{
    HWCPtr hwc = HWCPTR(crtc->scrn);
    hwc->cursorX = x;
    hwc->cursorY = y;
    hwc_trigger_redraw(crtc->scrn);
}

/*
 * The load_cursor_argb_check driver hook.
 *
 * Sets the hardware cursor by uploading it to texture.
 * On failure, returns FALSE indicating that the X server should fall
 * back to software cursors.
 */
static Bool
hwc_load_cursor_argb_check(xf86CrtcPtr crtc, CARD32 *image)
{
    HWCPtr hwc = HWCPTR(crtc->scrn);

    glBindTexture(GL_TEXTURE_2D, hwc->renderer.cursorTexture);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, hwc->cursorWidth, hwc->cursorHeight,
                    0, GL_RGBA, GL_UNSIGNED_BYTE, image);

    hwc_trigger_redraw(crtc->scrn);
    return TRUE;
}

static void
hwc_hide_cursor(xf86CrtcPtr crtc)
{
    HWCPtr hwc = HWCPTR(crtc->scrn);
    hwc->cursorShown = FALSE;
    hwc_trigger_redraw(crtc->scrn);
}

static void
hwc_show_cursor(xf86CrtcPtr crtc)
{
    HWCPtr hwc = HWCPTR(crtc->scrn);
    hwc->cursorShown = TRUE;
    hwc_trigger_redraw(crtc->scrn);
}

static const xf86CrtcFuncsRec hwcomposer_crtc_funcs = {
    .dpms = hwcomposer_crtc_dpms,
    .set_mode_major = hwcomposer_set_mode_major,
    .set_cursor_colors = hwc_set_cursor_colors,
    .set_cursor_position = hwc_set_cursor_position,
    .show_cursor = hwc_show_cursor,
    .hide_cursor = hwc_hide_cursor,
    .load_cursor_argb_check = hwc_load_cursor_argb_check
};

static void
hwc_output_dpms(xf86OutputPtr output, int mode)
{
    ScrnInfoPtr pScrn;
    pScrn = output->scrn;
    HWCPtr hwc = HWCPTR(pScrn);

    hwc->dpmsMode = mode;
    hwc_toggle_screen_brightness(pScrn);

    if (mode != DPMSModeOn)
    {
        // Wait for the renderer thread to finish to avoid causing locks
        pthread_mutex_lock(&(hwc->dirtyLock));
        hwc->dirty = FALSE;
        pthread_mutex_unlock(&(hwc->dirtyLock));
    }

    pthread_mutex_lock(&(hwc->rendererLock));
    hwc_set_power_mode(pScrn, HWC_DISPLAY_PRIMARY, (mode == DPMSModeOn) ? 1 : 0);
    pthread_mutex_unlock(&(hwc->rendererLock));

    if (mode == DPMSModeOn)
        // Force redraw after unblank
        hwc_trigger_redraw(pScrn);
}

static xf86OutputStatus
hwc_output_detect(xf86OutputPtr output)
{
    return XF86OutputStatusConnected;
}

static int
hwc_output_mode_valid(xf86OutputPtr output, DisplayModePtr pMode)
{
    return MODE_OK;
}

static DisplayModePtr
hwc_output_get_modes(xf86OutputPtr output)
{
    ScrnInfoPtr pScrn;
    pScrn = output->scrn;
    HWCPtr hwc = HWCPTR(pScrn);

    return xf86DuplicateModes(NULL, hwc->modes);
}

static const xf86OutputFuncsRec hwc_output_funcs = {
    .dpms = hwc_output_dpms,
    .detect = hwc_output_detect,
    .mode_valid = hwc_output_mode_valid,
    .get_modes = hwc_output_get_modes
};

void
hwc_trigger_redraw(ScrnInfoPtr pScrn)
{
    HWCPtr hwc = HWCPTR(pScrn);

    pthread_mutex_lock(&(hwc->dirtyLock));
    hwc->dirty = TRUE;
    pthread_cond_signal(&(hwc->dirtyCond));
    pthread_mutex_unlock(&(hwc->dirtyLock));
}

Bool
hwc_display_pre_init(ScrnInfoPtr pScrn)
{
    HWCPtr hwc = HWCPTR(pScrn);
    xf86OutputPtr output;
    xf86CrtcPtr crtc;

    /* Pick up size from the "Display" subsection if it exists */
    if (pScrn->display->virtualX) {
        pScrn->virtualX = pScrn->display->virtualX;
        pScrn->virtualY = pScrn->display->virtualY;
    } else {
        /* Pick rotated HWComposer screen resolution */
        pScrn->virtualX = hwc->hwcHeight;
        pScrn->virtualY = hwc->hwcWidth;
     }
    pScrn->displayWidth = pScrn->virtualX;

    /* Construct a mode with the screen's initial dimensions */
    hwc->modes = xf86CVTMode(pScrn->virtualX, pScrn->virtualY, 60, 0, 0);

    xf86CrtcConfigInit(pScrn, &hwc_xf86crtc_config_funcs);
    xf86CrtcSetSizeRange(pScrn, 8, 8, SHRT_MAX, SHRT_MAX);

    output = xf86OutputCreate(pScrn, &hwc_output_funcs, "hwcomposer");
    output->possible_crtcs = 0x7f;

    crtc = xf86CrtcCreate(pScrn, &hwcomposer_crtc_funcs);

    xf86ProviderSetup(pScrn, NULL, "hwcomposer");

    xf86InitialConfiguration(pScrn, TRUE);

    pScrn->currentMode = pScrn->modes;
    crtc->funcs->set_mode_major(crtc, pScrn->currentMode, RR_Rotate_0, 0, 0);

    return TRUE;
}
