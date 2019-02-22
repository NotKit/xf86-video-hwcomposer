#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86.h>
#include "xf86Crtc.h"

#include "driver.h"
#include "pixmap.h"

#define DRIHYBRIS
#ifdef DRIHYBRIS
#include "drihybris.h"
#include <hybris/eglplatformcommon/hybris_nativebufferext.h>
#endif

static PixmapPtr
hwc_drihybris_pixmap_from_hybris_buffer(ScreenPtr screen,
                    CARD16 width,
                    CARD16 height,
                    CARD16 stride, CARD8 depth, CARD8 bpp,
                    int numInts, int *ints,
                    int numFds, int *fds)
{
    PixmapPtr pixmap;
    Bool ret;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    HWCPtr hwc = HWCPTR(scrn);

    pixmap = screen->CreatePixmap(screen, 0, 0, depth,
                                HWC_CREATE_PIXMAP_DRIHYBRIS);
    if (!pixmap)
        return NULL;

    if (!screen->ModifyPixmapHeader(pixmap, width, height, 0, 0, stride * bpp / 8,
                                    NULL))
        goto free_pixmap;
    
    EGLClientBuffer buf;
    hwc->renderer.eglHybrisCreateRemoteBuffer(width, height, HYBRIS_USAGE_HW_RENDER | HYBRIS_USAGE_HW_TEXTURE | HYBRIS_USAGE_SW_READ_OFTEN,
                                            HYBRIS_PIXEL_FORMAT_RGBA_8888, stride,
                                            numInts, ints, numFds, fds, &buf);

    if (hwc_set_pixmap_buf(pixmap, buf))
        return pixmap;

free_pixmap:
    screen->DestroyPixmap(pixmap);
    return NULL;
}

static drihybris_screen_info_rec hwc_drihybris_screen_info = {
    .version = 1,
    .pixmap_from_buffer = hwc_drihybris_pixmap_from_hybris_buffer,
    //.buffer_from_pixmap = hwc_drihybris_buffer_from_pixmap,
};

Bool
hwc_drihybris_screen_init(ScreenPtr screen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    HWCPtr hwc = HWCPTR(scrn);

    if (!drihybris_screen_init(screen, &hwc_drihybris_screen_info)) {
        xf86DrvMsg(scrn->scrnIndex, X_WARNING,
            "drihybris_screen_init failed\n");
        return FALSE;
    }

    return TRUE;
}
