// gl_api.h - minimal self-contained OpenGL / OpenGL ES 3 loader.
//
// The renderer targets the common subset of OpenGL 3.3 core and OpenGL ES 3.0, so the
// same code runs on Windows/Linux (PCVR, flat mode) and on Meta Quest. Functions are
// loaded at runtime through a platform GetProcAddress (SDL_GL_GetProcAddress or
// eglGetProcAddress), so no system GL headers are needed.
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define SCRGL_APIENTRY __stdcall
#else
#define SCRGL_APIENTRY
#endif

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef char GLchar;
typedef unsigned char GLubyte;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

// Constants -----------------------------------------------------------------------------
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_LINES 0x0001
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#define GL_MULTISAMPLE 0x809D
#define GL_LEQUAL 0x0203
#define GL_LESS 0x0201
#define GL_ALWAYS 0x0207
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE 1
#define GL_ZERO 0
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_RGB 0x1907
#define GL_RED 0x1903
#define GL_RGBA8 0x8058
#define GL_SRGB8_ALPHA8 0x8C43
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_COMPONENT16 0x81A5
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_NUM_EXTENSIONS 0x821D
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_REPEAT 0x2901
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_STREAM_DRAW 0x88E0
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_MAX_SAMPLES 0x8D57

#define SCRGL_FUNCTIONS(X) \
    X(const GLubyte *, glGetString, (GLenum name)) \
    X(const GLubyte *, glGetStringi, (GLenum name, GLuint index)) \
    X(void, glGetIntegerv, (GLenum pname, GLint *data)) \
    X(GLenum, glGetError, (void)) \
    X(void, glEnable, (GLenum cap)) \
    X(void, glDisable, (GLenum cap)) \
    X(void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h)) \
    X(void, glScissor, (GLint x, GLint y, GLsizei w, GLsizei h)) \
    X(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
    X(void, glClear, (GLbitfield mask)) \
    X(void, glCullFace, (GLenum mode)) \
    X(void, glFrontFace, (GLenum mode)) \
    X(void, glDepthFunc, (GLenum func)) \
    X(void, glDepthMask, (GLboolean flag)) \
    X(void, glColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a)) \
    X(void, glBlendFunc, (GLenum s, GLenum d)) \
    X(void, glPolygonOffset, (GLfloat factor, GLfloat units)) \
    X(void, glFinish, (void)) \
    X(void, glFlush, (void)) \
    X(void, glPixelStorei, (GLenum pname, GLint param)) \
    X(void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *data)) \
    X(GLuint, glCreateShader, (GLenum type)) \
    X(void, glShaderSource, (GLuint s, GLsizei count, const GLchar *const *str, const GLint *len)) \
    X(void, glCompileShader, (GLuint s)) \
    X(void, glGetShaderiv, (GLuint s, GLenum pname, GLint *params)) \
    X(void, glGetShaderInfoLog, (GLuint s, GLsizei max, GLsizei *len, GLchar *log)) \
    X(void, glDeleteShader, (GLuint s)) \
    X(GLuint, glCreateProgram, (void)) \
    X(void, glAttachShader, (GLuint p, GLuint s)) \
    X(void, glBindAttribLocation, (GLuint p, GLuint index, const GLchar *name)) \
    X(void, glLinkProgram, (GLuint p)) \
    X(void, glGetProgramiv, (GLuint p, GLenum pname, GLint *params)) \
    X(void, glGetProgramInfoLog, (GLuint p, GLsizei max, GLsizei *len, GLchar *log)) \
    X(void, glUseProgram, (GLuint p)) \
    X(void, glDeleteProgram, (GLuint p)) \
    X(GLint, glGetUniformLocation, (GLuint p, const GLchar *name)) \
    X(void, glUniform1i, (GLint loc, GLint v)) \
    X(void, glUniform1f, (GLint loc, GLfloat v)) \
    X(void, glUniform2f, (GLint loc, GLfloat a, GLfloat b)) \
    X(void, glUniform3f, (GLint loc, GLfloat a, GLfloat b, GLfloat c)) \
    X(void, glUniform4f, (GLint loc, GLfloat a, GLfloat b, GLfloat c, GLfloat d)) \
    X(void, glUniformMatrix4fv, (GLint loc, GLsizei count, GLboolean transpose, const GLfloat *v)) \
    X(void, glGenBuffers, (GLsizei n, GLuint *b)) \
    X(void, glDeleteBuffers, (GLsizei n, const GLuint *b)) \
    X(void, glBindBuffer, (GLenum target, GLuint b)) \
    X(void, glBufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage)) \
    X(void, glBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void *data)) \
    X(void, glGenVertexArrays, (GLsizei n, GLuint *a)) \
    X(void, glDeleteVertexArrays, (GLsizei n, const GLuint *a)) \
    X(void, glBindVertexArray, (GLuint a)) \
    X(void, glEnableVertexAttribArray, (GLuint index)) \
    X(void, glVertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean norm, GLsizei stride, const void *ptr)) \
    X(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count)) \
    X(void, glGenTextures, (GLsizei n, GLuint *t)) \
    X(void, glDeleteTextures, (GLsizei n, const GLuint *t)) \
    X(void, glBindTexture, (GLenum target, GLuint t)) \
    X(void, glActiveTexture, (GLenum unit)) \
    X(void, glTexParameteri, (GLenum target, GLenum pname, GLint param)) \
    X(void, glTexImage2D, (GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum fmt, GLenum type, const void *data)) \
    X(void, glTexSubImage2D, (GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, const void *data)) \
    X(void, glTexImage3D, (GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLsizei d, GLint border, GLenum fmt, GLenum type, const void *data)) \
    X(void, glGenerateMipmap, (GLenum target)) \
    X(void, glGenFramebuffers, (GLsizei n, GLuint *f)) \
    X(void, glDeleteFramebuffers, (GLsizei n, const GLuint *f)) \
    X(void, glBindFramebuffer, (GLenum target, GLuint f)) \
    X(void, glFramebufferTexture2D, (GLenum target, GLenum attach, GLenum textarget, GLuint tex, GLint level)) \
    X(void, glFramebufferRenderbuffer, (GLenum target, GLenum attach, GLenum rbtarget, GLuint rb)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum target)) \
    X(void, glGenRenderbuffers, (GLsizei n, GLuint *r)) \
    X(void, glDeleteRenderbuffers, (GLsizei n, const GLuint *r)) \
    X(void, glBindRenderbuffer, (GLenum target, GLuint r)) \
    X(void, glRenderbufferStorage, (GLenum target, GLenum ifmt, GLsizei w, GLsizei h)) \
    X(void, glBlitFramebuffer, (GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter))

