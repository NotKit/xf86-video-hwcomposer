
/*
 * Copyright 2002, SuSE Linux AG, Author: Egbert Eich
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* All drivers should typically include these */
#include "xf86.h"
#include "xf86_OSproc.h"

/* All drivers initialising the SW cursor need this */
#include "mipointer.h"

/* All drivers using the mi colormap manipulation need this */
#include "micmap.h"

/* identifying atom needed by magnifiers */
#include <X11/Xatom.h>
#include "property.h"

#include "xf86cmap.h"
#include "xf86Crtc.h"

#include "fb.h"

#include "picturestr.h"

/*
 * Driver data structures.
 */
#include "dummy.h"

#ifdef ENABLE_GLAMOR
#define GLAMOR_FOR_XORG 1
#include <glamor.h>
#endif
#ifdef ENABLE_DRIHYBRIS
#include <drihybris.h>
#endif

/* These need to be checked */
#include <X11/X.h>
#include <X11/Xproto.h>
#include "scrnintstr.h"
#include "servermd.h"

/* Mandatory functions */
static const OptionInfoRec *	DUMMYAvailableOptions(int chipid, int busid);
static void     DUMMYIdentify(int flags);
static Bool     DUMMYProbe(DriverPtr drv, int flags);
static Bool     DUMMYPreInit(ScrnInfoPtr pScrn, int flags);
static Bool     DUMMYScreenInit(SCREEN_INIT_ARGS_DECL);
static Bool     DUMMYEnterVT(VT_FUNC_ARGS_DECL);
static void     DUMMYLeaveVT(VT_FUNC_ARGS_DECL);
static Bool     DUMMYCloseScreen(CLOSE_SCREEN_ARGS_DECL);
static Bool     DUMMYCreateWindow(WindowPtr pWin);
static void     DUMMYFreeScreen(FREE_SCREEN_ARGS_DECL);
static ModeStatus DUMMYValidMode(SCRN_ARG_TYPE arg, DisplayModePtr mode,
                                 Bool verbose, int flags);
static Bool	DUMMYSaveScreen(ScreenPtr pScreen, int mode);

/* Internally used functions */
static Bool	dummyDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op,
                            pointer ptr);

#define DUMMY_VERSION 4000
#define DUMMY_NAME "hwcomposer"
#define DUMMY_DRIVER_NAME "hwcomposer"

#define DUMMY_MAJOR_VERSION PACKAGE_VERSION_MAJOR
#define DUMMY_MINOR_VERSION PACKAGE_VERSION_MINOR
#define DUMMY_PATCHLEVEL PACKAGE_VERSION_PATCHLEVEL

/*
 * This is intentionally screen-independent.  It indicates the binding
 * choice made in the first PreInit.
 */
static int pix24bpp = 0;

/*
 * This contains the functions needed by the server after loading the driver
 * module.  It must be supplied, and gets passed back by the SetupProc
 * function in the dynamic case.  In the static case, a reference to this
 * is compiled in, and this requires that the name of this DriverRec be
 * an upper-case version of the driver name.
 */

_X_EXPORT DriverRec HWCOMPOSER = {
    DUMMY_VERSION,
    DUMMY_DRIVER_NAME,
    DUMMYIdentify,
    DUMMYProbe,
    DUMMYAvailableOptions,
    NULL,
    0,
    dummyDriverFunc
};

static SymTabRec DUMMYChipsets[] = {
    { DUMMY_CHIP, "hwcomposer" },
    { -1,         NULL }
};

typedef enum {
    OPTION_SW_CURSOR,
    OPTION_ACCEL_METHOD
} DUMMYOpts;

static const OptionInfoRec DUMMYOptions[] = {
    { OPTION_SW_CURSOR, "SWcursor", OPTV_BOOLEAN, {0}, FALSE },
    { OPTION_ACCEL_METHOD, "AccelMethod", OPTV_STRING, {0}, FALSE},
    { -1,               NULL,       OPTV_NONE,    {0}, FALSE }
};

#ifdef XFree86LOADER

static MODULESETUPPROTO(dummySetup);

