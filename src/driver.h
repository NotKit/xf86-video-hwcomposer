/* All drivers should typically include these */
#include "xf86.h"
#include "xf86_OSproc.h"

#include "xf86Cursor.h"

#ifdef XvExtension
#include "xf86xv.h"
#include <X11/extensions/Xv.h>
#endif
#include <string.h>
#include <pthread.h>

#include <android-config.h>

#define MESA_EGL_NO_X11_HEADERS 1
#include <epoxy/gl.h>
#include <epoxy/egl.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <hardware/lights.h>
#include <hybris/eglplatformcommon/hybris_nativebufferext.h>

#include "compat-api.h"

/* function prototypes */

extern Bool SwitchMode(SWITCH_MODE_ARGS_DECL);
extern void AdjustFrame(ADJUST_FRAME_ARGS_DECL);

/* globals */
typedef struct _color
{
    int red;
    int green;
    int blue;
} dummy_colors;

Bool hwc_display_pre_init(ScrnInfoPtr pScrn);
Bool hwc_hwcomposer_init(ScrnInfoPtr pScrn);
void hwc_hwcomposer_close(ScrnInfoPtr pScrn);
Bool hwc_lights_init(ScrnInfoPtr pScrn);
Bool hwc_init_hybris_native_buffer(ScrnInfoPtr pScrn);
Bool hwc_egl_renderer_init(ScrnInfoPtr pScrn);
void hwc_egl_renderer_close(ScrnInfoPtr pScrn);
void hwc_egl_renderer_screen_init(ScreenPtr pScreen);
void hwc_egl_renderer_screen_close(ScreenPtr pScreen);
void hwc_egl_renderer_update(ScreenPtr pScreen);
Bool hwc_present_screen_init(ScreenPtr pScreen);
void hwc_vblank_screen_init(ScreenPtr pScreen);
int hwc_queue_vblank(ScreenPtr screen, uint64_t event_id, uint64_t hwcc);
Bool hwc_cursor_init(ScreenPtr pScreen);

typedef enum {
    HWC_ROTATE_CW,
    HWC_ROTATE_CCW
} hwc_rotation;

typedef struct HWCRec
{
    /* options */
    OptionInfoPtr Options;
    Bool swCursor;
    /* proc pointer */
    CloseScreenProcPtr CloseScreen;
    CreateScreenResourcesProcPtr	CreateScreenResources;
    xf86CursorInfoPtr CursorInfo;
    ScreenBlockHandlerProcPtr BlockHandler;

    dummy_colors colors[1024];
    Bool        (*CreateWindow)() ;     /* wrapped CreateWindow */
    Bool prop;

    DamagePtr damage;
    Bool dirty;
    Bool glamor;
    Bool drihybris;
    hwc_rotation rotation;

    gralloc_module_t *gralloc;
    alloc_device_t *alloc;

    hwc_composer_device_1_t *hwcDevicePtr;
    hwc_display_contents_1_t **hwcContents;
    hwc_layer_1_t *fblayer;
    uint32_t hwcVersion;
    int hwcWidth;
    int hwcHeight;

    Bool cursorShown;
    xf86CursorInfoPtr cursorInfo;
    int cursorX;
    int cursorY;
    int cursorWidth;
    int cursorHeight;

    struct light_device_t *lightsDevice;
    int screenBrightness;

    DisplayModePtr modes;
    int dpmsMode;

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
    GLuint cursorTexture;
    GLuint shaderProgram;
    GLuint shaderProgramMVP;

    float projection[16];
    float rotationMatrix[16];
    float projectionRotated[16];

    EGLClientBuffer buffer;
    int stride;
    EGLImageKHR image;
} HWCRec, *HWCPtr;

/* The privates of the hwcomposer driver */
#define HWCPTR(p)	((HWCPtr)((p)->driverPrivate))

