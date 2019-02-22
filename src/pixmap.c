/* Copyright © 2014 Advanced Micro Devices, Inc.
*
* Permission is hereby granted, free of charge, to any person
* obtaining a copy of this software and associated documentation
* files (the "Software"), to deal in the Software without
* restriction, including without limitation the rights to use, copy,
* modify, merge, publish, distribute, sublicense, and/or sell copies
* of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice (including
* the next paragraph) shall be included in all copies or substantial
* portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
* HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86.h>
#include "fb.h"

#include "driver.h"
#include "pixmap.h"

DevPrivateKeyRec hwc_pixmap_index;

static PixmapPtr
hwc_pixmap_create(ScreenPtr screen, int w, int h, int depth, unsigned usage)
{
    ScrnInfoPtr scrn;
    struct hwc_pixmap *priv;
    PixmapPtr pixmap;

    /* only DRI2 pixmap is suppported */
    if (!(usage & HWC_CREATE_PIXMAP_DRIHYBRIS))
        return fbCreatePixmap(screen, w, h, depth, usage);

    if (w > 32767 || h > 32767)
        return NullPixmap;

    if (depth == 1)
        return fbCreatePixmap(screen, w, h, depth, usage);

    pixmap = fbCreatePixmap(screen, 0, 0, depth, usage);
    if (pixmap == NullPixmap)
        return pixmap;

    if (w && h) {
        goto fallback_pixmap;
    }

    return pixmap;

fallback_priv:
    free(priv);
fallback_pixmap:
    fbDestroyPixmap(pixmap);
    return fbCreatePixmap(screen, w, h, depth, usage);
}

static Bool hwc_pixmap_destroy(PixmapPtr pixmap)
{
    if (pixmap->refcnt == 1) {
        hwc_set_pixmap_buf(pixmap, NULL);
    }
    fbDestroyPixmap(pixmap);
    return TRUE;
}

Bool hwc_set_pixmap_buf(PixmapPtr pPix, EGLClientBuffer buf)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(pPix->drawable.pScreen);
    HWCPtr hwc = HWCPTR(scrn);
    struct hwc_pixmap *priv;
    void *pixels = NULL;
    int err;

    priv = hwc_get_pixmap_private(pPix);
    if (!priv && !buf)
        return TRUE;

    if (priv) {
        if (priv->buf) {
            if (priv->buf == buf)
                return TRUE;

            hwc->renderer.eglHybrisUnlockNativeBuffer(priv->buf);
            hwc->renderer.eglHybrisReleaseNativeBuffer(priv->buf);
            priv->handle_valid = FALSE;
        }
    }

    if (buf) {
        if (!priv) {
            priv = calloc(1, sizeof(struct hwc_pixmap));
            if (!priv)
                return FALSE;
        }
        priv->buf = buf;

        err = hwc->renderer.eglHybrisLockNativeBuffer(priv->buf,
                                    HYBRIS_USAGE_SW_READ_OFTEN,
                                    0, 0, pPix->drawable.width, pPix->drawable.height, &pixels);

        //xf86DrvMsg(scrn->scrnIndex, X_INFO, "hwc_set_pixmap_buf: gralloc lock returns %i\n", err);
        //xf86DrvMsg(scrn->scrnIndex, X_INFO, "hwc_set_pixmap_buf: lock to vaddr %p\n", pixels);

        if (!pPix->drawable.pScreen->ModifyPixmapHeader(pPix, -1, -1, -1, -1, -1, pixels))
            FatalError("Couldn't adjust screen pixmap\n");

    }

    hwc_set_pixmap_private(pPix, priv);
    return TRUE;
}

/* This should only be called when glamor is disabled */
Bool hwc_pixmap_init(ScreenPtr screen)
{
    if (!dixRegisterPrivateKey(&hwc_pixmap_index, PRIVATE_PIXMAP, 0))
        return FALSE;

    screen->CreatePixmap = hwc_pixmap_create;
    screen->DestroyPixmap = hwc_pixmap_destroy;
    return TRUE;
}
