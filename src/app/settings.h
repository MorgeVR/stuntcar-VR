// settings.h - user settings, stored as key = value lines in settings.ini
#pragma once

#include <string>
#include "input_sdl.h"
#include "ffb.h"
#include "scene_setup.h"

namespace scr {

struct Settings
{
    std::string mode = "auto";       // auto (VR if a headset is available), vr, flat
    double sim_hz = kDefaultSimHz;   // 8.33 = Amiga pace, 12.5 = Windows remake pace
    float metres_per_unit = kDefaultMetresPerUnit;
    ComfortSettings comfort;
    FfbSettings ffb;
    bool ffb_invert = false;
    InputConfig input;
    float vr_wheel_lock_deg = 180.0f;   // virtual wheel rotation (VR controllers) for full lock
    bool flat_cockpit = false;          // flat screen: show the 3D cockpit instead of the classic view
    bool mirror = false;                // desktop VR: render a spectator view in the window
    int window_w = 1280, window_h = 720;
    int version = 2;                    // settings file format / defaults version

    bool load(const std::string &path);
    bool save(const std::string &path) const;
};

} // namespace scr