static XF86ModuleVersionInfo hwcomposerVersRec =
{
	"hwcomposer",
	MODULEVENDORSTRING,
	MODINFOSTRING1,
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,
	DUMMY_MAJOR_VERSION, DUMMY_MINOR_VERSION, DUMMY_PATCHLEVEL,
	ABI_CLASS_VIDEODRV,
	ABI_VIDEODRV_VERSION,
	MOD_CLASS_VIDEODRV,
	{0,0,0,0}
};

/*
 * This is the module init data.
 * Its name has to be the driver name followed by ModuleData
 */
_X_EXPORT XF86ModuleData hwcomposerModuleData = { &hwcomposerVersRec, dummySetup, NULL };

static pointer
dummySetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
    static Bool setupDone = FALSE;

    if (!setupDone) {
        setupDone = TRUE;
        xf86AddDriver(&HWCOMPOSER, module, HaveDriverFuncs);

        /*
        * Modules that this driver always requires can be loaded here
        * by calling LoadSubModule().
        */

        /*
        * The return value must be non-NULL on success even though there
        * is no TearDownProc.
        */
        return (pointer)1;
    } else {
        if (errmaj) *errmaj = LDR_ONCEONLY;
            return NULL;
    }
}

#endif /* XFree86LOADER */

/*
 * Build a DisplayModeRec that matches the screen's dimensions.
 *
 * Make up a fake pixel clock so that applications that use the VidMode
 * extension to query the "refresh rate" get 60 Hz.
 */
static void ConstructFakeDisplayMode(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
    mode->HDisplay = mode->HSyncStart = mode->HSyncEnd = mode->HTotal =
        pScrn->virtualX;
    mode->VDisplay = mode->VSyncStart = mode->VSyncEnd = mode->VTotal =
        pScrn->virtualY;
    mode->Clock = mode->HTotal * mode->VTotal * 60 / 1000;

    xf86SetCrtcForModes(pScrn, 0);
}

static Bool
dummy_xf86crtc_resize(ScrnInfoPtr pScrn, int width, int height)
{
    ScreenPtr pScreen = pScrn->pScreen;
    PixmapPtr rootPixmap = pScreen->GetScreenPixmap(pScreen);
    int newPitch = width * (pScrn->bitsPerPixel / 8);
    void *oldScreen = rootPixmap->devPrivate.ptr;
    void *newScreen = calloc(newPitch, height);

    if (!newScreen)
        return FALSE;

    if (!pScreen->ModifyPixmapHeader(rootPixmap, width, height,
                                     -1, -1, newPitch, newScreen)) {
        free(newScreen);
        return FALSE;
    }

    free(oldScreen);

    pScrn->virtualX = width;
    pScrn->virtualY = height;
    pScrn->displayWidth = width;
    ConstructFakeDisplayMode(pScrn, pScrn->modes);

    return TRUE;
}

static const xf86CrtcConfigFuncsRec dummy_xf86crtc_config_funcs = {
    dummy_xf86crtc_resize
};

static Bool
DUMMYGetRec(ScrnInfoPtr pScrn)
{
    /*
     * Allocate a DUMMYRec, and hook it into pScrn->driverPrivate.
     * pScrn->driverPrivate is initialised to NULL, so we can check if
     * the allocation has already been done.
     */
    if (pScrn->driverPrivate != NULL)
        return TRUE;

    pScrn->driverPrivate = xnfcalloc(sizeof(DUMMYRec), 1);

    if (pScrn->driverPrivate == NULL)
        return FALSE;
    return TRUE;
}

static void
DUMMYFreeRec(ScrnInfoPtr pScrn)
{
    if (pScrn->driverPrivate == NULL)
	return;
    free(pScrn->driverPrivate);
    pScrn->driverPrivate = NULL;
}

static const OptionInfoRec *
DUMMYAvailableOptions(int chipid, int busid)
{
    return DUMMYOptions;
}

/* Mandatory */
static void
DUMMYIdentify(int flags)
{
    xf86PrintChipsets(DUMMY_NAME, "Driver for Dummy chipsets",
                      DUMMYChipsets);
}

