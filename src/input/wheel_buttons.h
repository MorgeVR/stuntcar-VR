// wheel_buttons.h - steering wheel button actions and the one-time "press a button for..." setup.
//
// Wheel button numbering differs between wheels and drivers (the G27's depends on the Logitech
// driver), so instead of guessing, the game asks once: it prompts for each action and stores
// the button numbers in settings.ini ("wheel_buttons").
#pragma once

#include <string>
#include <cstdio>
#include <cstdlib>

namespace scr {

enum WheelAction
{
    kWbBoost = 0,
    kWbView,
    kWbTurnAround,
    kWbPause,
    kWbMenu,
    kWbSelect,
    kWbRecenter,
    kWbCount
};

struct WheelButtons
{
    int map[kWbCount] = {-1, -1, -1, -1, -1, -1, -1};
    bool configured = false;        // setup completed at least once

    bool setup_active = false;
    int setup_index = 0;
    float setup_timer = 0.0f;
    bool changed = false;           // mapping changed: settings should be saved

    static constexpr float kPromptSeconds = 8.0f;

    static const char *name(int a)
    {
        static const char *n[kWbCount] = {"BOOST", "CHANGE VIEW", "TURN AROUND / RESET", "PAUSE",
                                          "BACK TO TRACK MENU", "SELECT (MENUS)", "RECENTRE VR SEAT"};
        return (a >= 0 && a < kWbCount) ? n[a] : "?";
    }

    void startSetup()
    {
        setup_active = true;
        setup_index = 0;
        setup_timer = kPromptSeconds;
    }

    void cancelSetup() { setup_active = false; }

    // A wheel button was pressed. Returns the action it triggers, or -1 (none / used by setup).
    int press(int button)
    {
        if (setup_active)
        {
            for (int a = 0; a < kWbCount; a++)
                if (map[a] == button) map[a] = -1;   // one action per button
            map[setup_index] = button;
            advance();
            return -1;
        }
        for (int a = 0; a < kWbCount; a++)
            if (map[a] == button) return a;
        return -1;
    }

    // Prompt timeout: skip the current action (keeping its previous button)
    void tick(float dt)
    {
        if (!setup_active) return;
        setup_timer -= dt;
        if (setup_timer <= 0.0f) advance();
    }

    // Boost is held, not pressed. Without a boost button every unassigned button boosts.
    bool boostHeld(const bool *down, int count) const
    {
        if (map[kWbBoost] >= 0) return map[kWbBoost] < count && down[map[kWbBoost]];
        for (int b = 0; b < count; b++)
        {
            bool assigned = false;
            for (int a = 0; a < kWbCount; a++) assigned |= (map[a] == b);
            if (down[b] && !assigned) return true;
        }
        return false;
    }

    std::string prompt() const
    {
        if (!setup_active) return std::string();
        char buf[160];
        snprintf(buf, sizeof(buf), "WHEEL SETUP %d/%d: PRESS THE BUTTON FOR %s  (WAIT %d S TO SKIP)",
                 setup_index + 1, (int)kWbCount, name(setup_index), (int)(setup_timer + 0.99f));
        return buf;
    }

    // Three lines for the menu banner, separated by '|'
    std::string panelText() const
    {
        if (!setup_active) return std::string();
        char buf[160];
        snprintf(buf, sizeof(buf), "WHEEL SETUP %d/%d: PRESS A BUTTON FOR|%s|OR WAIT %d SECONDS TO SKIP",
                 setup_index + 1, (int)kWbCount, name(setup_index), (int)(setup_timer + 0.99f));
        return buf;
    }

    std::string toString() const
    {
        std::string s;
        for (int a = 0; a < kWbCount; a++) s += (a ? "," : "") + std::to_string(map[a]);
        return s;
    }

    void fromString(const std::string &s)
    {
        int a = 0;
        size_t pos = 0;
        while (a < kWbCount && pos <= s.size())
        {
            size_t c = s.find(',', pos);
            map[a++] = atoi(s.substr(pos, c == std::string::npos ? std::string::npos : c - pos).c_str());
            if (c == std::string::npos) break;
            pos = c + 1;
        }
    }

private:
    void advance()
    {
        setup_index++;
        setup_timer = kPromptSeconds;
        if (setup_index >= kWbCount)
        {
            setup_active = false;
            configured = true;
            changed = true;
        }
    }
};

} // namespace scr
