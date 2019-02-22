/*
* Copyright © 2014 Advanced Micro Devices, Inc.
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

#ifndef HWC_PIXMAP_H
#define HWC_PIXMAP_H

struct hwc_pixmap {
    /* GEM handle for pixmaps shared via DRI2/3 */
    Bool handle_valid;
    EGLClientBuffer buf;
};

extern DevPrivateKeyRec hwc_pixmap_index;

static inline struct hwc_pixmap *hwc_get_pixmap_private(PixmapPtr pixmap)
{
    return dixGetPrivate(&pixmap->devPrivates, &hwc_pixmap_index);
}

static inline void hwc_set_pixmap_private(PixmapPtr pixmap,
                        struct hwc_pixmap *priv)
{
    dixSetPrivate(&pixmap->devPrivates, &hwc_pixmap_index, priv);
}

Bool hwc_set_pixmap_buf(PixmapPtr pPix, EGLClientBuffer buf);

enum {
    HWC_CREATE_PIXMAP_DRIHYBRIS = 0x08000000,
};

extern Bool hwc_pixmap_init(ScreenPtr screen);

#endif /* AMDGPU_PIXMAP_H */
