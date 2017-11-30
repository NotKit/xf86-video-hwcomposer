/* All drivers should typically include these */
#include "xf86.h"
#include "xf86_OSproc.h"

#include "xf86Cursor.h"

#ifdef XvExtension
#include "xf86xv.h"
#include <X11/extensions/Xv.h>
#endif
#include <string.h>

#include <android-config.h>

#define MESA_EGL_NO_X11_HEADERS 1
#include <epoxy/gl.h>
#include <epoxy/egl.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <hybris/eglplatformcommon/hybris_nativebufferext.h>

#include "compat-api.h"

/* Supported chipsets */
typedef enum {
    DUMMY_CHIP
} DUMMYType;

/* function prototypes */

extern Bool DUMMYSwitchMode(SWITCH_MODE_ARGS_DECL);
extern void DUMMYAdjustFrame(ADJUST_FRAME_ARGS_DECL);

/* in dummy_cursor.c */
extern Bool DUMMYCursorInit(ScreenPtr pScrn);
extern void DUMMYShowCursor(ScrnInfoPtr pScrn);
extern void DUMMYHideCursor(ScrnInfoPtr pScrn);

/* globals */
typedef struct _color
{
    int red;
    int green;
    int blue;
} dummy_colors;

Bool hwc_hwcomposer_init(ScrnInfoPtr pScrn);
void hwc_hwcomposer_close(ScrnInfoPtr pScrn);
Bool hwc_init_hybris_native_buffer(ScrnInfoPtr pScrn);
Bool hwc_egl_renderer_init(ScrnInfoPtr pScrn);
void hwc_egl_renderer_close(ScrnInfoPtr pScrn);
void hwc_egl_renderer_screen_init(ScreenPtr pScreen);
void hwc_egl_renderer_screen_close(ScreenPtr pScreen);
void hwc_egl_renderer_update(ScreenPtr pScreen);
Bool hwc_present_screen_init(ScreenPtr pScreen);

typedef struct dummyRec 
{
    /* options */
    OptionInfoPtr Options;
    Bool swCursor;
    /* proc pointer */
    CloseScreenProcPtr CloseScreen;
    CreateScreenResourcesProcPtr	CreateScreenResources;
    xf86CursorInfoPtr CursorInfo;
    ScreenBlockHandlerProcPtr BlockHandler;

    Bool DummyHWCursorShown;
    int cursorX, cursorY;
    int cursorFG, cursorBG;

    dummy_colors colors[1024];
    Bool        (*CreateWindow)() ;     /* wrapped CreateWindow */
    Bool prop;

    DamagePtr damage;
    Bool dirty_enabled;
    Bool glamor;
    Bool drihybris;

    gralloc_module_t *gralloc;
    alloc_device_t *alloc;

    hwc_composer_device_1_t *hwcDevicePtr;
    hwc_display_contents_1_t **hwcContents;
    hwc_layer_1_t *fblayer;

    int hwcWidth;
    int hwcHeight;

    PFNEGLHYBRISCREATENATIVEBUFFERPROC eglHybrisCreateNativeBuffer;
    PFNEGLHYBRISLOCKNATIVEBUFFERPROC eglHybrisLockNativeBuffer;
    PFNEGLHYBRISUNLOCKNATIVEBUFFERPROC eglHybrisUnlockNativeBuffer;
    PFNEGLHYBRISRELEASENATIVEBUFFERPROC eglHybrisReleaseNativeBuffer;
    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;

    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    GLuint rootTexture;
    GLuint shaderProgram;

    EGLClientBuffer buffer;
    int stride;
    EGLImageKHR image;
} DUMMYRec, *DUMMYPtr;

/* The privates of the DUMMY driver */
#define DUMMYPTR(p)	((DUMMYPtr)((p)->driverPrivate))

