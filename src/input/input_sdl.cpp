// input_sdl.cpp - keyboard, gamepad, wheel + force feedback (SDL3)

#include "input_sdl.h"
#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>

namespace scr {

bool g_ffb_invert = false;   // set from settings: flips the sign of constant forces

static bool LooksLikeWheel(SDL_JoystickID id)
{
    if (SDL_GetJoystickTypeForID(id) == SDL_JOYSTICK_TYPE_WHEEL) return true;
    const char *n = SDL_GetJoystickNameForID(id);
    if (!n) return false;
    std::string s(n);
    for (char &c : s) c = (char)tolower((unsigned char)c);
    static const char *keys[] = {"wheel", "g29", "g920", "g923", "g27", "g25", "driving force", "t300", "t150", "t248",
                                 "tmx", "t500", "t-gt", "ts-pc", "ts-xw", "fanatec", "csl", "clubsport", "podium",
                                 "moza", "simucube", "simagic", "cammus", "thrustmaster", "racing"};
    for (const char *k : keys)
        if (s.find(k) != std::string::npos) return true;
    return false;
}

bool InputSystem::init(const InputConfig &cfg)
{
    m_cfg = cfg;
    if (cfg.throttle_axis >= 0 && cfg.brake_axis >= 0) m_learn_stage = 2;
    int n = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&n);
    for (int i = 0; ids && i < n; i++) openJoystick(ids[i]);
    SDL_free(ids);
    return true;
}

void InputSystem::shutdown()
{
    if (m_haptic) { SDL_StopHapticEffects(m_haptic); SDL_CloseHaptic(m_haptic); m_haptic = nullptr; }
    if (m_wheel) { SDL_CloseJoystick(m_wheel); m_wheel = nullptr; }
    if (m_pad) { SDL_CloseGamepad(m_pad); m_pad = nullptr; }
}

void InputSystem::openJoystick(SDL_JoystickID id)
{
    if (!m_wheel && LooksLikeWheel(id))
    {
        m_wheel = SDL_OpenJoystick(id);
        if (m_wheel)
        {
            m_wheel_name = SDL_GetJoystickName(m_wheel) ? SDL_GetJoystickName(m_wheel) : "wheel";
            for (PedalAxis &pa : m_pedals) pa = PedalAxis();
            fprintf(stderr, "[StuntCarVR] steering wheel: %s (%d axes)\n", m_wheel_name.c_str(), SDL_GetNumJoystickAxes(m_wheel));
            setupHaptics();
        }
        return;
    }
    // SDL also announces some wheels (e.g. the G27) as gamepads: never open a wheel twice
    if (LooksLikeWheel(id) || (m_wheel && SDL_GetJoystickID(m_wheel) == id)) return;
    if (!m_pad && SDL_IsGamepad(id))
    {
        m_pad = SDL_OpenGamepad(id);
        if (m_pad)
        {
            m_pad_name = SDL_GetGamepadName(m_pad) ? SDL_GetGamepadName(m_pad) : "gamepad";
            fprintf(stderr, "[StuntCarVR] gamepad: %s\n", m_pad_name.c_str());
        }
    }
}

void InputSystem::closeJoystick(SDL_JoystickID id)
{
    if (m_wheel && SDL_GetJoystickID(m_wheel) == id)
    {
        if (m_haptic) { SDL_CloseHaptic(m_haptic); m_haptic = nullptr; }
        SDL_CloseJoystick(m_wheel);
        m_wheel = nullptr;
        m_eff_spring = m_eff_damper = m_eff_const = m_eff_sine = -1;
    }
    if (m_pad && SDL_GetGamepadID(m_pad) == id)
    {
        SDL_CloseGamepad(m_pad);
        m_pad = nullptr;
    }
}

