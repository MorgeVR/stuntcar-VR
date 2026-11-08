// renderer.h - OpenGL 3.3 / OpenGL ES 3.0 renderer for Stunt Car Racer VR.
//
// All game geometry is stored in the original left-handed game units and drawn in
// a right-handed metric world through scr::Space (see space.h). The same renderer
// draws flat-screen views and each eye of a VR frame.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "gl_api.h"
#include "vmath.h"
#include "scr_compat.h"

namespace scr {

// Mapping between game units (LH, +z forward) and world metres (RH, -z forward).
struct Space
{
    float metres_per_unit = 1.0f / 64.0f;

    vm::vec3 toWorld(const vm::vec3 &g) const { return {g.x * metres_per_unit, g.y * metres_per_unit, -g.z * metres_per_unit}; }
    static vm::quat rotToWorld(const vm::quat &q) { return {-q.x, -q.y, q.z, q.w}; }
    // Matrix taking game-unit points to world metres (includes the z mirror).
    vm::mat4 gameToWorld() const { return vm::mat4::scale({metres_per_unit, metres_per_unit, -metres_per_unit}); }
    // Model matrix for a mesh in object-local game units placed at a game-space pose.
    vm::mat4 objectToWorld(const vm::vec3 &pos_game, const vm::quat &rot_game) const
    {
        return vm::mat4::translation(toWorld(pos_game)) * vm::mat4::rotation(rotToWorld(rot_game)) *
               vm::mat4::scale({metres_per_unit, metres_per_unit, -metres_per_unit});
    }
    vm::Pose poseToWorld(const vm::vec3 &pos_game, const vm::quat &rot_game) const
    {
        return {toWorld(pos_game), rotToWorld(rot_game)};
    }
};

struct View
{
    vm::mat4 view;   // world -> eye
    vm::mat4 proj;
};

// A CPU RGBA canvas with 8x8 bitmap text, uploaded to a texture for HUD / menu panels.
class TextPanel
{
public:
    void create(int w, int h);
    void clear(uint32_t rgba);
    void fillRect(int x, int y, int w, int h, uint32_t rgba);
    // Draw text with pixel scale; returns width in pixels.
    int text(int x, int y, const std::string &s, int scale, uint32_t rgba);
    static int textWidth(const std::string &s, int scale) { return (int)s.size() * 8 * scale; }
    void upload();   // to GL texture (creates it on first call)
    GLuint texture() const { return m_tex; }
    int width() const { return m_w; }
    int height() const { return m_h; }
    void destroy();

private:
    std::vector<uint32_t> m_pixels;
    int m_w = 0, m_h = 0;
    GLuint m_tex = 0;
    bool m_dirty = true;
};

struct SceneParams
{
    // What to draw ---------------------------------------------------------------
    bool draw_track = true;
    bool draw_player_car = false;     // outside view / diorama
    bool draw_cockpit = false;        // VR or flat cockpit
    bool draw_opponent = false;
    bool draw_ground = true;
    bool draw_scenery = true;
    bool draw_shadow = true;
    bool draw_table = false;          // diorama table under the track (VR menu)

    // Transform from game units to world (normally Space::gameToWorld(); the VR menu
    // uses a scaled-down "diorama" transform instead).
    vm::mat4 game_to_world;

    // Car poses in game units (LH)
    vm::vec3 player_pos, opponent_pos;
    vm::quat player_rot, opponent_rot;

    // Optional HUD panel on the dashboard (VR cockpit)
    const TextPanel *dashboard = nullptr;

    bool linear_output = false;       // true when rendering into an sRGB swapchain
    float fog_start = 1500.0f, fog_end = 6000.0f;  // metres
    float car_scale = 1.0f;           // >1 makes cars visible as toys on the diorama
};

class Renderer
{
public:
    bool init(const Space &space);
    void shutdown();

    // Re-upload track / scenery meshes if they changed (Draw Bridge, track switch).
    void sync();

    // Draw the 3D scene into the currently bound framebuffer / viewport.
    void drawScene(const View &v, const SceneParams &p);

    // Draw a textured panel (HUD, menu) as a quad in world space.
    // `pose` is the panel centre; it faces +z (toward a viewer at +z).
    void drawPanel(const View &v, const TextPanel &panel, const vm::Pose &pose, float width_m, float height_m,
                   bool linear_output, float opacity = 1.0f);
    // Draw a panel in screen space (flat mode overlay), pixel coordinates from top-left.
    void drawPanel2D(const TextPanel &panel, int x, int y, int w, int h, int fb_w, int fb_h, bool linear_output);

    // Comfort vignette: darkens the periphery. strength 0..1.
    void drawVignette(float strength);

    // VR controllers as simple blocks at world-space grip poses (yellow while gripping).
    void drawControllers(const View &v, const vm::Pose *poses, const bool *valid, const bool *gripping, bool linear_output);

    vm::vec3 skyColour(bool linear) const;
    const Space &space() const { return m_space; }
    void setSpace(const Space &s) { m_space = s; }
    void setFrontFaceCW(bool cw) { m_front_cw = cw; }
    // Rotation of the cockpit steering wheel (radians, positive = clockwise / right).
    void setSteeringWheelAngle(float radians) { m_steer_angle = radians; }

    struct Mesh
    {
        GLuint vao = 0, vbo = 0;
        int32_t count = 0, capacity = 0;
        void upload(const UTVERTEX *v, int32_t n, bool dynamic);
        void draw() const;
        void destroy();
    };

private:
    void buildStaticMeshes();
    void drawPanelMatrix(const View &v, const TextPanel &panel, const vm::mat4 &model, float w, float h,
                         bool linear_output, float opacity);
    void drawMesh(const Mesh &m, const vm::mat4 &model, const View &v, const SceneParams &p, bool cull);

    Space m_space;
    GLuint m_prog = 0, m_ui_prog = 0, m_vig_prog = 0;
    GLint u_mvp = -1, u_mv = -1, u_fog_col = -1, u_fog = -1, u_linear = -1, u_tex = -1;
    GLint u_ui_mvp = -1, u_ui_tex = -1, u_ui_linear = -1, u_ui_opacity = -1;
    GLint u_vig_strength = -1;
    GLuint m_road_tex = 0;
    GLuint m_ui_vao = 0, m_ui_vbo = 0, m_vig_vao = 0;
    Mesh m_track, m_car, m_cockpit, m_ground, m_scenery, m_shadow, m_table, m_ctrl, m_ctrl_grip;
    uint32_t m_track_version = 0xffffffff;
    int32_t m_scenery_type = -1;
    bool m_front_cw = true;
    float m_steer_angle = 0.0f;
};

// Cockpit geometry for the VR / flat in-car view (car-local game units).
std::vector<UTVERTEX> BuildCockpitMesh();

} // namespace scr
