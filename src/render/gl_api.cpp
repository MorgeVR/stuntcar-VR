// gl_api.cpp - runtime loading of the GL / GLES3 entry points listed in gl_api.h
#include "gl_api.h"
#include <cstring>

#define SCRGL_DEFINE(ret, name, args) PFN_##name name = nullptr;
SCRGL_FUNCTIONS(SCRGL_DEFINE)
#undef SCRGL_DEFINE

PFN_glClearDepthf glClearDepthf_opt = nullptr;
PFN_glClearDepth glClearDepth_opt = nullptr;
PFN_glInvalidateFramebuffer glInvalidateFramebuffer_opt = nullptr;

namespace scrgl {

static bool g_gles = false;
static GetProcFn g_get_proc = nullptr;

void *GetProc(const char *name) { return g_get_proc ? g_get_proc(name) : nullptr; }

bool Load(GetProcFn get_proc, bool gles, const char **missing)
{
    g_gles = gles;
    g_get_proc = get_proc;
    bool ok = true;
#define SCRGL_LOAD(ret, name, args)                                   \
    name = (PFN_##name)get_proc(#name);                               \
    if (!name && ok) { ok = false; if (missing) *missing = #name; }
    SCRGL_FUNCTIONS(SCRGL_LOAD)
#undef SCRGL_LOAD
    glClearDepthf_opt = (PFN_glClearDepthf)get_proc("glClearDepthf");
    glClearDepth_opt = (PFN_glClearDepth)get_proc("glClearDepth");
    glInvalidateFramebuffer_opt = (PFN_glInvalidateFramebuffer)get_proc("glInvalidateFramebuffer");
    return ok;
}

bool IsGLES() { return g_gles; }

bool HasExtension(const char *name)
{
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; i++)
    {
        const char *e = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (e && strcmp(e, name) == 0) return true;
    }
    return false;
}

void ClearDepth(float d)
{
    if (glClearDepthf_opt) glClearDepthf_opt(d);
    else if (glClearDepth_opt) glClearDepth_opt(d);
}

} // namespace scrgl
