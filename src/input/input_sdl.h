// input_sdl.h - keyboard, gamepads and force feedback steering wheels via SDL3.
#pragma once

#include <SDL3/SDL.h>
#include <string>
#include "ffb.h"
#include "car_behaviour.h"

namespace scr {

struct InputConfig
{
    // Steering wheel
    int   steer_axis = 0;           // joystick axis for steering
    int   throttle_axis = -1;       // -1 = learn: first pedal pressed becomes throttle
    int   brake_axis = -1;          // -1 = learn: second pedal pressed becomes brake
    float wheel_range_deg = 900.0f; // physical rotation of the wheel lock to lock
    float steer_lock_deg = 360.0f;  // wheel rotation that gives the car's full steering lock
    float steer_deadzone = 0.02f;
    // Gamepad
    float stick_deadzone = 0.12f;
};

// Per-frame input result
struct InputFrame
{
    uint32_t keys = 0;                 // scr::kKey* digital driving controls
    ScrAnalogInput analog = {};        // gamepad / wheel
    float wheel_angle_deg = 0.0f;      // physical wheel angle (for the cockpit wheel), 0 if none
    bool has_wheel = false;
    bool vr_steering = false;          // VR controllers are steering this frame

    // edge-triggered actions
    bool quit = false, select = false, back = false, up = false, down = false;
    int  track_hotkey = -1;            // 0..7 from number keys
    bool pause = false, unpause = false, turn_around = false;
    bool toggle_outside = false, toggle_cockpit = false, next_scenery = false;
    bool recenter = false, toggle_pace = false, toggle_comfort = false;
};

class InputSystem
{
public:
    bool init(const InputConfig &cfg);
    void shutdown();
    void handleEvent(const SDL_Event &e, bool racing, InputFrame &f);   // keyboard + hotplug
    void poll(bool racing, InputFrame &f);                              // analog devices
    void applyForceFeedback(const FfbOutput &o, float steer_pos, float dt);
    std::string status() const;

private:
    void openJoystick(SDL_JoystickID id);
    void closeJoystick(SDL_JoystickID id);
    void setupHaptics();
    float pedal(int axis) const;

    InputConfig m_cfg;
    SDL_Gamepad *m_pad = nullptr;
    SDL_Joystick *m_wheel = nullptr;
    SDL_Haptic *m_haptic = nullptr;
    int m_eff_spring = -1, m_eff_damper = -1, m_eff_const = -1, m_eff_sine = -1;
    uint32_t m_haptic_features = 0;
    float m_last_const = 0, m_last_spring = -1, m_last_damper = -1, m_last_sine = -1;
    double m_ffb_timer = 0.0;
    int m_axis_rest[16];
    bool m_rest_valid = false;
    int m_learn_stage = 0;    // 0 = throttle, 1 = brake, 2 = done
    std::string m_wheel_name, m_pad_name, m_learn_msg;
};

} // namespace scr