void InputSystem::setupHaptics()
{
    m_haptic = SDL_OpenHapticFromJoystick(m_wheel);
    if (!m_haptic)
    {
        fprintf(stderr, "[StuntCarVR] wheel has no force feedback (%s)\n", SDL_GetError());
        return;
    }
    m_haptic_features = SDL_GetHapticFeatures(m_haptic);
    if (m_haptic_features & SDL_HAPTIC_GAIN) SDL_SetHapticGain(m_haptic, 100);
    if (m_haptic_features & SDL_HAPTIC_AUTOCENTER) SDL_SetHapticAutocenter(m_haptic, 0);   // we do our own centring

    auto make = [&](uint32_t feature, auto fill) -> int {
        if (!(m_haptic_features & feature)) return -1;
        SDL_HapticEffect e;
        SDL_zero(e);
        fill(e);
        int id = SDL_CreateHapticEffect(m_haptic, &e);
        if (id >= 0) SDL_RunHapticEffect(m_haptic, id, SDL_HAPTIC_INFINITY);
        return id;
    };
    m_eff_spring = make(SDL_HAPTIC_SPRING, [](SDL_HapticEffect &e) {
        e.type = SDL_HAPTIC_SPRING;
        e.condition.direction.type = SDL_HAPTIC_STEERING_AXIS;
        e.condition.length = SDL_HAPTIC_INFINITY;
        e.condition.right_sat[0] = e.condition.left_sat[0] = 0xFFFF;
        e.condition.right_coeff[0] = e.condition.left_coeff[0] = 0x2000;
    });
    m_eff_damper = make(SDL_HAPTIC_DAMPER, [](SDL_HapticEffect &e) {
        e.type = SDL_HAPTIC_DAMPER;
        e.condition.direction.type = SDL_HAPTIC_STEERING_AXIS;
        e.condition.length = SDL_HAPTIC_INFINITY;
        e.condition.right_sat[0] = e.condition.left_sat[0] = 0xFFFF;
        e.condition.right_coeff[0] = e.condition.left_coeff[0] = 0x1000;
    });
    m_eff_const = make(SDL_HAPTIC_CONSTANT, [](SDL_HapticEffect &e) {
        e.type = SDL_HAPTIC_CONSTANT;
        e.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
        e.constant.length = SDL_HAPTIC_INFINITY;
        e.constant.level = 0;
    });
    m_eff_sine = make(SDL_HAPTIC_SINE, [](SDL_HapticEffect &e) {
        e.type = SDL_HAPTIC_SINE;
        e.periodic.direction.type = SDL_HAPTIC_STEERING_AXIS;
        e.periodic.length = SDL_HAPTIC_INFINITY;
        e.periodic.period = 50;
        e.periodic.magnitude = 0;
    });
    fprintf(stderr, "[StuntCarVR] force feedback: spring %s, damper %s, constant %s, vibration %s\n",
            m_eff_spring >= 0 ? "yes" : "no", m_eff_damper >= 0 ? "yes" : "no", m_eff_const >= 0 ? "yes" : "no",
            m_eff_sine >= 0 ? "yes" : "no");
}