/* Mandatory */
static Bool
DUMMYProbe(DriverPtr drv, int flags)
{
    Bool foundScreen = FALSE;
    int numDevSections, numUsed;
    GDevPtr *devSections;
    int i;

    if (flags & PROBE_DETECT)
        return FALSE;
    /*
     * Find the config file Device sections that match this
     * driver, and return if there are none.
     */
    if ((numDevSections = xf86MatchDevice(DUMMY_DRIVER_NAME,
                                          &devSections)) <= 0) {
        return FALSE;
    }

    numUsed = numDevSections;

    if (numUsed > 0) {
        for (i = 0; i < numUsed; i++) {
            ScrnInfoPtr pScrn = NULL;
            int entityIndex =
            xf86ClaimNoSlot(drv,DUMMY_CHIP,devSections[i],TRUE);
            /* Allocate a ScrnInfoRec and claim the slot */
            if ((pScrn = xf86AllocateScreen(drv,0 ))) {
            xf86AddEntityToScreen(pScrn,entityIndex);
                pScrn->driverVersion = DUMMY_VERSION;
                pScrn->driverName    = DUMMY_DRIVER_NAME;
                pScrn->name          = DUMMY_NAME;
                pScrn->Probe         = DUMMYProbe;
                pScrn->PreInit       = DUMMYPreInit;
                pScrn->ScreenInit    = DUMMYScreenInit;
                pScrn->SwitchMode    = DUMMYSwitchMode;
                pScrn->AdjustFrame   = DUMMYAdjustFrame;
                pScrn->EnterVT       = DUMMYEnterVT;
                pScrn->LeaveVT       = DUMMYLeaveVT;
                pScrn->FreeScreen    = DUMMYFreeScreen;
                pScrn->ValidMode     = DUMMYValidMode;

                foundScreen = TRUE;
            }
        }
    }

    free(devSections);

    return foundScreen;
}

#ifdef ENABLE_GLAMOR
static void
try_enable_glamor(ScrnInfoPtr pScrn)
{
    DUMMYPtr dPtr = DUMMYPTR(pScrn);
    const char *accel_method_str = xf86GetOptValString(dPtr->Options,
                                                       OPTION_ACCEL_METHOD);
    Bool do_glamor = (!accel_method_str ||
                      strcmp(accel_method_str, "glamor") == 0);

    if (!do_glamor) {
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "glamor disabled\n");
        return;
    }

    if (xf86LoadSubModule(pScrn, GLAMOR_EGLHYBRIS_MODULE_NAME)) {
        //if (hwc_glamor_egl_init(pScrn, dPtr->display, dPtr->context, dPtr->surface)) {
        if (hwc_glamor_egl_init(pScrn, dPtr->display, dPtr->context, dPtr->surface)) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "glamor-hybris initialized\n");
            dPtr->glamor = TRUE;
        } else {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "glamor-hybris initialization failed\n");
        }
    } else {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to load glamor-hybris module.\n");
    }
#ifdef ENABLE_DRIHYBRIS
    if (xf86LoadSubModule(pScrn, "drihybris"))
    {
        dPtr->drihybris = TRUE;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "drihybris initialized\n");
    }
#endif
}
#endif

# define RETURN \
    { DUMMYFreeRec(pScrn);\
			    return FALSE;\
					     }

