// renderer.cpp - GL 3.3 / GLES 3.0 renderer for Stunt Car Racer VR.

#include "renderer.h"
#include "font8x8.h"

#include "stuntcarracer.h"
#include "track.h"
#include "car.h"
#include "backdrop.h"
#include "car_behaviour.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

using namespace vm;

namespace scr {

// =====================================================================================
// Shaders
// =====================================================================================
static const char *kSceneVS = R"(
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_col;
layout(location = 2) in vec3 a_uvl;
uniform mat4 u_mvp;
uniform mat4 u_mv;
out vec3 v_col;
out vec3 v_uvl;
out vec3 v_viewpos;
void main()
{
    v_col = a_col.zyx;          // stored as 0xAARRGGBB little-endian (B,G,R,A bytes)
    v_uvl = a_uvl;
    v_viewpos = (u_mv * vec4(a_pos, 1.0)).xyz;
    gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

static const char *kSceneFS = R"(
uniform mediump sampler2DArray u_tex;
uniform vec3 u_fog_col;
uniform vec2 u_fog;      // start, end (metres)
uniform int u_linear;
in vec3 v_col;
in vec3 v_uvl;
in vec3 v_viewpos;
out vec4 o_col;
void main()
{
    vec3 c = v_col;
    if (v_uvl.z >= 0.0)
        c = texture(u_tex, vec3(v_uvl.x, 0.5, v_uvl.z)).rgb;
    float d = length(v_viewpos);
    float f = clamp((d - u_fog.x) / (u_fog.y - u_fog.x), 0.0, 1.0);
    c = mix(c, u_fog_col, f);
    if (u_linear == 1) c = pow(c, vec3(2.2));
    o_col = vec4(c, 1.0);
}
)";

static const char *kUiVS = R"(
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
uniform mat4 u_mvp;
out vec2 v_uv;
void main()
{
    v_uv = a_uv;
    gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

static const char *kUiFS = R"(
uniform sampler2D u_tex;
uniform int u_linear;
uniform float u_opacity;
in vec2 v_uv;
out vec4 o_col;
void main()
{
    vec4 c = texture(u_tex, v_uv);
    if (u_linear == 1) c.rgb = pow(c.rgb, vec3(2.2));
    o_col = vec4(c.rgb, c.a * u_opacity);
}
)";

static const char *kVignetteVS = R"(
out vec2 v_ndc;
void main()
{
    // full screen triangle
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
    v_ndc = p;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

static const char *kVignetteFS = R"(
uniform float u_strength;
in vec2 v_ndc;
out vec4 o_col;
void main()
{
    float r = length(v_ndc);
    float inner = mix(1.25, 0.45, u_strength);
    float a = smoothstep(inner, inner + 0.45, r) * clamp(u_strength * 1.5, 0.0, 1.0);
    o_col = vec4(0.0, 0.0, 0.0, a);
}
)";

static GLuint CompileShader(GLenum type, const char *body)
{
    const char *header = scrgl::IsGLES()
        ? "#version 300 es\nprecision highp float;\nprecision mediump int;\n"
        : "#version 330 core\n";
    const char *src[2] = {header, body};
    GLuint s = glCreateShader(type);
    glShaderSource(s, 2, src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "[StuntCarVR] shader compile failed:\n%s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint LinkProgram(const char *vs, const char *fs)
{
    GLuint v = CompileShader(GL_VERTEX_SHADER, vs), f = CompileShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        fprintf(stderr, "[StuntCarVR] program link failed:\n%s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// =====================================================================================
// Mesh
// =====================================================================================
void Renderer::Mesh::upload(const UTVERTEX *v, int32_t n, bool dynamic)
{
    if (!vao)
    {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(UTVERTEX), (void *)offsetof(UTVERTEX, pos));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(UTVERTEX), (void *)offsetof(UTVERTEX, color));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(UTVERTEX), (void *)offsetof(UTVERTEX, tu));
        glBindVertexArray(0);
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    if (n > capacity)
    {
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof(UTVERTEX)), v, dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
        capacity = n;
    }
    else if (n > 0)
    {
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * sizeof(UTVERTEX)), v);
    }
    count = n;
}