#define SCRGL_DECLARE(ret, name, args) typedef ret(SCRGL_APIENTRY *PFN_##name) args; extern PFN_##name name;
SCRGL_FUNCTIONS(SCRGL_DECLARE)
#undef SCRGL_DECLARE

// Optional functions (may be null)
typedef void(SCRGL_APIENTRY *PFN_glClearDepthf)(GLfloat d);
typedef void(SCRGL_APIENTRY *PFN_glClearDepth)(double d);
typedef void(SCRGL_APIENTRY *PFN_glInvalidateFramebuffer)(GLenum target, GLsizei n, const GLenum *attachments);
extern PFN_glClearDepthf glClearDepthf_opt;
extern PFN_glClearDepth glClearDepth_opt;
extern PFN_glInvalidateFramebuffer glInvalidateFramebuffer_opt;

namespace scrgl {

typedef void *(*GetProcFn)(const char *name);

// Load all functions. `gles` selects GLSL ES shader headers. Returns false if a
// required function is missing (the name is written to `missing`).
bool Load(GetProcFn get_proc, bool gles, const char **missing = nullptr);
bool IsGLES();
void *GetProc(const char *name);   // the loader passed to Load()
bool HasExtension(const char *name);
void ClearDepth(float d);

} // namespace scrgl