/* Mandatory */
Bool
DUMMYPreInit(ScrnInfoPtr pScrn, int flags)
{
    DUMMYPtr dPtr;
    GDevPtr device = xf86GetEntityInfo(pScrn->entityList[0])->device;

    if (flags & PROBE_DETECT)
        return TRUE;

    /* Allocate the DummyRec driverPrivate */
    if (!DUMMYGetRec(pScrn)) {
        return FALSE;
    }

    dPtr = DUMMYPTR(pScrn);

    pScrn->chipset = (char *)xf86TokenToString(DUMMYChipsets, DUMMY_CHIP);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Chipset is a DUMMY\n");

    pScrn->monitor = pScrn->confScreen->monitor;

    if (!xf86SetDepthBpp(pScrn, 0, 0, 0,  Support24bppFb | Support32bppFb))
        return FALSE;
    else {
        /* Check that the returned depth is one we support */
        switch (pScrn->depth) {
        case 8:
        case 15:
        case 16:
        case 24:
        case 30:
            break;
        default:
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Given depth (%d) is not supported by this driver\n",
                       pScrn->depth);
            return FALSE;
        }
    }

    xf86PrintDepthBpp(pScrn);
    if (pScrn->depth == 8)
        pScrn->rgbBits = 8;

    /* Get the depth24 pixmap format */
    if (pScrn->depth == 24 && pix24bpp == 0)
        pix24bpp = xf86GetBppFromDepth(pScrn, 24);

    /*
     * This must happen after pScrn->display has been set because
     * xf86SetWeight references it.
     */
    if (pScrn->depth > 8) {
        /* The defaults are OK for us */
        rgb zeros = {0, 0, 0};

        if (!xf86SetWeight(pScrn, zeros, zeros)) {
            return FALSE;
        } else {
            /* XXX check that weight returned is supported */
            ;
        }
    }

    if (!xf86SetDefaultVisual(pScrn, -1))
        return FALSE;

    if (pScrn->depth > 1) {
    Gamma zeros = {0.0, 0.0, 0.0};

    if (!xf86SetGamma(pScrn, zeros))
        return FALSE;
    }

    xf86CollectOptions(pScrn, device->options);
    /* Process the options */
    if (!(dPtr->Options = malloc(sizeof(DUMMYOptions))))
        return FALSE;
    memcpy(dPtr->Options, DUMMYOptions, sizeof(DUMMYOptions));

    xf86ProcessOptions(pScrn->scrnIndex, pScrn->options, dPtr->Options);

    xf86GetOptValBool(dPtr->Options, OPTION_SW_CURSOR,&dPtr->swCursor);

    //xf86CrtcConfigInit(pScrn, &dummy_xf86crtc_config_funcs);
    //xf86CrtcSetSizeRange(pScrn, 8, 8, SHRT_MAX, SHRT_MAX);

    if (!hwc_hwcomposer_init(pScrn)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "failed to initialize HWComposer API and layers\n");
        return FALSE;
    }

    /* Pick up size from the "Display" subsection if it exists */
    if (pScrn->display->virtualX) {
        pScrn->virtualX = pScrn->display->virtualX;
        pScrn->virtualY = pScrn->display->virtualY;
    } else {
        /* Pick rotated HWComposer screen resolution */
        pScrn->virtualX = dPtr->hwcHeight;
        pScrn->virtualY = dPtr->hwcWidth;
     }
    pScrn->displayWidth = pScrn->virtualX;

    /* Construct a mode with the screen's initial dimensions */
    pScrn->modes = calloc(sizeof(DisplayModeRec), 1);
    ConstructFakeDisplayMode(pScrn, pScrn->modes);
    pScrn->modes->next = pScrn->modes->prev = pScrn->modes;
    pScrn->currentMode = pScrn->modes;

    /* Print the list of modes being used */
    xf86PrintModes(pScrn);

    /* If monitor resolution is set on the command line, use it */
    xf86SetDpi(pScrn, 0, 0);

    if (xf86LoadSubModule(pScrn, "fb") == NULL) {
        RETURN;
    }

    if (!dPtr->swCursor) {
        if (!xf86LoadSubModule(pScrn, "ramdac"))
            RETURN;
    }

    /* We have no contiguous physical fb in physical memory */
    pScrn->memPhysBase = 0;
    pScrn->fbOffset = 0;

    if (!hwc_egl_renderer_init(pScrn)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "failed to initialize EGL renderer\n");
            return FALSE;
    }

    if (!hwc_init_hybris_native_buffer(pScrn)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "failed to initialize libhybris native buffer EGL extension\n");
        return FALSE;
    }

    dPtr->buffer = NULL;

    dPtr->glamor = FALSE;
    dPtr->drihybris = FALSE;
#ifdef ENABLE_GLAMOR
    try_enable_glamor(pScrn);
#endif

    return TRUE;
}
#undef RETURN

/* Mandatory */
static Bool
DUMMYEnterVT(VT_FUNC_ARGS_DECL)
{
    return TRUE;
}

/* Mandatory */
static void
DUMMYLeaveVT(VT_FUNC_ARGS_DECL)
{
}