void Renderer::Mesh::draw() const
{
    if (!vao || count <= 0) return;
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, count);
}

void Renderer::Mesh::destroy()
{
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    vao = vbo = 0;
    count = capacity = 0;
}

// =====================================================================================
// Geometry helpers (game units, LH)
// =====================================================================================
static UTVERTEX V(float x, float y, float z, DWORD c)
{
    UTVERTEX v;
    v.pos = D3DXVECTOR3(x, y, z);
    v.color = c;
    v.tu = v.tv = 0.0f;
    v.tex = -1.0f;
    return v;
}

static void Quad(std::vector<UTVERTEX> &o, const vec3 &a, const vec3 &b, const vec3 &c, const vec3 &d, DWORD col)
{
    o.push_back(V(a.x, a.y, a.z, col)); o.push_back(V(b.x, b.y, b.z, col)); o.push_back(V(c.x, c.y, c.z, col));
    o.push_back(V(a.x, a.y, a.z, col)); o.push_back(V(c.x, c.y, c.z, col)); o.push_back(V(d.x, d.y, d.z, col));
}

static void Box(std::vector<UTVERTEX> &o, const vec3 &mn, const vec3 &mx, DWORD side, DWORD top)
{
    vec3 p[8] = {{mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
                 {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}};
    Quad(o, p[0], p[1], p[2], p[3], side);
    Quad(o, p[5], p[4], p[7], p[6], side);
    Quad(o, p[4], p[0], p[3], p[7], side);
    Quad(o, p[1], p[5], p[6], p[2], side);
    Quad(o, p[3], p[2], p[6], p[7], top);
    Quad(o, p[4], p[5], p[1], p[0], side);
}

// Square bar between two points
static void Bar(std::vector<UTVERTEX> &o, const vec3 &a, const vec3 &b, float t, DWORD col, DWORD col2)
{
    vec3 d = normalize(b - a);
    vec3 up = std::fabs(d.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 u = normalize(cross(d, up)) * (t * 0.5f);
    vec3 w = normalize(cross(d, u)) * (t * 0.5f);
    vec3 c[8] = {a - u - w, a + u - w, a + u + w, a - u + w, b - u - w, b + u - w, b + u + w, b - u + w};
    Quad(o, c[0], c[1], c[5], c[4], col);
    Quad(o, c[1], c[2], c[6], c[5], col2);
    Quad(o, c[2], c[3], c[7], c[6], col);
    Quad(o, c[3], c[0], c[4], c[7], col2);
}

std::vector<UTVERTEX> BuildCockpitMesh()
{
    // Car model reference (car.cpp): body x +-40, y -20..40, z +-128; wheels outside the
    // body at x +-(40..81), y -40..0. Driver's eye is at (0, 24, -8) (see game.h).
    std::vector<UTVERTEX> o;
    const DWORD body = SCRGB(SCR_BASE_COLOUR - 13), body_dark = SCRGB(SCR_BASE_COLOUR - 14);  // car colours 1 (reds)
    const DWORD cage = SCRGB(SCR_BASE_COLOUR + 14), cage2 = SCRGB(SCR_BASE_COLOUR + 13);
    const DWORD dark = SCRGB(SCR_BASE_COLOUR - 11), tyre = SCRGB(SCR_BASE_COLOUR - 9), tread = SCRGB(SCR_BASE_COLOUR - 10);

    // bonnet, sloping down toward the front
    Quad(o, {-36, 4, 22}, {36, 4, 22}, {34, -4, 128}, {-34, -4, 128}, body);
    // bonnet sides
    Quad(o, {-36, 4, 22}, {-34, -4, 128}, {-38, -20, 128}, {-40, -20, 22}, body_dark);
    Quad(o, {36, 4, 22}, {40, -20, 22}, {38, -20, 128}, {34, -4, 128}, body_dark);
    // nose
    Quad(o, {-34, -4, 128}, {34, -4, 128}, {38, -20, 128}, {-38, -20, 128}, body_dark);
    // dashboard / scuttle
    Box(o, {-36, -6, 18}, {36, 8, 24}, SCRGB(SCR_BASE_COLOUR - 10), SCRGB(SCR_BASE_COLOUR - 9));
    // side sills either side of the driver
    Box(o, {-40, -20, -128}, {-34, 6, 22}, body, body_dark);
    Box(o, {34, -20, -128}, {40, 6, 22}, body, body_dark);
    // floor and rear deck
    Quad(o, {-34, -14, -40}, {34, -14, -40}, {34, -14, 22}, {-34, -14, 22}, dark);
    Quad(o, {-36, 6, -128}, {36, 6, -128}, {36, 6, -34}, {-36, 6, -34}, body);
    // seat back
    Box(o, {-18, -14, -34}, {18, 30, -28}, dark, dark);

    // roll cage: front A-pillars, rear hoop, roof rails and cross bars
    const float t = 2.6f;
    Bar(o, {-34, 6, 20}, {-30, 40, -2}, t, cage, cage2);
    Bar(o, {34, 6, 20}, {30, 40, -2}, t, cage, cage2);
    Bar(o, {-34, 6, -34}, {-33, 40, -34}, t, cage, cage2);
    Bar(o, {34, 6, -34}, {33, 40, -34}, t, cage, cage2);
    Bar(o, {-30, 40, -2}, {-33, 40, -34}, t, cage, cage2);
    Bar(o, {30, 40, -2}, {33, 40, -34}, t, cage, cage2);
    Bar(o, {-30, 40, -2}, {30, 40, -2}, t, cage, cage2);
    Bar(o, {-33, 40, -34}, {33, 40, -34}, t, cage, cage2);
    Bar(o, {-33, 40, -34}, {33, 6, -34}, t, cage, cage2);   // diagonal brace

    // wheels: chunky blocks, matching the car model's wheel positions
    for (int side = -1; side <= 1; side += 2)
    {
        float x0 = side < 0 ? -81.0f : 44.0f, x1 = side < 0 ? -44.0f : 81.0f;
        Box(o, {x0, -40, 100}, {x1, 0, 140}, tyre, tread);    // front
        Box(o, {x0, -40, -140}, {x1, 0, -100}, tyre, tread);  // rear
        // suspension arms from body to wheels
        Bar(o, {side * 38.0f, -12, 116}, {side * 50.0f, -20, 120}, 3.0f, dark, dark);
        Bar(o, {side * 38.0f, -12, -116}, {side * 50.0f, -20, -120}, 3.0f, dark, dark);
    }
    return o;
}

static std::vector<UTVERTEX> BuildSteeringWheel()
{
    // Wheel in its own frame: centred at origin, in the x/y plane, facing -z (the driver).
    std::vector<UTVERTEX> o;
    const DWORD rim = SCRGB(0), spoke = SCRGB(SCR_BASE_COLOUR + 14), mark = SCRGB(SCR_BASE_COLOUR + 3);
    const int n = 20;
    const float r = 9.0f;
    for (int i = 0; i < n; i++)
    {
        float a0 = (float)i / n * 2 * kPi, a1 = (float)(i + 1) / n * 2 * kPi;
        bool top = (i == n / 4 - 1);   // yellow marker at 12 o'clock
        Bar(o, {std::cos(a0) * r, std::sin(a0) * r, 0}, {std::cos(a1) * r, std::sin(a1) * r, 0}, 1.6f,
            top ? mark : rim, top ? mark : rim);
    }
    Bar(o, {-r, 0, 0}, {r, 0, 0}, 1.0f, spoke, spoke);
    Bar(o, {0, 0, 0}, {0, -r, 0}, 1.0f, spoke, spoke);
    Bar(o, {0, 0, 0}, {0, 0, 8}, 2.0f, rim, rim);   // column
    return o;
}

// =====================================================================================
// Road textures (24-bit BMP, the six originals)
// =====================================================================================
static bool LoadBMP(const char *path, std::vector<uint8_t> &rgba, int &w, int &h)
{
    size_t size = 0;
    uint8_t *d = (uint8_t *)ScrLoadAsset(path, &size);
    if (!d || size < 54 || d[0] != 'B' || d[1] != 'M') { free(d); return false; }
    uint32_t off = d[10] | (d[11] << 8) | (d[12] << 16) | (d[13] << 24);
    int32_t bw = d[18] | (d[19] << 8) | (d[20] << 16) | (d[21] << 24);
    int32_t bh = d[22] | (d[23] << 8) | (d[24] << 16) | (d[25] << 24);
    int bpp = d[28] | (d[29] << 8);
    if (bpp != 24 || bw <= 0) { free(d); return false; }
    bool bottom_up = bh > 0;
    bh = bh < 0 ? -bh : bh;
    size_t stride = ((size_t)bw * 3 + 3) & ~(size_t)3;
    if (off + stride * bh > size) { free(d); return false; }
    w = bw; h = bh;
    rgba.resize((size_t)w * h * 4);
    for (int y = 0; y < h; y++)
    {
        const uint8_t *row = d + off + stride * (bottom_up ? (h - 1 - y) : y);
        for (int x = 0; x < w; x++)
        {
            uint8_t *p = &rgba[((size_t)y * w + x) * 4];
            p[0] = row[x * 3 + 2]; p[1] = row[x * 3 + 1]; p[2] = row[x * 3 + 0]; p[3] = 255;
        }
    }
    free(d);
    return true;
}

// =====================================================================================
// TextPanel
// =====================================================================================
void TextPanel::create(int w, int h)
{
    m_w = w; m_h = h;
    m_pixels.assign((size_t)w * h, 0);
    m_dirty = true;
}

void TextPanel::clear(uint32_t rgba) { std::fill(m_pixels.begin(), m_pixels.end(), rgba); m_dirty = true; }

void TextPanel::fillRect(int x, int y, int w, int h, uint32_t rgba)
{
    for (int yy = std::max(0, y); yy < std::min(m_h, y + h); yy++)
        for (int xx = std::max(0, x); xx < std::min(m_w, x + w); xx++)
            m_pixels[(size_t)yy * m_w + xx] = rgba;
    m_dirty = true;
}

int TextPanel::text(int x, int y, const std::string &s, int scale, uint32_t rgba)
{
    int cx = x;
    for (char ch : s)
    {
        if (ch >= 33 && ch <= 126)
        {
            const uint8_t *g = kFont8x8[ch - 33];
            for (int r = 0; r < 8; r++)
                for (int b = 0; b < 8; b++)
                    if ((g[r] >> b) & 1)
                        fillRect(cx + b * scale, y + r * scale, scale, scale, rgba);
        }
        cx += 8 * scale;
    }
    m_dirty = true;
    return cx - x;
}

void TextPanel::upload()
{
    if (!m_tex)
    {
        glGenTextures(1, &m_tex);
        glBindTexture(GL_TEXTURE_2D, m_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_w, m_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        m_dirty = true;
    }
    if (!m_dirty) return;
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_w, m_h, GL_RGBA, GL_UNSIGNED_BYTE, m_pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    m_dirty = false;
}

void TextPanel::destroy()
{
    if (m_tex) glDeleteTextures(1, &m_tex);
    m_tex = 0;
}

// =====================================================================================
// Renderer
// =====================================================================================
static Renderer::Mesh *g_wheel_mesh = nullptr;   // steering wheel (owned by Renderer below)

bool Renderer::init(const Space &space)
{
    m_space = space;
    m_prog = LinkProgram(kSceneVS, kSceneFS);
    m_ui_prog = LinkProgram(kUiVS, kUiFS);
    m_vig_prog = LinkProgram(kVignetteVS, kVignetteFS);
    if (!m_prog || !m_ui_prog || !m_vig_prog) return false;

    u_mvp = glGetUniformLocation(m_prog, "u_mvp");
    u_mv = glGetUniformLocation(m_prog, "u_mv");
    u_fog_col = glGetUniformLocation(m_prog, "u_fog_col");
    u_fog = glGetUniformLocation(m_prog, "u_fog");
    u_linear = glGetUniformLocation(m_prog, "u_linear");
    u_tex = glGetUniformLocation(m_prog, "u_tex");
    u_ui_mvp = glGetUniformLocation(m_ui_prog, "u_mvp");
    u_ui_tex = glGetUniformLocation(m_ui_prog, "u_tex");
    u_ui_linear = glGetUniformLocation(m_ui_prog, "u_linear");
    u_ui_opacity = glGetUniformLocation(m_ui_prog, "u_opacity");
    u_vig_strength = glGetUniformLocation(m_vig_prog, "u_strength");

    // Road textures as a 2D array (layer = SegmentRoadTexture index)
    static const char *names[6] = {"Bitmap/RoadYellowDark.bmp", "Bitmap/RoadYellowLight.bmp", "Bitmap/RoadRedDark.bmp",
                                   "Bitmap/RoadRedLight.bmp", "Bitmap/RoadBlack.bmp", "Bitmap/RoadWhite.bmp"};
    std::vector<uint8_t> all, one;
    int tw = 0, th = 0;
    for (int i = 0; i < 6; i++)
    {
        int w = 0, h = 0;
        if (!LoadBMP(names[i], one, w, h))
        {
            fprintf(stderr, "[StuntCarVR] missing road texture %s, using flat colour\n", names[i]);
            w = 4; h = 1; one.assign(16, 128);
        }
        if (i == 0) { tw = w; th = h; all.resize((size_t)tw * th * 4 * 6); }
        if (w != tw || h != th) { one.assign((size_t)tw * th * 4, 128); }
        memcpy(&all[(size_t)i * tw * th * 4], one.data(), (size_t)tw * th * 4);
    }
    glGenTextures(1, &m_road_tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_road_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, tw, th, 6, 0, GL_RGBA, GL_UNSIGNED_BYTE, all.data());
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

    // UI quad buffer: pos(3) + uv(2)
    glGenVertexArrays(1, &m_ui_vao);
    glGenBuffers(1, &m_ui_vbo);
    glBindVertexArray(m_ui_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_ui_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 5 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(3 * sizeof(float)));
    glGenVertexArrays(1, &m_vig_vao);
    glBindVertexArray(0);

    buildStaticMeshes();
    sync();
    return true;
}

void Renderer::buildStaticMeshes()
{
    int32_t n = 0;
    const UTVERTEX *car = GetCarVertices(&n);
    m_car.upload(car, n, false);

    std::vector<UTVERTEX> cockpit = BuildCockpitMesh();
    m_cockpit.upload(cockpit.data(), (int32_t)cockpit.size(), false);

    static Mesh wheel;
    std::vector<UTVERTEX> w = BuildSteeringWheel();
    wheel.upload(w.data(), (int32_t)w.size(), false);
    g_wheel_mesh = &wheel;

    // Ground: a large plane at the track base (y = 0) centred on the world
    const float c = (float)(NUM_TRACK_CUBES * (CUBE_SIZE >> LOG_PRECISION)) * 0.5f;
    const float r = 320000.0f;
    const DWORD g = SCRGB(SCR_BASE_COLOUR + 13);   // GROUND_COLOUR in backdrop.cpp
    std::vector<UTVERTEX> ground;
    Quad(ground, {c - r, -2, c - r}, {c - r, -2, c + r}, {c + r, -2, c + r}, {c + r, -2, c - r}, g);
    m_ground.upload(ground.data(), (int32_t)ground.size(), false);

    // Diorama table: a slab under the whole track area (used by the VR menu)
    const float t = (float)(NUM_TRACK_CUBES * (CUBE_SIZE >> LOG_PRECISION));
    const float m = t * 0.04f, th = t * 0.03f;
    std::vector<UTVERTEX> table;
    const DWORD top = SCRGB(SCR_BASE_COLOUR + 5), edge = SCRGB(SCR_BASE_COLOUR - 11);
    Box(table, {-m, -th, -m}, {t + m, -4, t + m}, edge, top);
    m_table.upload(table.data(), (int32_t)table.size(), false);

    // Controller blocks in centimetres (grip pose: origin in the palm, -z forward)
    for (int g = 0; g < 2; g++)
    {
        std::vector<UTVERTEX> c;
        DWORD body = SCRGB(SCR_BASE_COLOUR - 10), top = g ? SCRGB(SCR_BASE_COLOUR + 3) : SCRGB(SCR_BASE_COLOUR + 14);
        Box(c, {-2.0f, -2.5f, -7.0f}, {2.0f, 2.0f, 4.0f}, body, top);
        Box(c, {-0.6f, -0.6f, -11.0f}, {0.6f, 0.6f, -7.0f}, top, top);   // pointer nub
        (g ? m_ctrl_grip : m_ctrl).upload(c.data(), (int32_t)c.size(), false);
    }
}

void Renderer::sync()
{
    if (GetTrackMeshVersion() != m_track_version)
    {
        int32_t n = 0;
        const UTVERTEX *v = GetTrackVertices(&n);
        m_track.upload(v, n, true);
        m_track_version = GetTrackMeshVersion();
    }
    if (GetSceneryType() != m_scenery_type)
    {
        static UTVERTEX buf[32 * 4 * 6 * 3];
        const float c = (float)(NUM_TRACK_CUBES * (CUBE_SIZE >> LOG_PRECISION)) * 0.5f;
        int32_t n = BuildSceneryMesh(c, c, 260000.0f, buf, (int32_t)(sizeof(buf) / sizeof(buf[0])));
        m_scenery.upload(buf, n, false);
        m_scenery_type = GetSceneryType();
    }
    int32_t ns = 0;
    const UTVERTEX *sv = GetShadowVertices(&ns);
    m_shadow.upload(sv, ns, true);
}

void Renderer::shutdown()
{
    m_track.destroy(); m_table.destroy(); m_ctrl.destroy(); m_ctrl_grip.destroy(); m_car.destroy(); m_cockpit.destroy(); m_ground.destroy(); m_scenery.destroy(); m_shadow.destroy();
    if (g_wheel_mesh) g_wheel_mesh->destroy();
    if (m_road_tex) glDeleteTextures(1, &m_road_tex);
    if (m_ui_vbo) glDeleteBuffers(1, &m_ui_vbo);
    if (m_ui_vao) glDeleteVertexArrays(1, &m_ui_vao);
    if (m_vig_vao) glDeleteVertexArrays(1, &m_vig_vao);
    if (m_prog) glDeleteProgram(m_prog);
    if (m_ui_prog) glDeleteProgram(m_ui_prog);
    if (m_vig_prog) glDeleteProgram(m_vig_prog);
    m_prog = m_ui_prog = m_vig_prog = 0;
}

vec3 Renderer::skyColour(bool linear) const
{
    DWORD c = SCRGB(SCR_BASE_COLOUR + 7);   // SKY_COLOUR in backdrop.cpp
    vec3 s(((c >> 16) & 0xff) / 255.0f, ((c >> 8) & 0xff) / 255.0f, (c & 0xff) / 255.0f);
    if (linear) s = vec3(std::pow(s.x, 2.2f), std::pow(s.y, 2.2f), std::pow(s.z, 2.2f));
    return s;
}

void Renderer::drawMesh(const Mesh &m, const mat4 &model, const View &v, const SceneParams &p, bool cull)
{
    if (m.count <= 0) return;
    mat4 mv = v.view * model;
    mat4 mvp = v.proj * mv;
    glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp.m);
    glUniformMatrix4fv(u_mv, 1, GL_FALSE, mv.m);
    if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    m.draw();
    (void)p;
}

void Renderer::drawScene(const View &v, const SceneParams &p)
{
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    // Original geometry is Direct3D left-handed data with D3DCULL_CCW (front faces are
    // clockwise as seen by the viewer). The z-mirror into the right-handed world keeps
    // the picture identical, so front faces remain clockwise on screen.
    glFrontFace(m_front_cw ? GL_CW : GL_CCW);
    glCullFace(GL_BACK);

    glUseProgram(m_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_road_tex);
    glUniform1i(u_tex, 0);
    vec3 sky = skyColour(false);
    glUniform3f(u_fog_col, sky.x, sky.y, sky.z);
    glUniform2f(u_fog, p.fog_start, p.fog_end);
    glUniform1i(u_linear, p.linear_output ? 1 : 0);

    const mat4 &g2w = p.game_to_world;

    if (p.draw_ground) drawMesh(m_ground, g2w, v, p, false);
    if (p.draw_table) drawMesh(m_table, g2w, v, p, false);
    if (p.draw_scenery)
    {
        // light aerial haze only: the original backdrop had no fog at all
        glUniform2f(u_fog, p.fog_start, p.fog_end * 3.0f);
        drawMesh(m_scenery, g2w, v, p, false);
        glUniform2f(u_fog, p.fog_start, p.fog_end);
    }
    if (p.draw_track) drawMesh(m_track, g2w, v, p, true);

    if (p.draw_shadow && p.draw_opponent && m_shadow.count > 0)
    {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-2.0f, -4.0f);
        drawMesh(m_shadow, g2w, v, p, false);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    // Cars: object-local game units -> game space pose -> world (through g2w, which may be the diorama)
    auto carModel = [&](const vec3 &pos, const quat &rot) { return g2w * mat4::translation(pos) * mat4::rotation(rot); };
    auto carModelScaled = [&](const vec3 &pos, const quat &rot) { return carModel(pos, rot) * mat4::scale({p.car_scale, p.car_scale, p.car_scale}); };
    if (p.draw_opponent) drawMesh(m_car, carModelScaled(p.opponent_pos, p.opponent_rot), v, p, true);
    if (p.draw_player_car) drawMesh(m_car, carModelScaled(p.player_pos, p.player_rot), v, p, true);
    if (p.draw_cockpit)
    {
        mat4 car = carModel(p.player_pos, p.player_rot);
        drawMesh(m_cockpit, car, v, p, false);
        if (g_wheel_mesh)
        {
            // steering wheel in front of the driver, tilted back, turned by the steering input
            mat4 wheel = car * mat4::translation({0.0f, 9.0f, 12.0f}) *
                         mat4::rotation(quat::axisAngle({1, 0, 0}, 0.35f)) *   // top leans away from the driver
                         mat4::rotation(quat::axisAngle({0, 0, 1}, -m_steer_angle));
            drawMesh(*g_wheel_mesh, wheel, v, p, false);
        }
    }
    glBindVertexArray(0);

    // Dashboard HUD panel, mounted on the scuttle and tilted toward the driver
    if (p.draw_cockpit && p.dashboard)
    {
        mat4 car = carModel(p.player_pos, p.player_rot);
        const vec3 c_local(0.0f, 14.0f, 25.0f);
        vec3 centre = car.transformPoint(c_local);
        vec3 xa = car.transformPoint(c_local + vec3(1, 0, 0)) - centre;
        vec3 ya = car.transformPoint(c_local + vec3(0, 1, -0.45f)) - centre;
        float units_to_m = length(xa);   // metres per game unit (diorama aware)
        xa = normalize(xa);
        ya = normalize(ya);
        vec3 za = normalize(cross(xa, ya));   // world RH: right x up = toward the driver
        mat4 r;
        r(0, 0) = xa.x; r(1, 0) = xa.y; r(2, 0) = xa.z;
        r(0, 1) = ya.x; r(1, 1) = ya.y; r(2, 1) = ya.z;
        r(0, 2) = za.x; r(1, 2) = za.y; r(2, 2) = za.z;
        drawPanelMatrix(v, *p.dashboard, mat4::translation(centre) * r, 30.0f * units_to_m, 9.4f * units_to_m,
                        p.linear_output, 1.0f);
    }
}

void Renderer::drawPanelMatrix(const View &v, const TextPanel &panel, const mat4 &model, float w, float h,
                               bool linear_output, float opacity)
{
    if (!panel.texture()) return;
    const float hw = w * 0.5f, hh = h * 0.5f;
    // top-left of the canvas is v = 0
    const float q[6][5] = {{-hw, -hh, 0, 0, 1}, {hw, -hh, 0, 1, 1}, {hw, hh, 0, 1, 0},
                           {-hw, -hh, 0, 0, 1}, {hw, hh, 0, 1, 0}, {-hw, hh, 0, 0, 0}};
    glUseProgram(m_ui_prog);
    mat4 mvp = v.proj * v.view * model;
    glUniformMatrix4fv(u_ui_mvp, 1, GL_FALSE, mvp.m);
    glUniform1i(u_ui_tex, 0);
    glUniform1i(u_ui_linear, linear_output ? 1 : 0);
    glUniform1f(u_ui_opacity, opacity);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, panel.texture());
    glBindVertexArray(m_ui_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_ui_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(q), q);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

void Renderer::drawPanel(const View &v, const TextPanel &panel, const Pose &pose, float width_m, float height_m,
                         bool linear_output, float opacity)
{
    drawPanelMatrix(v, panel, pose.matrix(), width_m, height_m, linear_output, opacity);
}

void Renderer::drawPanel2D(const TextPanel &panel, int x, int y, int w, int h, int fb_w, int fb_h, bool linear_output)
{
    View v;
    v.proj = mat4::ortho(0.0f, (float)fb_w, (float)fb_h, 0.0f, -1.0f, 1.0f);   // y down, pixels
    v.view = mat4();
    // quad local coords are centred; flip y because the 2D projection is y-down
    mat4 model = mat4::translation({x + w * 0.5f, y + h * 0.5f, 0.0f}) * mat4::scale({1.0f, -1.0f, 1.0f});
    glDisable(GL_DEPTH_TEST);
    drawPanelMatrix(v, panel, model, (float)w, (float)h, linear_output, 1.0f);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawControllers(const View &v, const Pose *poses, const bool *valid, const bool *gripping, bool linear_output)
{
    glUseProgram(m_prog);
    glUniform2f(u_fog, 1.0e6f, 2.0e6f);
    glUniform1i(u_linear, linear_output ? 1 : 0);
    glEnable(GL_DEPTH_TEST);
    SceneParams p;
    for (int h = 0; h < 2; h++)
    {
        if (!valid[h]) continue;
        mat4 model = poses[h].matrix() * mat4::scale({0.01f, 0.01f, 0.01f});
        drawMesh(gripping[h] ? m_ctrl_grip : m_ctrl, model, v, p, false);
    }
    glBindVertexArray(0);
}

void Renderer::drawVignette(float strength)
{
    if (strength <= 0.01f) return;
    glUseProgram(m_vig_prog);
    glUniform1f(u_vig_strength, std::min(1.0f, strength));
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(m_vig_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

} // namespace scr
