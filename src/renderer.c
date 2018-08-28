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
#include <sync/sync.h>
#include <hybris/hwcomposerwindow/hwcomposer.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "driver.h"

const char vertex_src [] =
    "attribute vec4 position;\n"
    "attribute vec4 texcoords;\n"
    "varying vec2 textureCoordinate;\n"

    "void main()\n"
    "{\n"
    "    gl_Position = position;\n"
    "    textureCoordinate = texcoords.xy;\n"
    "}\n";

const char vertex_mvp_src [] =
    "attribute vec2 position;\n"
    "attribute vec2 texcoords;\n"
    "varying vec2 textureCoordinate;\n"
    "uniform mat4 transform;\n"

    "void main()\n"
    "{\n"
    "    gl_Position = transform * vec4(position, 0.0, 1.0);\n"
    "    textureCoordinate = texcoords.xy;\n"
    "}\n";

const char fragment_src [] =
    "varying highp vec2 textureCoordinate;\n"
    "uniform sampler2D texture;\n"

    "void main()\n"
    "{\n"
    "    gl_FragColor = texture2D(texture, textureCoordinate);\n"
    "}\n";

const char fragment_src_bgra [] =
    "varying highp vec2 textureCoordinate;\n"
    "uniform sampler2D texture;\n"

    "void main()\n"
    "{\n"
    "    gl_FragColor = texture2D(texture, textureCoordinate).bgra;\n"
    "}\n";

const char fragment_src_cursor [] =
    "varying highp vec2 textureCoordinate;\n"
    "uniform sampler2D texture;\n"

    "void main()\n"
    "{\n"
    "    gl_FragColor = texture2D(texture, textureCoordinate).bgra;\n"
    "    //gl_FragColor = vec4(gl_FragColor.r, 1.0, 0.0, 1.0);\n"
    "}\n";

GLuint load_shader(const char *shader_source, GLenum type)
{
    GLuint  shader = glCreateShader(type);

    glShaderSource(shader, 1, &shader_source, NULL);
    glCompileShader(shader);

    return shader;
}

GLint position_loc;
GLint texcoords_loc;
GLint texture_loc;

GLint position_mvp_loc;
GLint texcoords_mvp_loc;
GLint transform_mvp_loc;
GLint texture_mvp_loc;

static const GLfloat squareVertices[] = {
    -1.0f, -1.0f,
    1.0f, -1.0f,
    -1.0f,  1.0f,
    1.0f,  1.0f,
};

static const GLfloat textureVertices[][8] = {
    { // CW - 90 degrees
        1.0f, 1.0f,
        1.0f, 0.0f,
        0.0f,  1.0f,
        0.0f,  0.0f,
    },
    { // CCW - 270 degrees
        0.0f,  0.0f,
        0.0f,  1.0f,
        1.0f, 0.0f,
        1.0f, 1.0f
    }
};

GLfloat cursorVertices[8];

void ortho2D(float* mat, float left, float right, float bottom, float top)
{
    const float zNear = -1.0f;
    const float zFar = 1.0f;
    const float inv_z = 1.0f / (zFar - zNear);
    const float inv_y = 1.0f / (top - bottom);
    const float inv_x = 1.0f / (right - left);

    // first column
    *mat++ = (2.0f*inv_x);
    *mat++ = (0.0f);
    *mat++ = (0.0f);
    *mat++ = (0.0f);

    // second
    *mat++ = (0.0f);
    *mat++ = (2.0*inv_y);
    *mat++ = (0.0f);
    *mat++ = (0.0f);

    // third
    *mat++ = (0.0f);
    *mat++ = (0.0f);
    *mat++ = (-2.0f*inv_z);
    *mat++ = (0.0f);

    // fourth
    *mat++ = (-(right + left)*inv_x);
    *mat++ = (-(top + bottom)*inv_y);
    *mat++ = (-(zFar + zNear)*inv_z);
    *mat++ = (1.0f);
}