static void
DUMMYLoadPalette(
   ScrnInfoPtr pScrn,
   int numColors,
   int *indices,
   LOCO *colors,
   VisualPtr pVisual
){
   int i, index, shift, Gshift;
   DUMMYPtr dPtr = DUMMYPTR(pScrn);

   switch(pScrn->depth) {
   case 15:
    shift = Gshift = 1;
    break;
   case 16:
    shift = 0;
    Gshift = 0;
    break;
   default:
    shift = Gshift = 0;
    break;
   }

   for(i = 0; i < numColors; i++) {
       index = indices[i];
       dPtr->colors[index].red = colors[index].red << shift;
       dPtr->colors[index].green = colors[index].green << Gshift;
       dPtr->colors[index].blue = colors[index].blue << shift;
   }
}

static void DUMMYBlockHandler(ScreenPtr pScreen, void *timeout)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    DUMMYPtr dPtr = DUMMYPTR(pScrn);
    PixmapPtr rootPixmap;
    int err;

    pScreen->BlockHandler = dPtr->BlockHandler;
    pScreen->BlockHandler(pScreen, timeout);
    pScreen->BlockHandler = DUMMYBlockHandler;

    RegionPtr dirty = DamageRegion(dPtr->damage);
    unsigned num_cliprects = REGION_NUM_RECTS(dirty);

    if (num_cliprects)
    {
        void *pixels = NULL;
        rootPixmap = pScreen->GetScreenPixmap(pScreen);
        dPtr->eglHybrisUnlockNativeBuffer(dPtr->buffer);

        hwc_egl_renderer_update(pScreen);

        err = dPtr->eglHybrisLockNativeBuffer(dPtr->buffer,
                        HYBRIS_USAGE_SW_READ_RARELY|HYBRIS_USAGE_SW_WRITE_OFTEN,
                        0, 0, dPtr->stride, pScrn->virtualY, &pixels);

        if (!dPtr->glamor) {
            if (!pScreen->ModifyPixmapHeader(rootPixmap, -1, -1, -1, -1, -1, pixels))
                FatalError("Couldn't adjust screen pixmap\n");
        }

        DamageEmpty(dPtr->damage);
    }
}

static Bool
CreateScreenResources(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    DUMMYPtr dPtr = DUMMYPTR(pScrn);
    PixmapPtr rootPixmap;
    Bool ret;
    void *pixels = NULL;
    int err;

    pScreen->CreateScreenResources = dPtr->CreateScreenResources;
    ret = pScreen->CreateScreenResources(pScreen);
    pScreen->CreateScreenResources = CreateScreenResources;

    rootPixmap = pScreen->GetScreenPixmap(pScreen);

#ifdef ENABLE_GLAMOR
    if (dPtr->glamor) {
        pScreen->DestroyPixmap(rootPixmap);

        rootPixmap = glamor_create_pixmap(pScreen,
                                            pScreen->width,
                                            pScreen->height,
                                            pScreen->rootDepth,
                                            GLAMOR_CREATE_NO_LARGE);
        pScreen->SetScreenPixmap(rootPixmap);
    }
#endif

    err = dPtr->eglHybrisCreateNativeBuffer(pScrn->virtualX, pScrn->virtualY,
                                      HYBRIS_USAGE_HW_TEXTURE |
                                      HYBRIS_USAGE_SW_READ_RARELY|HYBRIS_USAGE_SW_WRITE_OFTEN,
                                      HYBRIS_PIXEL_FORMAT_RGBA_8888,
                                      &dPtr->stride, &dPtr->buffer);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "alloc: status=%d, stride=%d\n", err, dPtr->stride);

    hwc_egl_renderer_screen_init(pScreen);

#ifdef ENABLE_GLAMOR
    if (dPtr->glamor)
        dPtr->rootTexture = glamor_get_pixmap_texture(rootPixmap);
#endif

    err = dPtr->eglHybrisLockNativeBuffer(dPtr->buffer,
                                    HYBRIS_USAGE_SW_READ_RARELY|HYBRIS_USAGE_SW_WRITE_OFTEN,
                                    0, 0, dPtr->stride, pScrn->virtualY, &pixels);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "gralloc lock returns %i\n", err);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "lock to vaddr %p\n", pixels);

    if (!dPtr->glamor) {
        if (!pScreen->ModifyPixmapHeader(rootPixmap, -1, -1, -1, -1, -1, pixels))
            FatalError("Couldn't adjust screen pixmap\n");
    }

    dPtr->damage = DamageCreate(NULL, NULL, DamageReportNone, TRUE,
                                pScreen, rootPixmap);

    if (dPtr->damage) {
        DamageRegister(&rootPixmap->drawable, dPtr->damage);
        dPtr->dirty_enabled = TRUE;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Damage tracking initialized\n");
    }
    else {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "Failed to create screen damage record\n");
        return FALSE;
    }

    return ret;
}

