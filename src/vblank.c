/*
 * Copyright © 2018 TheKit
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <xf86.h>
#include <xf86Crtc.h>
#include "driver.h"

#define VBLANK_INTERVAL 16667

static struct xorg_list fake_vblank_queue;

typedef struct hwc_vblank {
    struct xorg_list            list;
    uint64_t                    event_id;
    OsTimerPtr                  timer;
    ScreenPtr                   screen;
} hwc_vblank_rec, *hwc_vblank_ptr;

int
hwc_get_ust_hwcc(ScreenPtr screen, uint64_t *ust, uint64_t *hwcc)
{
    *ust = GetTimeInMicros();
    *hwcc = (*ust + VBLANK_INTERVAL / 2) / VBLANK_INTERVAL;
    return Success;
}

static void
hwc_update(ScreenPtr screen, uint64_t event_id)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(screen);
    HWCPtr hwc = HWCPTR(pScrn);
    PixmapPtr rootPixmap;
    int err;

    if (hwc->damage && hwc->dpmsMode == DPMSModeOn) {
        RegionPtr dirty = DamageRegion(hwc->damage);
        unsigned num_cliprects = REGION_NUM_RECTS(dirty);

        if (num_cliprects || hwc->dirty)
        {
            void *pixels = NULL;
            rootPixmap = screen->GetScreenPixmap(screen);
            hwc->eglHybrisUnlockNativeBuffer(hwc->buffer);

            hwc_egl_renderer_update(screen);

            err = hwc->eglHybrisLockNativeBuffer(hwc->buffer,
                            HYBRIS_USAGE_SW_READ_RARELY|HYBRIS_USAGE_SW_WRITE_OFTEN,
                            0, 0, hwc->stride, pScrn->virtualY, &pixels);

            if (!hwc->glamor) {
                if (!screen->ModifyPixmapHeader(rootPixmap, -1, -1, -1, -1, -1, pixels))
                    FatalError("Couldn't adjust screen pixmap\n");
            }

            DamageEmpty(hwc->damage);
            hwc->dirty = FALSE;
        }
    }
}

static CARD32
hwc_do_timer(OsTimerPtr timer,
                      CARD32 time,
                      void *arg)
{
    hwc_vblank_ptr fake_vblank = arg;

    //xf86DrvMsg(xf86ScreenToScrn(fake_vblank->screen)->scrnIndex, X_INFO,
    //           "fake vblank event, time=%d\n", time);

    hwc_queue_vblank(fake_vblank->screen, fake_vblank->event_id + 1, 1);

    hwc_update(fake_vblank->screen, fake_vblank->event_id);
    xorg_list_del(&fake_vblank->list);
    TimerFree(fake_vblank->timer);
    free(fake_vblank);
    return 0;
}

void
hwc_abort_vblank(ScreenPtr screen, uint64_t event_id, uint64_t hwcc)
{
    hwc_vblank_ptr     fake_vblank, tmp;

    xorg_list_for_each_entry_safe(fake_vblank, tmp, &fake_vblank_queue, list) {
        if (fake_vblank->event_id == event_id) {
            TimerFree(fake_vblank->timer); /* TimerFree will call TimerCancel() */
            xorg_list_del(&fake_vblank->list);
            free (fake_vblank);
            break;
        }
    }
}

int
hwc_queue_vblank(ScreenPtr     screen,
                          uint64_t      event_id,
                          uint64_t      hwcc)
{
    uint64_t                    ust = hwcc * VBLANK_INTERVAL;
    uint64_t                    now = GetTimeInMicros();
    //INT32                       delay = ((int64_t) (ust - now)) / 1000;
    INT32                       delay = VBLANK_INTERVAL / 1000;
    hwc_vblank_ptr     fake_vblank;

    if (delay <= 0) {
        hwc_update(screen, event_id);
        return Success;
    }

    fake_vblank = calloc (1, sizeof(hwc_vblank_rec));
    if (!fake_vblank)
        return BadAlloc;

    fake_vblank->screen = screen;
    fake_vblank->event_id = event_id;
    fake_vblank->timer = TimerSet(NULL, 0, delay, hwc_do_timer, fake_vblank);
    if (!fake_vblank->timer) {
        free(fake_vblank);
        return BadAlloc;
    }

    xorg_list_add(&fake_vblank->list, &fake_vblank_queue);

    return Success;
}

void
hwc_vblank_screen_init(ScreenPtr screen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    HWCPtr hwc = HWCPTR(scrn);
    xorg_list_init(&fake_vblank_queue);

    hwc_queue_vblank(screen, 1, 1);
}

void
hwc_vblank_close_screen(ScreenPtr screen)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    HWCPtr hwc = HWCPTR(scrn);
}
