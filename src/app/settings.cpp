// settings.cpp - tiny ini reader / writer

#include "settings.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace scr {

static std::string Trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool Settings::load(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "r");
    if (!f) return false;
    std::map<std::string, std::string> kv;
    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        std::string l = Trim(line);
        if (l.empty() || l[0] == '#' || l[0] == ';' || l[0] == '[') continue;
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        kv[Trim(l.substr(0, eq))] = Trim(l.substr(eq + 1));
    }
    fclose(f);

    auto F = [&](const char *k, float &v) { auto it = kv.find(k); if (it != kv.end()) v = (float)atof(it->second.c_str()); };
    auto D = [&](const char *k, double &v) { auto it = kv.find(k); if (it != kv.end()) v = atof(it->second.c_str()); };
    auto I = [&](const char *k, int &v) { auto it = kv.find(k); if (it != kv.end()) v = atoi(it->second.c_str()); };
    auto B = [&](const char *k, bool &v) { auto it = kv.find(k); if (it != kv.end()) v = (it->second == "1" || it->second == "true" || it->second == "yes"); };
    auto S = [&](const char *k, std::string &v) { auto it = kv.find(k); if (it != kv.end()) v = it->second; };

    S("mode", mode);
    D("sim_hz", sim_hz);
    F("metres_per_unit", metres_per_unit);
    F("comfort_horizon_lock", comfort.horizon_lock);
    F("comfort_vignette", comfort.vignette);
    B("ffb_enabled", ffb.enabled);
    F("ffb_gain", ffb.gain);
    F("ffb_centering", ffb.centering);
    F("ffb_road", ffb.road);
    F("ffb_impacts", ffb.impacts);
    B("ffb_invert", ffb_invert);
    I("wheel_steer_axis", input.steer_axis);
    I("wheel_throttle_axis", input.throttle_axis);
    I("wheel_brake_axis", input.brake_axis);
    F("wheel_range_deg", input.wheel_range_deg);
    F("wheel_steer_lock_deg", input.steer_lock_deg);
    F("wheel_deadzone", input.steer_deadzone);
    F("gamepad_deadzone", input.stick_deadzone);
    F("vr_wheel_lock_deg", vr_wheel_lock_deg);
    B("flat_cockpit", flat_cockpit);
    B("mirror", mirror);
    I("window_width", window_w);
    I("window_height", window_h);

    if (sim_hz < 4.0 || sim_hz > 60.0) sim_hz = kDefaultSimHz;
    if (metres_per_unit < 0.002f || metres_per_unit > 0.1f) metres_per_unit = kDefaultMetresPerUnit;
    return true;
}

bool Settings::save(const std::string &path) const
{
    FILE *f = fopen(path.c_str(), "w");
    if (!f) return false;
    fprintf(f, "# Stunt Car Racer VR settings\n\n");
    fprintf(f, "# auto = VR when a headset / OpenXR runtime is available, otherwise flat screen\nmode = %s\n\n", mode.c_str());
    fprintf(f, "# Physics rate. 8.333 = original Amiga pace, 12.5 = Windows remake pace (faster)\nsim_hz = %.3f\n", sim_hz);
    fprintf(f, "# World scale in VR (metres per game unit). Larger = everything bigger\nmetres_per_unit = %.6f\n\n", metres_per_unit);
    fprintf(f, "# VR comfort: 0 = camera follows the car's pitch/roll, 1 = level horizon\ncomfort_horizon_lock = %.2f\n", comfort.horizon_lock);
    fprintf(f, "# Darken the edges of your view while airborne (0 = off, 1 = strong)\ncomfort_vignette = %.2f\n\n", comfort.vignette);
    fprintf(f, "# Force feedback (steering wheels) / rumble (gamepads, VR controllers)\nffb_enabled = %d\nffb_gain = %.2f\nffb_centering = %.2f\nffb_road = %.2f\nffb_impacts = %.2f\n", ffb.enabled ? 1 : 0, ffb.gain, ffb.centering, ffb.road, ffb.impacts);
    fprintf(f, "# Set to 1 if jolts push the wheel the wrong way\nffb_invert = %d\n\n", ffb_invert ? 1 : 0);
    fprintf(f, "# Steering wheel axes (-1 = learn: press throttle, then brake, after starting)\nwheel_steer_axis = %d\nwheel_throttle_axis = %d\nwheel_brake_axis = %d\n", input.steer_axis, input.throttle_axis, input.brake_axis);
    fprintf(f, "# Physical rotation of your wheel, and how much of it gives full steering lock\nwheel_range_deg = %.0f\nwheel_steer_lock_deg = %.0f\nwheel_deadzone = %.3f\ngamepad_deadzone = %.3f\n\n", input.wheel_range_deg, input.steer_lock_deg, input.steer_deadzone, input.stick_deadzone);
    fprintf(f, "# VR controllers: grip both hands to hold the virtual wheel; rotation for full lock\nvr_wheel_lock_deg = %.0f\n\n", vr_wheel_lock_deg);
    fprintf(f, "# Flat screen: 1 = 3D cockpit view, 0 = classic view (toggle with C)\nflat_cockpit = %d\n", flat_cockpit ? 1 : 0);
    fprintf(f, "# Desktop VR: show a spectator view in the window\nmirror = %d\nwindow_width = %d\nwindow_height = %d\n", mirror ? 1 : 0, window_w, window_h);
    fclose(f);
    return true;
}

} // namespace scr