static ScrnInfoPtr DUMMYScrn; /* static-globalize it */

/* Mandatory */
static Bool
DUMMYScreenInit(SCREEN_INIT_ARGS_DECL)
{
    ScrnInfoPtr pScrn;
    DUMMYPtr dPtr;
    int ret;
    VisualPtr visual;
    void *pixels;

    /*
     * we need to get the ScrnInfoRec for this screen, so let's allocate
     * one first thing
     */
    pScrn = xf86ScreenToScrn(pScreen);
    dPtr = DUMMYPTR(pScrn);
    DUMMYScrn = pScrn;

    /*
     * Reset visual list.
     */
    miClearVisualTypes();

    /* Setup the visuals we support. */

    if (!miSetVisualTypes(pScrn->depth,
      		      miGetDefaultVisualMask(pScrn->depth),
		      pScrn->rgbBits, pScrn->defaultVisual))
         return FALSE;

    if (!miSetPixmapDepths ()) return FALSE;

    /*
     * Call the framebuffer layer's ScreenInit function, and fill in other
     * pScreen fields.
     */
    ret = fbScreenInit(pScreen, NULL,
                       pScrn->virtualX, pScrn->virtualY,
                       pScrn->xDpi, pScrn->yDpi,
                       pScrn->displayWidth, pScrn->bitsPerPixel);
    if (!ret)
        return FALSE;

    if (pScrn->depth > 8) {
        /* Fixup RGB ordering */
        visual = pScreen->visuals + pScreen->numVisuals;
        while (--visual >= pScreen->visuals) {
            if ((visual->class | DynamicClass) == DirectColor) {
                visual->offsetRed = pScrn->offset.red;
                visual->offsetGreen = pScrn->offset.green;
                visual->offsetBlue = pScrn->offset.blue;
                visual->redMask = pScrn->mask.red;
                visual->greenMask = pScrn->mask.green;
                visual->blueMask = pScrn->mask.blue;
            }
        }
    }

    /* must be after RGB ordering fixed */
    fbPictureInit(pScreen, 0, 0);

#ifdef ENABLE_GLAMOR
    if (dPtr->glamor && !glamor_init(pScreen, GLAMOR_USE_EGL_SCREEN)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "Failed to initialize glamor at ScreenInit() time.\n");
        return FALSE;
    }
#endif

    xf86SetBlackWhitePixels(pScreen);

    dPtr->CreateScreenResources = pScreen->CreateScreenResources;
    pScreen->CreateScreenResources = CreateScreenResources;

    if (dPtr->swCursor)
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "Using Software Cursor.\n");

    xf86SetBackingStore(pScreen);
    xf86SetSilkenMouse(pScreen);

    /* Initialise cursor functions */
    miDCInitialize (pScreen, xf86GetPointerScreenFuncs());


    if (!dPtr->swCursor) {
      /* HW cursor functions */
        if (!DUMMYCursorInit(pScreen)) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Hardware cursor initialization failed\n");
            return FALSE;
        }
    }

    /* Initialise default colourmap */
    if(!miCreateDefColormap(pScreen))
        return FALSE;

    if (!xf86HandleColormaps(pScreen, 1024, pScrn->rgbBits,
                         DUMMYLoadPalette, NULL,
                         CMAP_PALETTED_TRUECOLOR
                         | CMAP_RELOAD_ON_MODE_SWITCH))
        return FALSE;

    //if (!xf86CrtcScreenInit(pScreen))
    //    return FALSE;

    pScreen->SaveScreen = DUMMYSaveScreen;

    /* Wrap the current CloseScreen function */
    dPtr->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = DUMMYCloseScreen;

    /* Wrap the current CreateWindow function */
    dPtr->CreateWindow = pScreen->CreateWindow;
    pScreen->CreateWindow = DUMMYCreateWindow;

    /* Report any unused options (only for the first generation) */
    if (serverGeneration == 1) {
        xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);
    }

    /* Wrap the current BlockHandler function */
    dPtr->BlockHandler = pScreen->BlockHandler;
    pScreen->BlockHandler = DUMMYBlockHandler;

