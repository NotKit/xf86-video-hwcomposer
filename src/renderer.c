#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "xf86.h"

#include <android-config.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <malloc.h>
#include <sync/sync.h>
#include <hybris/hwcomposerwindow/hwcomposer.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "renderer.h"
#include "hwcomposer.h"

const char vertex_src [] =
    "attribute vec4 position;\n"
    "attribute vec4 texcoords;\n"
    "varying vec2 textureCoordinate;\n"

    "void main()\n"
    "{\n"
    "    gl_Position = position;\n"
    "    textureCoordinate = texcoords.xy;\n"
    "}\n";


const char fragment_src [] =
    "varying highp vec2 textureCoordinate;\n"
    "uniform sampler2D texture;\n"

    "void main()\n"
    "{\n"
    "    gl_FragColor = texture2D(texture, textureCoordinate).bgra;\n"
    "}\n";

GLuint load_shader(const char *shader_source, GLenum type)
{
	GLuint  shader = glCreateShader(type);

	glShaderSource(shader, 1, &shader_source, NULL);
	glCompileShader(shader);

	return shader;
}


GLfloat norm_x    =  0.0;
GLfloat norm_y    =  0.0;
GLfloat offset_x  =  0.0;
GLfloat offset_y  =  0.0;
GLfloat p1_pos_x  =  0.0;
GLfloat p1_pos_y  =  0.0;

//GLint phase_loc;
//GLint offset_loc;
GLint position_loc;
GLint texcoords_loc;
GLint texture_loc;

const float vertexArray[] = {
	0.0,  1.0,  0.0,
	-1.,  0.0,  0.0,
	0.0, -1.0,  0.0,
	1.,  0.0,  0.0,
	0.0,  1.,  0.0
};

static const GLfloat squareVertices[] = {
    -1.0f, -1.0f,
    1.0f, -1.0f,
    -1.0f,  1.0f,
    1.0f,  1.0f,
};

static const GLfloat textureVertices[] = {
    1.0f, 1.0f,
    1.0f, 0.0f,
    0.0f,  1.0f,
    0.0f,  0.0f,
};

void present(void *user_data, struct ANativeWindow *window,
                                   struct ANativeWindowBuffer *buffer)
{
    HWComposer* HWComposer_private = (HWComposer*)user_data;
    hwc_display_contents_1_t **contents = HWComposer_private->hwcContents;
    hwc_layer_1_t *fblayer = HWComposer_private->fblayer;
    hwc_composer_device_1_t *hwcdevice = HWComposer_private->hwcDevicePtr;

    printf("present callback called\n");

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

EGLRenderer *EGLRenderer_Init(ScreenPtr pScreen, void* hwcomposer)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	EGLRenderer *self;

