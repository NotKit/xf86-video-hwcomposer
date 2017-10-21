/*
 * Copyright © 2013 Siarhei Siamashka <siarhei.siamashka@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef HWCOMPOSER_H
#define HWCOMPOSER_H

typedef struct {
    hwc_composer_device_1_t *hwcDevicePtr;
    hwc_display_contents_1_t **hwcContents;
    hwc_layer_1_t *fblayer;

//     RegionRec           clip;
//     uint32_t            colorKey;
//     Bool                colorKeyEnabled;
//     int                 overlay_data_offs;
//     XF86VideoAdaptorPtr adapt[1];
//     void               *port_privates[1];
} HWComposer;

HWComposer *HWComposer_Init(ScreenPtr pScreen);
void HWComposer_Close(ScreenPtr pScreen);

#endif
