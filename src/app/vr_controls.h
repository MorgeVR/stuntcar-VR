// vr_controls.h - VR controller driving: a virtual steering wheel held with the grips,
// thumbstick fallback, triggers for throttle / brake, menu navigation.
#pragma once

#include "xr_app.h"
#include "input_sdl.h"

namespace scr {

struct VrWheel
{
    float lock_deg = 180.0f;     // wheel rotation lock to lock for full steering
    // state
    float angle = 0.0f;          // current wheel angle (radians, + = clockwise / right)
    bool held = false;
    int  held_hands = 0;         // bitmask
    float grab_hand_angle = 0.0f, grab_wheel_angle = 0.0f;
    float two_hand_offset = 0.0f;
    bool menu_stick_held = false;

    // Wheel centre in seat space (metres, -z forward): matches the cockpit wheel model.
    vm::vec3 centre = vm::vec3(0.0f, -0.23f, -0.44f);

    // Update from controllers. Writes analog driving input and menu actions into `f`.
    void update(const XrInputs &in, bool racing, float dt, InputFrame &f);
    float steer() const;         // -1..1
};

} // namespace scr