    EGLDisplay display;
	EGLConfig ecfg;
	EGLint num_config;
	EGLint attr[] = {       // some attributes to set up our egl-interface
		EGL_BUFFER_SIZE, 32,
		EGL_RENDERABLE_TYPE,
		EGL_OPENGL_ES2_BIT,
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

	if (!(self = calloc(1, sizeof(EGLRenderer)))) {
		xf86DrvMsg(pScreen->myNum, X_INFO, "EGLRenderer_Init: calloc failed\n");
		return NULL;
	}

	self->hwcomposer = (HWComposer*) hwcomposer;

    struct ANativeWindow *win = HWCNativeWindowCreate(1080, 1920, HAL_PIXEL_FORMAT_RGBA_8888, present, hwcomposer);

    display = eglGetDisplay(NULL);
	assert(eglGetError() == EGL_SUCCESS);
	assert(display != EGL_NO_DISPLAY);
    self->display = (void*)display;

	rv = eglInitialize(display, 0, 0);
	assert(eglGetError() == EGL_SUCCESS);
	assert(rv == EGL_TRUE);

	eglChooseConfig((EGLDisplay) display, attr, &ecfg, 1, &num_config);
	assert(eglGetError() == EGL_SUCCESS);
	assert(rv == EGL_TRUE);

	surface = eglCreateWindowSurface((EGLDisplay) display, ecfg, (EGLNativeWindowType)win, NULL);
	assert(eglGetError() == EGL_SUCCESS);
	assert(surface != EGL_NO_SURFACE);
    self->surface = (void*)surface;

	context = eglCreateContext((EGLDisplay) display, ecfg, EGL_NO_CONTEXT, ctxattr);
	assert(eglGetError() == EGL_SUCCESS);
	assert(context != EGL_NO_CONTEXT);

	assert(eglMakeCurrent((EGLDisplay) display, surface, surface, context) == EGL_TRUE);

	char *version = glGetString(GL_VERSION);
	assert(version);
	printf("%s\n",version);

	GLuint vertexShader   = load_shader ( vertex_src , GL_VERTEX_SHADER  );     // load vertex shader
	GLuint fragmentShader = load_shader ( fragment_src , GL_FRAGMENT_SHADER );  // load fragment shader

	GLuint shaderProgram  = glCreateProgram ();                 // create program object
	glAttachShader ( shaderProgram, vertexShader );             // and attach both...
	glAttachShader ( shaderProgram, fragmentShader );           // ... shaders to it

	glLinkProgram ( shaderProgram );    // link the program
	glUseProgram  ( shaderProgram );    // and select it for usage

	//// now get the locations (kind of handle) of the shaders variables
	position_loc  = glGetAttribLocation  ( shaderProgram , "position" );
	texcoords_loc = glGetAttribLocation  ( shaderProgram , "texcoords" );
	//phase_loc     = glGetUniformLocation ( shaderProgram , "phase"    );
	//offset_loc    = glGetUniformLocation ( shaderProgram , "offset"   );
    texture_loc = glGetUniformLocation ( shaderProgram , "texture" );

	if ( position_loc < 0  ||  texcoords_loc < 0 || texture_loc < 0 ) {
		xf86DrvMsg(pScreen->myNum, X_INFO, "EGLRenderer_Init: failed to get shader variables locations\n");
		return NULL;
    }

    glGenTextures(1, &self->rootTexture);

	glClearColor (1., 1., 1., 1.);    // background color
//	float phase = 0;
//	int i, oldretire = -1, oldrelease = -1, oldrelease2 = -1;
// 	for (i=0; i<30*60; ++i) {
// 		glClear(GL_COLOR_BUFFER_BIT);
// 		glUniform1f ( phase_loc , phase );  // write the value of phase to the shaders phase
// 		phase  =  fmodf ( phase + 0.5f , 2.f * 3.141f );    // and update the local variable
//
// 		glUniform4f ( offset_loc  ,  offset_x , offset_y , 0.0 , 0.0 );
//
// 		glVertexAttribPointer ( position_loc, 3, GL_FLOAT, GL_FALSE, 0, vertexArray );
// 		glEnableVertexAttribArray ( position_loc );
// 		glDrawArrays ( GL_TRIANGLE_STRIP, 0, 5 );
//
// 		eglSwapBuffers ( (EGLDisplay) display, surface );  // get the rendered buffer to the screen
// 	}

	return self;
}

void dump_buffer_to_file(void *vaddr)
{
	static int cnt = 0;

	char b[1024];
	int bytes_pp = 4;

	snprintf(b, 1020, "vaddr.%p.%i.%is%ix%ix%i", vaddr, cnt, 1920, 1920, 1080, bytes_pp);
	cnt++;
	int fd = open(b, O_WRONLY|O_CREAT, S_IRWXU);
	if(fd < 0)
		return;

	write(fd, vaddr, 1920 * 1080 * bytes_pp);
	close(fd);
}

EGLRenderer_Update(void *self, ScreenPtr pScreen)
{
    EGLRenderer* renderer = (EGLRenderer*)self;
    PixmapPtr rootPixmap;

    rootPixmap = pScreen->GetScreenPixmap(pScreen);
    // data rootPixmap->devPrivate.ptr
    printf("pixmap data: %p\n", rootPixmap->devPrivate.ptr);
    //dump_buffer_to_file(rootPixmap->devPrivate.ptr);

    glClear(GL_COLOR_BUFFER_BIT);

    glBindTexture(GL_TEXTURE_2D, renderer->rootTexture);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1920, 1080, 0, GL_RGBA, GL_UNSIGNED_BYTE, rootPixmap->devPrivate.ptr);

    glUniform1i(texture_loc, 0);

    glVertexAttribPointer(position_loc, 2, GL_FLOAT, 0, 0, squareVertices);
    glEnableVertexAttribArray(position_loc);

    glVertexAttribPointer(texcoords_loc, 2, GL_FLOAT, 0, 0, textureVertices);
    glEnableVertexAttribArray(texcoords_loc);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    eglSwapBuffers ((EGLDisplay) renderer->display, (EGLSurface) renderer->surface );  // get the rendered buffer to the screen
    printf("eglSwapBuffers\n;");
}

void EGLRenderer_Close(ScreenPtr pScreen)
{
}