/* adapted from Xf86Cursor.c */
static void
hwc_rotate_coord_to_hw(Rotation rotation,
                       int width,
                       int height, int x, int y, int *x_out, int *y_out)
{
    int t;

    if (rotation & RR_Reflect_X)
        x = width - x - 1;
    if (rotation & RR_Reflect_Y)
        y = height - y - 1;

    switch (rotation & 0xf) {
    case RR_Rotate_0:
        break;
    case RR_Rotate_90:
        t = x;
        x = height - y - 1;
        y = width - t - 1;
        break;
    case RR_Rotate_180:
        x = width - x - 1;
        y = height - y - 1;
        break;
    case RR_Rotate_270:
        t = x;
        x = y;
        y = t;
        break;
    }
    *x_out = x;
    *y_out = y;
}

void present(void *user_data, struct ANativeWindow *window,
                                struct ANativeWindowBuffer *buffer)
{
    ScrnInfoPtr pScrn = (ScrnInfoPtr)user_data;
    HWCPtr hwc = HWCPTR(pScrn);

    hwc_display_contents_1_t **contents = hwc->hwcContents;
    hwc_layer_1_t *fblayer = hwc->fblayer;
    hwc_composer_device_1_t *hwcdevice = hwc->hwcDevicePtr;

    int oldretire = contents[0]->retireFenceFd;
    contents[0]->retireFenceFd = -1;

    fblayer->handle = buffer->handle;
    fblayer->acquireFenceFd = HWCNativeBufferGetFence(buffer);
    fblayer->releaseFenceFd = -1;
    int err = hwcdevice->prepare(hwcdevice, HWC_NUM_DISPLAY_TYPES, contents);
    assert(err == 0);

    err = hwcdevice->set(hwcdevice, HWC_NUM_DISPLAY_TYPES, contents);
    // in android surfaceflinger ignores the return value as not all display types may be supported
    HWCNativeBufferSetFence(buffer, fblayer->releaseFenceFd);

    if (oldretire != -1)
    {
        sync_wait(oldretire, -1);
        close(oldretire);
    }
}

Bool hwc_init_hybris_native_buffer(ScrnInfoPtr pScrn)
{
    HWCPtr hwc = HWCPTR(pScrn);

    if (strstr(eglQueryString(hwc->display, EGL_EXTENSIONS), "EGL_HYBRIS_native_buffer") == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "EGL_HYBRIS_native_buffer is missing. Make sure libhybris EGL implementation is used\n");
        return FALSE;
    }

    hwc->eglHybrisCreateNativeBuffer = (PFNEGLHYBRISCREATENATIVEBUFFERPROC) eglGetProcAddress("eglHybrisCreateNativeBuffer");
    assert(hwc->eglHybrisCreateNativeBuffer != NULL);

    hwc->eglHybrisLockNativeBuffer = (PFNEGLHYBRISLOCKNATIVEBUFFERPROC) eglGetProcAddress("eglHybrisLockNativeBuffer");
    assert(hwc->eglHybrisLockNativeBuffer != NULL);

    hwc->eglHybrisUnlockNativeBuffer = (PFNEGLHYBRISUNLOCKNATIVEBUFFERPROC) eglGetProcAddress("eglHybrisUnlockNativeBuffer");
    assert(hwc->eglHybrisUnlockNativeBuffer != NULL);

    hwc->eglHybrisReleaseNativeBuffer = (PFNEGLHYBRISRELEASENATIVEBUFFERPROC) eglGetProcAddress("eglHybrisReleaseNativeBuffer");
    assert(hwc->eglHybrisReleaseNativeBuffer != NULL);

    hwc->eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
    assert(hwc->eglCreateImageKHR != NULL);

    hwc->eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR");
    assert(hwc->eglDestroyImageKHR  != NULL);

    hwc->glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress("glEGLImageTargetTexture2DOES");
    assert(hwc->glEGLImageTargetTexture2DOES != NULL);
}

void GLAPIENTRY
MessageCallback( GLenum source,
                 GLenum type,
                 GLuint id,
                 GLenum severity,
                 GLsizei length,
                 const GLchar* message,
                 const void* userParam )
{
  fprintf( stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
           ( type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "" ),
            type, severity, message );
}

