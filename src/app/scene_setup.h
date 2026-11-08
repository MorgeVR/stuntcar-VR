// scene_setup.h - turns Game state into renderer views / scene parameters.
// Shared by the flat-screen path, the VR path and the offscreen render test.
#pragma once

#include "game.h"
#include "renderer.h"

namespace scr {

struct ComfortSettings
{
    // 0 = camera follows the car's pitch and roll exactly (original feel)
    // 1 = horizon stays level (car still tilts around you)
    float horizon_lock = 0.0f;
    // Vignette strength while airborne / rotating fast, 0 disables
    float vignette = 0.6f;
};

// Pose of the driver's seat in world metres (VR seat origin). Head tracking is applied
// on top of this. comfort.horizon_lock removes some of the car's pitch / roll.
vm::Pose SeatWorldPose(const Game &game, const Space &space, const ComfortSettings &comfort);

// Flat screen camera (original views: menu orbit, preview, cockpit or outside view).
View FlatView(const Game &game, const Space &space, int width, int height, bool cockpit, float vfov_deg = 60.0f);

// Scene contents for the current game mode. `vr` enables the cockpit model and,
// in the menu / preview, the tabletop diorama placed relative to `diorama_anchor`.
SceneParams BuildScene(const Game &game, const Space &space, bool vr, bool cockpit, const vm::Pose &diorama_anchor);

// The diorama: the whole track as a ~1.6 m wide model on a virtual table.
vm::mat4 DioramaTransform(const vm::Pose &anchor, float table_width_m = 1.1f);

// Default diorama placement relative to the player's (recentred) head, and the world
// environment (ground + horizon scenery) around a standing / seated player in the menu.
vm::Pose DefaultDioramaAnchor();
vm::mat4 MenuEnvironmentTransform(const Space &space, float floor_below_head_m = 1.25f);

// Vignette strength from car telemetry (0..1)
float ComfortVignette(const Game &game, const ComfortSettings &comfort, float dt);

// Panels
void DrawDashboard(TextPanel &panel, const Game &game);
void DrawMenuPanel(TextPanel &panel, const Game &game, int highlighted_track, bool vr);
void DrawRaceOverlay(TextPanel &panel, const Game &game, bool vr);   // opponent name, race won/lost, game over

} // namespace scr