#ifdef ENABLE_DRIHYBRIS
    if (dPtr->drihybris) {
        drihybris_extension_init();

        if (!hwc_present_screen_init(pScreen)) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Failed to initialize the Present extension.\n");
        }
    }
#endif

    return TRUE;
}

/* Mandatory */
Bool
DUMMYSwitchMode(SWITCH_MODE_ARGS_DECL)
{
    return TRUE;
}

/* Mandatory */
void
DUMMYAdjustFrame(ADJUST_FRAME_ARGS_DECL)
{
}

/* Mandatory */
static Bool
DUMMYCloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    DUMMYPtr dPtr = DUMMYPTR(pScrn);

    if (dPtr->damage) {
        DamageUnregister(dPtr->damage);
        DamageDestroy(dPtr->damage);
        dPtr->damage = NULL;
    }

    hwc_egl_renderer_screen_close(pScreen);

    if (dPtr->buffer != NULL)
    {
        dPtr->eglHybrisUnlockNativeBuffer(dPtr->buffer);
        dPtr->eglHybrisReleaseNativeBuffer(dPtr->buffer);
        dPtr->buffer = NULL;
    }

    if (dPtr->CursorInfo)
        xf86DestroyCursorInfoRec(dPtr->CursorInfo);

    pScrn->vtSema = FALSE;
    pScreen->CloseScreen = dPtr->CloseScreen;
    return (*pScreen->CloseScreen)(CLOSE_SCREEN_ARGS);
}

/* Optional */
static void
DUMMYFreeScreen(FREE_SCREEN_ARGS_DECL)
{
    SCRN_INFO_PTR(arg);
    DUMMYFreeRec(pScrn);
}

static Bool
DUMMYSaveScreen(ScreenPtr pScreen, int mode)
{
    return TRUE;
}

/* Optional */
static ModeStatus
DUMMYValidMode(SCRN_ARG_TYPE arg, DisplayModePtr mode, Bool verbose, int flags)
{
    return(MODE_OK);
}

Atom VFB_PROP  = 0;
#define  VFB_PROP_NAME  "VFB_IDENT"

static Bool
DUMMYCreateWindow(WindowPtr pWin)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    DUMMYPtr dPtr = DUMMYPTR(DUMMYScrn);
    WindowPtr pWinRoot;
    int ret;

    pScreen->CreateWindow = dPtr->CreateWindow;
    ret = pScreen->CreateWindow(pWin);
    dPtr->CreateWindow = pScreen->CreateWindow;
    pScreen->CreateWindow = DUMMYCreateWindow;

    if(ret != TRUE)
        return(ret);

    if(dPtr->prop == FALSE) {
#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 8
        pWinRoot = WindowTable[DUMMYScrn->pScreen->myNum];
#else
        pWinRoot = DUMMYScrn->pScreen->root;
#endif
        if (! ValidAtom(VFB_PROP))
            VFB_PROP = MakeAtom(VFB_PROP_NAME, strlen(VFB_PROP_NAME), 1);

        ret = dixChangeWindowProperty(serverClient, pWinRoot, VFB_PROP,
                                      XA_STRING, 8, PropModeReplace,
                                      (int)4, (pointer)"TRUE", FALSE);
        if( ret != Success)
            ErrorF("Could not set VFB root window property");
            dPtr->prop = TRUE;

        return TRUE;
    }
    return TRUE;
}

#ifndef HW_SKIP_CONSOLE
#define HW_SKIP_CONSOLE 4
#endif

static Bool
dummyDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op, pointer ptr)
{
    CARD32 *flag;

    switch (op) {
    case GET_REQUIRED_HW_INTERFACES:
        flag = (CARD32*)ptr;
        (*flag) = HW_SKIP_CONSOLE;
        return TRUE;
    default:
        return FALSE;
    }
}