Bool hwc_egl_renderer_init(ScrnInfoPtr pScrn)
{
    HWCPtr hwc = HWCPTR(pScrn);

    EGLDisplay display;
    EGLConfig ecfg;
    EGLint num_config;
    EGLint attr[] = {       // some attributes to set up our egl-interface
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_NONE
    };
    EGLSurface surface;
    EGLint ctxattr[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLContext context;
    EGLBoolean rv;
    int err;

    struct ANativeWindow *win = HWCNativeWindowCreate(hwc->hwcWidth, hwc->hwcHeight, HAL_PIXEL_FORMAT_RGBA_8888, present, pScrn);

    display = eglGetDisplay(NULL);
    assert(eglGetError() == EGL_SUCCESS);
    assert(display != EGL_NO_DISPLAY);
    hwc->display = display;

    rv = eglInitialize(display, 0, 0);
    assert(eglGetError() == EGL_SUCCESS);
    assert(rv == EGL_TRUE);

    eglChooseConfig((EGLDisplay) display, attr, &ecfg, 1, &num_config);
    assert(eglGetError() == EGL_SUCCESS);
    assert(rv == EGL_TRUE);

    surface = eglCreateWindowSurface((EGLDisplay) display, ecfg, (EGLNativeWindowType)win, NULL);
    assert(eglGetError() == EGL_SUCCESS);
    assert(surface != EGL_NO_SURFACE);
    hwc->surface = surface;

    context = eglCreateContext((EGLDisplay) display, ecfg, EGL_NO_CONTEXT, ctxattr);
    assert(eglGetError() == EGL_SUCCESS);
    assert(context != EGL_NO_CONTEXT);
    hwc->context = context;

    assert(eglMakeCurrent((EGLDisplay) display, surface, surface, context) == EGL_TRUE);

    // During init, enable debug output
    glEnable              ( GL_DEBUG_OUTPUT );
    glDebugMessageCallback( MessageCallback, 0 );


    const char *version = glGetString(GL_VERSION);
    assert(version);
    printf("%s\n",version);

    glGenTextures(1, &hwc->rootTexture);
    glGenTextures(1, &hwc->cursorTexture);
    hwc->image = EGL_NO_IMAGE_KHR;
    hwc->shaderProgram = 0;
    hwc->shaderProgramMVP = 0;

    return TRUE;
}

void hwc_egl_renderer_screen_init(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);

    glBindTexture(GL_TEXTURE_2D, hwc->rootTexture);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (!hwc->glamor && hwc->image == EGL_NO_IMAGE_KHR) {
        hwc->image = hwc->eglCreateImageKHR(hwc->display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_HYBRIS,
                                            (EGLClientBuffer)hwc->buffer, NULL);
        hwc->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, hwc->image);
    }

    if (!hwc->shaderProgram)
    {
        GLuint vertexShader = load_shader(vertex_src, GL_VERTEX_SHADER);     // load vertex shader
        GLuint fragmentShader;

        if (hwc->glamor)
            fragmentShader = load_shader(fragment_src, GL_FRAGMENT_SHADER);  // load fragment shader
        else
            fragmentShader = load_shader(fragment_src_bgra, GL_FRAGMENT_SHADER);  // load fragment shader

        GLuint shaderProgram  = hwc->shaderProgram = glCreateProgram();          // create program object
        glAttachShader ( shaderProgram, vertexShader );             // and attach both...
        glAttachShader ( shaderProgram, fragmentShader );           // ... shaders to it

        glLinkProgram ( shaderProgram );    // link the program

        // now get the locations of the shaders variables
        position_loc  = glGetAttribLocation(shaderProgram, "position");
        texcoords_loc = glGetAttribLocation(shaderProgram, "texcoords");
        texture_loc = glGetUniformLocation(shaderProgram, "texture");

        if ( position_loc < 0  ||  texcoords_loc < 0 || texture_loc < 0 ) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "EGLRenderer_Init: failed to get shader variables locations\n");
        }
    }

    if (!hwc->shaderProgramMVP)
    {
        GLuint vertexShader = load_shader(vertex_mvp_src, GL_VERTEX_SHADER);
        GLuint fragmentShader;

        if (hwc->glamor)
            fragmentShader = load_shader(fragment_src_cursor, GL_FRAGMENT_SHADER);
        else
            fragmentShader = load_shader(fragment_src_cursor, GL_FRAGMENT_SHADER);

        GLuint shaderProgram  = hwc->shaderProgramMVP = glCreateProgram();
        glAttachShader (shaderProgram, vertexShader);
        glAttachShader (shaderProgram, fragmentShader);

        glLinkProgram (shaderProgram);

        // now get the locations of the shaders variables
        position_mvp_loc  = glGetAttribLocation(shaderProgram, "position");
        texcoords_mvp_loc = glGetAttribLocation(shaderProgram, "texcoords");
        transform_mvp_loc = glGetUniformLocation(shaderProgram, "transform");
        texture_mvp_loc = glGetUniformLocation(shaderProgram, "texture");

        if ( position_mvp_loc < 0  ||  texcoords_mvp_loc < 0 || texture_mvp_loc < 0 ) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "EGLRenderer_Init: failed to get shader variables locations\n");
        }
    }

    ortho2D(hwc->projection, 0.0f, pScrn->virtualY, 0.0f, pScrn->virtualX);
    //rotation_matrix(hwc->rotationMatrix, 90.0f);
    //multiply_matrix(hwc->projectionRotated, hwc->projection, hwc->rotationMatrix);

    eglSwapInterval(hwc->display, 0);
}

