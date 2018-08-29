#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "driver.h"

void hwc_ortho_2d(float* mat, float left, float right, float bottom, float top)
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

/* The following code is based on
 * https://github.com/swaywm/wlroots/blob/master/render/gles2/renderer.c */
static GLuint compile_shader(GLuint type, const GLchar *src) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);

	GLint ok;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (ok == GL_FALSE) {
		glDeleteShader(shader);
		shader = 0;
	}
	return shader;
}

GLuint hwc_link_program(const GLchar *vert_src, const GLchar *frag_src) {
	GLuint vert = compile_shader(GL_VERTEX_SHADER, vert_src);
	if (!vert) {
		return 0;
	}

	GLuint frag = compile_shader(GL_FRAGMENT_SHADER, frag_src);
	if (!frag) {
		glDeleteShader(vert);
		return 0;
	}

	GLuint prog = glCreateProgram();
	glAttachShader(prog, vert);
	glAttachShader(prog, frag);
	glLinkProgram(prog);

	glDetachShader(prog, vert);
	glDetachShader(prog, frag);
	glDeleteShader(vert);
	glDeleteShader(frag);

	GLint ok;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (ok == GL_FALSE) {
		glDeleteProgram(prog);
		return 0;
	}

	return prog;
}