void InputSystem::handleEvent(const SDL_Event &e, bool racing, InputFrame &f)
{
    switch (e.type)
    {
    case SDL_EVENT_JOYSTICK_ADDED: openJoystick(e.jdevice.which); break;
    case SDL_EVENT_JOYSTICK_REMOVED: closeJoystick(e.jdevice.which); break;
    case SDL_EVENT_GAMEPAD_ADDED: openJoystick(e.gdevice.which); break;
    case SDL_EVENT_GAMEPAD_REMOVED: closeJoystick(e.gdevice.which); break;
    case SDL_EVENT_QUIT: f.quit = true; break;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        if (m_wheel && e.jbutton.which == SDL_GetJoystickID(m_wheel))
        {
            bool in_setup = m_cfg.buttons.setup_active;
            int act = m_cfg.buttons.press(e.jbutton.button);
            fprintf(stderr, "[StuntCarVR] wheel button %d pressed%s%s\n", (int)e.jbutton.button,
                    in_setup ? " (assigned)" : (act >= 0 ? " -> " : ""), (!in_setup && act >= 0) ? WheelButtons::name(act) : "");
            switch (act)
            {
            case kWbView: f.toggle_outside = true; break;
            case kWbTurnAround: f.turn_around = true; break;
            case kWbPause: f.pause = true; break;
            case kWbMenu: f.back = true; break;
            case kWbSelect: if (!racing) f.select = true; break;
            case kWbRecenter: f.recenter = true; break;
            default: break;
            }
        }
        break;
    case SDL_EVENT_JOYSTICK_HAT_MOTION:
        // the G27 shifter's D-pad moves through menus
        if (m_wheel && e.jhat.which == SDL_GetJoystickID(m_wheel) && !racing)
        {
            if (e.jhat.value & SDL_HAT_UP) f.up = true;
            if (e.jhat.value & SDL_HAT_DOWN) f.down = true;
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        switch (e.gbutton.button)
        {
        case SDL_GAMEPAD_BUTTON_SOUTH: if (!racing) f.select = true; break;
        case SDL_GAMEPAD_BUTTON_EAST: if (racing) f.turn_around = true; else f.back = true; break;
        case SDL_GAMEPAD_BUTTON_NORTH: f.toggle_outside = true; break;
        case SDL_GAMEPAD_BUTTON_BACK: f.back = true; break;
        case SDL_GAMEPAD_BUTTON_START: f.pause = true; break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP: f.up = true; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: f.down = true; break;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: f.recenter = true; break;
        default: break;
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        if (e.key.repeat) break;
        switch (e.key.key)
        {
        case SDLK_ESCAPE: f.quit = true; break;
        case SDLK_RETURN: case SDLK_KP_ENTER: if (!racing) f.select = true; break;
        case SDLK_S: if (!racing) f.select = true; break;   // original: S selects in menus (and steers left in a race)
        case SDLK_M: f.back = true; break;
        case SDLK_UP: if (!racing) f.up = true; break;
        case SDLK_DOWN: if (!racing) f.down = true; break;
        case SDLK_P: f.pause = true; break;
        case SDLK_O: f.unpause = true; break;
        case SDLK_R: if (racing) f.turn_around = true; break;
        case SDLK_V: case SDLK_BACKSPACE: f.toggle_outside = true; break;
        case SDLK_C: f.toggle_cockpit = true; break;
        case SDLK_F4: f.next_scenery = true; break;
        case SDLK_F9: case SDLK_F10: f.toggle_pace = true; break;
        case SDLK_F2: f.toggle_comfort = true; break;
        case SDLK_F3: f.wheel_setup = true; break;
        case SDLK_HOME: case SDLK_F12: f.recenter = true; break;
        default:
            if (!racing && e.key.key >= SDLK_1 && e.key.key <= SDLK_8) f.track_hotkey = (int)(e.key.key - SDLK_1);
            break;
        }
        break;
    default: break;
    }
}

float InputSystem::pedal(int axis)
{
    if (!m_wheel || axis < 0 || axis >= SDL_GetNumJoystickAxes(m_wheel) || axis >= 16) return 0.0f;
    PedalAxis &pa = m_pedals[axis];
    const bool was_known = pa.known();
    float v = pa.update(SDL_GetJoystickAxis(m_wheel, axis));
    if (!was_known && pa.known())
        fprintf(stderr, "[StuntCarVR] wheel axis %d rests at %s end\n", axis, pa.rest_sign > 0 ? "the + " : "the - ");
    return v;
}

void InputSystem::poll(bool racing, InputFrame &f)
{
    // Keyboard: original layout (S/D steer, Return accelerate+boost, Space brake+boost, ' brake)
    // plus arrows / WASD-style modern layout.
    const bool *k = SDL_GetKeyboardState(nullptr);
    if (racing)
    {
        if (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) f.keys |= kKeyLeft;
        if (k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) f.keys |= kKeyRight;
        if (k[SDL_SCANCODE_RETURN]) f.keys |= kKeyAccelBoost;
        if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) f.keys |= (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) ? kKeyAccelBoost : kKeyAccel;
        if (k[SDL_SCANCODE_SPACE]) f.keys |= (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) ? kKeyAccelBoost : kKeyBrakeBoost;
        if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_APOSTROPHE] || k[SDL_SCANCODE_NONUSHASH]) f.keys |= kKeyBrake;
    }

    ScrAnalogInput a = {};
    a.steer_deadzone = 0.08f;

    // Gamepad
    if (m_pad)
    {
        float lx = SDL_GetGamepadAxis(m_pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
        float rt = SDL_GetGamepadAxis(m_pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0f;
        float lt = SDL_GetGamepadAxis(m_pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0f;
        bool boost = SDL_GetGamepadButton(m_pad, SDL_GAMEPAD_BUTTON_SOUTH) || SDL_GetGamepadButton(m_pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        if (std::fabs(lx) > m_cfg.stick_deadzone || rt > 0.05f || lt > 0.05f || boost)
        {
            a.active = true;
            a.steer = lx;
            a.steer_deadzone = m_cfg.stick_deadzone;
            a.throttle = rt;
            a.brake = lt;
            a.boost = boost && racing;
        }
        if (!racing)
        {
            static bool stick_held = false;
            float ly = SDL_GetGamepadAxis(m_pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
            if (!stick_held && ly < -0.6f) f.up = true;
            if (!stick_held && ly > 0.6f) f.down = true;
            stick_held = std::fabs(ly) > 0.4f;
        }
    }

    // Steering wheel (takes priority over the gamepad)
    if (m_wheel)
    {
        int na = std::min(16, SDL_GetNumJoystickAxes(m_wheel));
        // learn pedals: the first non-steering axis pushed past half travel
        if (m_learn_stage < 2)
        {
            for (int i = 0; i < na; i++)
            {
                if (i == m_cfg.steer_axis || i == m_cfg.throttle_axis) continue;
                if (pedal(i) > 0.5f && m_pedals[i].known())
                {
                    if (m_learn_stage == 0) { m_cfg.throttle_axis = i; m_learn_msg = "throttle = axis " + std::to_string(i); }
                    else { m_cfg.brake_axis = i; m_learn_msg = "brake = axis " + std::to_string(i); }
                    fprintf(stderr, "[StuntCarVR] pedal learned: %s\n", m_learn_msg.c_str());
                    m_learn_stage++;
                    break;
                }
            }
        }
        float raw = SDL_GetJoystickAxis(m_wheel, m_cfg.steer_axis) / 32767.0f;
        f.has_wheel = true;
        f.wheel_angle_deg = raw * m_cfg.wheel_range_deg * 0.5f;
        float steer = f.wheel_angle_deg / (m_cfg.steer_lock_deg * 0.5f);
        a.active = true;
        a.steer = std::max(-1.0f, std::min(1.0f, steer));
        a.steer_deadzone = m_cfg.steer_deadzone;
        a.throttle = std::max(a.throttle, pedal(m_cfg.throttle_axis));
        a.brake = std::max(a.brake, pedal(m_cfg.brake_axis));
        // boost: the assigned button (or, before setup, any unassigned button)
        bool down[32] = {};
        int nb = std::min(32, SDL_GetNumJoystickButtons(m_wheel));
        for (int b = 0; b < nb; b++) down[b] = SDL_GetJoystickButton(m_wheel, b);
        if (racing && m_cfg.buttons.boostHeld(down, nb)) a.boost = true;
    }
    f.analog = a;
}

void InputSystem::applyForceFeedback(const FfbOutput &o, float steer_pos, float dt)
{
    // Gamepad rumble
    if (m_pad)
        SDL_RumbleGamepad(m_pad, (Uint16)(o.rumble_low * 0xFFFF), (Uint16)(o.rumble_high * 0xFFFF), 100);

    if (!m_haptic) return;
    // Rate-limit updates (USB wheels choke on too many effect updates)
    m_ffb_timer += dt;
    if (m_ffb_timer < 1.0 / 100.0) return;
    m_ffb_timer = 0.0;

    float constant = o.constant;
    if (m_eff_spring < 0)
        constant -= o.spring * steer_pos;   // emulate centring with a constant force
    if (g_ffb_invert) constant = -constant;

    SDL_HapticEffect e;
    if (m_eff_spring >= 0 && std::fabs(o.spring - m_last_spring) > 0.01f)
    {
        SDL_zero(e);
        e.type = SDL_HAPTIC_SPRING;
        e.condition.direction.type = SDL_HAPTIC_STEERING_AXIS;
        e.condition.length = SDL_HAPTIC_INFINITY;
        e.condition.right_sat[0] = e.condition.left_sat[0] = 0xFFFF;
        e.condition.right_coeff[0] = e.condition.left_coeff[0] = (Sint16)(o.spring * 0x7FFF);
        SDL_UpdateHapticEffect(m_haptic, m_eff_spring, &e);
        m_last_spring = o.spring;
    }
    if (m_eff_damper >= 0 && std::fabs(o.damper - m_last_damper) > 0.01f)
    {
        SDL_zero(e);
        e.type = SDL_HAPTIC_DAMPER;
        e.condition.direction.type = SDL_HAPTIC_STEERING_AXIS;
        e.condition.length = SDL_HAPTIC_INFINITY;
        e.condition.right_sat[0] = e.condition.left_sat[0] = 0xFFFF;
        e.condition.right_coeff[0] = e.condition.left_coeff[0] = (Sint16)(o.damper * 0x7FFF);
        SDL_UpdateHapticEffect(m_haptic, m_eff_damper, &e);
        m_last_damper = o.damper;
    }
    if (m_eff_const >= 0 && std::fabs(constant - m_last_const) > 0.005f)
    {
        SDL_zero(e);
        e.type = SDL_HAPTIC_CONSTANT;
        e.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
        e.constant.length = SDL_HAPTIC_INFINITY;
        e.constant.level = (Sint16)(std::max(-1.0f, std::min(1.0f, constant)) * 0x7FFF);
        SDL_UpdateHapticEffect(m_haptic, m_eff_const, &e);
        m_last_const = constant;
    }
    if (m_eff_sine >= 0 && std::fabs(o.periodic - m_last_sine) > 0.01f)
    {
        SDL_zero(e);
        e.type = SDL_HAPTIC_SINE;
        e.periodic.direction.type = SDL_HAPTIC_STEERING_AXIS;
        e.periodic.length = SDL_HAPTIC_INFINITY;
        e.periodic.period = (Uint16)std::max(10.0f, 1000.0f / std::max(1.0f, o.periodic_hz));
        e.periodic.magnitude = (Sint16)(std::min(1.0f, o.periodic) * 0x7FFF);
        SDL_UpdateHapticEffect(m_haptic, m_eff_sine, &e);
        m_last_sine = o.periodic;
    }
}

void InputSystem::update(float dt, bool in_menu)
{
    WheelButtons &wb = m_cfg.buttons;
    if (wb.setup_active && (!in_menu || !m_wheel)) wb.cancelSetup();
    // first time a wheel is seen in the menu: ask for its buttons once
    if (m_wheel && in_menu && !wb.configured && !m_auto_setup_done && !wb.setup_active)
    {
        m_auto_setup_done = true;
        startWheelSetup();
    }
    wb.tick(dt);
}

void InputSystem::startWheelSetup()
{
    if (!m_wheel) return;
    m_cfg.buttons.startSetup();
    fprintf(stderr, "[StuntCarVR] wheel button setup started\n");
}

bool InputSystem::takeButtonChange(WheelButtons &out)
{
    if (!m_cfg.buttons.changed) return false;
    m_cfg.buttons.changed = false;
    out = m_cfg.buttons;
    out.setup_active = false;
    fprintf(stderr, "[StuntCarVR] wheel buttons saved: %s\n", out.toString().c_str());
    return true;
}

std::string InputSystem::status() const
{
    std::string s;
    if (m_wheel)
    {
        s += "Wheel: " + m_wheel_name + (m_haptic ? " (force feedback)" : " (no FFB)");
        if (m_cfg.buttons.setup_active) s += " - " + m_cfg.buttons.prompt();
        else if (m_learn_stage == 0) s += " - press the THROTTLE pedal";
        else if (m_learn_stage == 1) s += " - press the BRAKE pedal";
    }
    if (m_pad) s += (s.empty() ? "" : " | ") + std::string("Gamepad: ") + m_pad_name;
    return s;
}

} // namespace scr