void hwc_translate_cursor(int x, int y, int width, int height, float* vertices) {
    int w = width / 2;
    int h = height / 2;
    // top left
    vertices[0] = x;
    vertices[1] = y;
    // top right
    vertices[2] = x + width;
    vertices[3] = y;
    // bottom left
    vertices[4] = x;
    vertices[5] = y + height;
    // bottom right
    vertices[6] = x + width;
    vertices[7] = y + height;
}

void hwc_egl_render_cursor(ScreenPtr pScreen) {
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);

    glUseProgram(hwc->shaderProgramMVP);

    glBindTexture(GL_TEXTURE_2D, hwc->cursorTexture);
    glUniform1i(texture_mvp_loc, 0);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);

    int x, y;
    hwc_rotate_coord_to_hw(RR_Rotate_270, pScrn->virtualX,
                       pScrn->virtualY, hwc->cursorX, hwc->cursorY, &x, &y);
    hwc_translate_cursor(x, y, hwc->cursorWidth, hwc->cursorHeight, cursorVertices);

    glVertexAttribPointer(position_mvp_loc, 2, GL_FLOAT, 0, 0, cursorVertices);
    glEnableVertexAttribArray(position_mvp_loc);

    glVertexAttribPointer(texcoords_mvp_loc, 2, GL_FLOAT, 0, 0, textureVertices[hwc->rotation]);
    glEnableVertexAttribArray(texcoords_mvp_loc);

    glUniformMatrix4fv(transform_mvp_loc, 1, GL_FALSE, hwc->projection);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisable(GL_BLEND);
    glDisableVertexAttribArray(position_mvp_loc);
    glDisableVertexAttribArray(texcoords_mvp_loc);
}

void hwc_egl_renderer_update(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);

    if (hwc->glamor) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, hwc->hwcWidth, hwc->hwcHeight);
    }

    glUseProgram(hwc->shaderProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hwc->rootTexture);
    glUniform1i(texture_loc, 0);

    glVertexAttribPointer(position_loc, 2, GL_FLOAT, 0, 0, squareVertices);
    glEnableVertexAttribArray(position_loc);

    glVertexAttribPointer(texcoords_loc, 2, GL_FLOAT, 0, 0, textureVertices[hwc->rotation]);
    glEnableVertexAttribArray(texcoords_loc);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(position_loc);
    glDisableVertexAttribArray(texcoords_loc);

    if (hwc->cursorShown)
        hwc_egl_render_cursor(pScreen);

    eglSwapBuffers (hwc->display, hwc->surface );  // get the rendered buffer to the screen
}

void hwc_egl_renderer_screen_close(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);

    if (hwc->image != EGL_NO_IMAGE_KHR) {
        hwc->eglDestroyImageKHR(hwc->display, hwc->image);
        hwc->image = EGL_NO_IMAGE_KHR;
    }
}

void hwc_egl_renderer_close(ScrnInfoPtr pScrn)
{
}
