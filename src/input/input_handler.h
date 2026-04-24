// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "SDL3/SDL_events.h"
#include "SDL3/SDL_timer.h"

#include "common/logging/log.h"
#include "common/types.h"
#include "core/libraries/pad/pad.h"
#include "fmt/format.h"
#include "input/controller.h"
#include <queue>
#include <core/libraries/ime/ime_common.h>
#include <core/libraries/mouse/mouse_common.h>

// +1 and +2 is taken
#define SDL_MOUSE_WHEEL_UP SDL_EVENT_MOUSE_WHEEL + 3
#define SDL_MOUSE_WHEEL_DOWN SDL_EVENT_MOUSE_WHEEL + 4
#define SDL_MOUSE_WHEEL_LEFT SDL_EVENT_MOUSE_WHEEL + 5
#define SDL_MOUSE_WHEEL_RIGHT SDL_EVENT_MOUSE_WHEEL + 7

#define SDL_GAMEPAD_BUTTON_TOUCHPAD_LEFT SDL_GAMEPAD_BUTTON_COUNT + 1
#define SDL_GAMEPAD_BUTTON_TOUCHPAD_CENTER SDL_GAMEPAD_BUTTON_COUNT + 2
#define SDL_GAMEPAD_BUTTON_TOUCHPAD_RIGHT SDL_GAMEPAD_BUTTON_COUNT + 3

#define SDL_EVENT_TOGGLE_FULLSCREEN SDL_EVENT_USER + 1
#define SDL_EVENT_TOGGLE_PAUSE SDL_EVENT_USER + 2
#define SDL_EVENT_CHANGE_CONTROLLER SDL_EVENT_USER + 3
#define SDL_EVENT_TOGGLE_SIMPLE_FPS SDL_EVENT_USER + 4
#define SDL_EVENT_RELOAD_INPUTS SDL_EVENT_USER + 5
#define SDL_EVENT_MOUSE_TO_JOYSTICK SDL_EVENT_USER + 6
#define SDL_EVENT_MOUSE_TO_GYRO SDL_EVENT_USER + 7
#define SDL_EVENT_MOUSE_TO_TOUCHPAD SDL_EVENT_USER + 8
#define SDL_EVENT_QUIT_DIALOG SDL_EVENT_USER + 9
#define SDL_EVENT_MOUSE_WHEEL_OFF SDL_EVENT_USER + 10
#define SDL_EVENT_ADD_VIRTUAL_USER SDL_EVENT_USER + 11
#define SDL_EVENT_REMOVE_VIRTUAL_USER SDL_EVENT_USER + 12
#define SDL_EVENT_RDOC_CAPTURE SDL_EVENT_USER + 13
#define SDL_EVENT_SCREENSHOT_WITH_OVERLAYS SDL_EVENT_USER + 14

#define LEFTJOYSTICK_HALFMODE 0x00010000
#define RIGHTJOYSTICK_HALFMODE 0x00020000
#define BACK_BUTTON 0x00040000

#define KEY_TOGGLE 0x00200000
#define MOUSE_GYRO_ROLL_MODE 0x00400000

#define HOTKEY_FULLSCREEN 0xf0000001
#define HOTKEY_PAUSE 0xf0000002
#define HOTKEY_SIMPLE_FPS 0xf0000003
#define HOTKEY_QUIT 0xf0000004
#define HOTKEY_RELOAD_INPUTS 0xf0000005
#define HOTKEY_TOGGLE_MOUSE_TO_JOYSTICK 0xf0000006
#define HOTKEY_TOGGLE_MOUSE_TO_GYRO 0xf0000007
#define HOTKEY_TOGGLE_MOUSE_TO_TOUCHPAD 0xf0000008
#define HOTKEY_RENDERDOC 0xf0000009
#define HOTKEY_VOLUME_UP 0xf000000a
#define HOTKEY_VOLUME_DOWN 0xf000000b
#define HOTKEY_ADD_VIRTUAL_USER 0xf000000c
#define HOTKEY_REMOVE_VIRTUAL_USER 0xf000000d
#define HOTKEY_SCREENSHOT_WITH_OVERLAYS 0xf000000e

#define SDL_UNMAPPED UINT32_MAX - 1

namespace Input {
using Input::Axis;
using Libraries::Pad::OrbisPadButtonDataOffset;

struct AxisMapping {
    u32 axis;
    s16 value;
    AxisMapping(SDL_GamepadAxis a, s16 v) : axis(a), value(v) {}
};

enum class InputType { Axis, KeyboardMouse, Controller, Count };
const std::array<std::string, 4> input_type_names = {"Axis", "KBM", "Controller", "Unknown"};

class InputID {
public:
    InputType type;
    u32 sdl_id;
    u8 gamepad_id;
    InputID(InputType d = InputType::Count, u32 i = (u32)-1, u8 g = 1)
        : type(d), sdl_id(i), gamepad_id(g) {}
    bool operator==(const InputID& o) const {
        return type == o.type && sdl_id == o.sdl_id && gamepad_id == o.gamepad_id;
    }
    bool operator!=(const InputID& o) const {
        return type != o.type || sdl_id != o.sdl_id || gamepad_id != o.gamepad_id;
    }
    auto operator<=>(const InputID& o) const {
        return std::tie(gamepad_id, type, sdl_id, gamepad_id) <=>
               std::tie(o.gamepad_id, o.type, o.sdl_id, o.gamepad_id);
    }
    bool IsValid() const {
        return *this != InputID();
    }
    std::string ToString() {
        return fmt::format("({}. {}: {:x})", gamepad_id, input_type_names[(u8)type], sdl_id);
    }
};

class InputEvent {
public:
    InputID input;
    bool active;
    s8 axis_value;

    InputEvent(InputID i = InputID(), bool a = false, s8 v = 0)
        : input(i), active(a), axis_value(v) {}
    InputEvent(InputType d, u32 i, bool a = false, s8 v = 0)
        : input(d, i), active(a), axis_value(v) {}
};

// i strongly suggest you collapse these maps
const std::map<std::string, u32> string_to_cbutton_map = {
    {"triangle", SDL_GAMEPAD_BUTTON_NORTH},
    {"circle", SDL_GAMEPAD_BUTTON_EAST},
    {"cross", SDL_GAMEPAD_BUTTON_SOUTH},
    {"square", SDL_GAMEPAD_BUTTON_WEST},
    {"l1", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {"r1", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {"l3", SDL_GAMEPAD_BUTTON_LEFT_STICK},
    {"r3", SDL_GAMEPAD_BUTTON_RIGHT_STICK},
    {"pad_up", SDL_GAMEPAD_BUTTON_DPAD_UP},
    {"pad_down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {"pad_left", SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {"pad_right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {"options", SDL_GAMEPAD_BUTTON_START},

    // these are outputs only (touchpad can only be bound to itself)
    {"touchpad_left", SDL_GAMEPAD_BUTTON_TOUCHPAD_LEFT},
    {"touchpad_center", SDL_GAMEPAD_BUTTON_TOUCHPAD_CENTER},
    {"touchpad_right", SDL_GAMEPAD_BUTTON_TOUCHPAD_RIGHT},
    {"leftjoystick_halfmode", LEFTJOYSTICK_HALFMODE},
    {"rightjoystick_halfmode", RIGHTJOYSTICK_HALFMODE},

    // this is only for input
    {"back", SDL_GAMEPAD_BUTTON_BACK},
    {"share", SDL_GAMEPAD_BUTTON_BACK},
    {"lpaddle_high", SDL_GAMEPAD_BUTTON_LEFT_PADDLE1},
    {"lpaddle_low", SDL_GAMEPAD_BUTTON_LEFT_PADDLE2},
    {"rpaddle_high", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1},
    {"rpaddle_low", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2},
    {"mouse_gyro_roll_mode", MOUSE_GYRO_ROLL_MODE},
};
const std::map<std::string, u32> string_to_hotkey_map = {
    {"hotkey_pause", HOTKEY_PAUSE},
    {"hotkey_fullscreen", HOTKEY_FULLSCREEN},
    {"hotkey_show_fps", HOTKEY_SIMPLE_FPS},
    {"hotkey_quit", HOTKEY_QUIT},
    {"hotkey_reload_inputs", HOTKEY_RELOAD_INPUTS},
    {"hotkey_toggle_mouse_to_joystick", HOTKEY_TOGGLE_MOUSE_TO_JOYSTICK},
    {"hotkey_toggle_mouse_to_gyro", HOTKEY_TOGGLE_MOUSE_TO_GYRO},
    {"hotkey_toggle_mouse_to_touchpad", HOTKEY_TOGGLE_MOUSE_TO_TOUCHPAD},
    {"hotkey_capture_frame", HOTKEY_RENDERDOC},
    {"hotkey_screenshot_with_overlays", HOTKEY_SCREENSHOT_WITH_OVERLAYS},
    {"hotkey_renderdoc_capture", HOTKEY_RENDERDOC},
    {"hotkey_add_virtual_user", HOTKEY_ADD_VIRTUAL_USER},
    {"hotkey_remove_virtual_user", HOTKEY_REMOVE_VIRTUAL_USER},
    {"hotkey_volume_up", HOTKEY_VOLUME_UP},
    {"hotkey_volume_down", HOTKEY_VOLUME_DOWN},
};

const std::map<std::string, AxisMapping> string_to_axis_map = {
    {"axis_left_x_plus", {SDL_GAMEPAD_AXIS_LEFTX, 127}},
    {"axis_left_x_minus", {SDL_GAMEPAD_AXIS_LEFTX, -127}},
    {"axis_left_y_plus", {SDL_GAMEPAD_AXIS_LEFTY, 127}},
    {"axis_left_y_minus", {SDL_GAMEPAD_AXIS_LEFTY, -127}},
    {"axis_right_x_plus", {SDL_GAMEPAD_AXIS_RIGHTX, 127}},
    {"axis_right_x_minus", {SDL_GAMEPAD_AXIS_RIGHTX, -127}},
    {"axis_right_y_plus", {SDL_GAMEPAD_AXIS_RIGHTY, 127}},
    {"axis_right_y_minus", {SDL_GAMEPAD_AXIS_RIGHTY, -127}},

    {"l2", {SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 127}},
    {"r2", {SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 127}},

    // should only use these to bind analog inputs to analog outputs
    {"axis_left_x", {SDL_GAMEPAD_AXIS_LEFTX, 127}},
    {"axis_left_y", {SDL_GAMEPAD_AXIS_LEFTY, 127}},
    {"axis_right_x", {SDL_GAMEPAD_AXIS_RIGHTX, 127}},
    {"axis_right_y", {SDL_GAMEPAD_AXIS_RIGHTY, 127}},
};
const std::map<std::string, u32> string_to_keyboard_key_map = {
    // alphanumeric
    {"a", SDLK_A},
    {"b", SDLK_B},
    {"c", SDLK_C},
    {"d", SDLK_D},
    {"e", SDLK_E},
    {"f", SDLK_F},
    {"g", SDLK_G},
    {"h", SDLK_H},
    {"i", SDLK_I},
    {"j", SDLK_J},
    {"k", SDLK_K},
    {"l", SDLK_L},
    {"m", SDLK_M},
    {"n", SDLK_N},
    {"o", SDLK_O},
    {"p", SDLK_P},
    {"q", SDLK_Q},
    {"r", SDLK_R},
    {"s", SDLK_S},
    {"t", SDLK_T},
    {"u", SDLK_U},
    {"v", SDLK_V},
    {"w", SDLK_W},
    {"x", SDLK_X},
    {"y", SDLK_Y},
    {"z", SDLK_Z},
    {"0", SDLK_0},
    {"1", SDLK_1},
    {"2", SDLK_2},
    {"3", SDLK_3},
    {"4", SDLK_4},
    {"5", SDLK_5},
    {"6", SDLK_6},
    {"7", SDLK_7},
    {"8", SDLK_8},
    {"9", SDLK_9},

    // F keys
    {"f1", SDLK_F1},
    {"f2", SDLK_F2},
    {"f3", SDLK_F3},
    {"f4", SDLK_F4},
    {"f5", SDLK_F5},
    {"f6", SDLK_F6},
    {"f7", SDLK_F7},
    {"f8", SDLK_F8},
    {"f9", SDLK_F9},
    {"f10", SDLK_F10},
    {"f11", SDLK_F11},
    {"f12", SDLK_F12},

    // symbols
    {"grave", SDLK_GRAVE},
    {"tilde", SDLK_TILDE},
    {"exclamation", SDLK_EXCLAIM},
    {"at", SDLK_AT},
    {"hash", SDLK_HASH},
    {"dollar", SDLK_DOLLAR},
    {"percent", SDLK_PERCENT},
    {"caret", SDLK_CARET},
    {"ampersand", SDLK_AMPERSAND},
    {"asterisk", SDLK_ASTERISK},
    {"lparen", SDLK_LEFTPAREN},
    {"rparen", SDLK_RIGHTPAREN},
    {"minus", SDLK_MINUS},
    {"underscore", SDLK_UNDERSCORE},
    {"equals", SDLK_EQUALS},
    {"plus", SDLK_PLUS},
    {"lbracket", SDLK_LEFTBRACKET},
    {"rbracket", SDLK_RIGHTBRACKET},
    {"lbrace", SDLK_LEFTBRACE},
    {"rbrace", SDLK_RIGHTBRACE},
    {"backslash", SDLK_BACKSLASH},
    {"pipe", SDLK_PIPE},
    {"semicolon", SDLK_SEMICOLON},
    {"colon", SDLK_COLON},
    {"apostrophe", SDLK_APOSTROPHE},
    {"quote", SDLK_DBLAPOSTROPHE},
    {"comma", SDLK_COMMA},
    {"less", SDLK_LESS},
    {"period", SDLK_PERIOD},
    {"greater", SDLK_GREATER},
    {"slash", SDLK_SLASH},
    {"question", SDLK_QUESTION},

    // special keys
    {"escape", SDLK_ESCAPE},
    {"printscreen", SDLK_PRINTSCREEN},
    {"scrolllock", SDLK_SCROLLLOCK},
    {"pausebreak", SDLK_PAUSE},
    {"backspace", SDLK_BACKSPACE},
    {"delete", SDLK_DELETE},
    {"insert", SDLK_INSERT},
    {"home", SDLK_HOME},
    {"end", SDLK_END},
    {"pgup", SDLK_PAGEUP},
    {"pgdown", SDLK_PAGEDOWN},
    {"tab", SDLK_TAB},
    {"capslock", SDLK_CAPSLOCK},
    {"enter", SDLK_RETURN},
    {"lshift", SDLK_LSHIFT},
    {"rshift", SDLK_RSHIFT},
    {"lctrl", SDLK_LCTRL},
    {"rctrl", SDLK_RCTRL},
    {"lalt", SDLK_LALT},
    {"ralt", SDLK_RALT},
    {"lmeta", SDLK_LGUI},
    {"rmeta", SDLK_RGUI},
    {"lwin", SDLK_LGUI},
    {"rwin", SDLK_RGUI},
    {"space", SDLK_SPACE},
    {"up", SDLK_UP},
    {"down", SDLK_DOWN},
    {"left", SDLK_LEFT},
    {"right", SDLK_RIGHT},

    // keypad
    {"kp0", SDLK_KP_0},
    {"kp1", SDLK_KP_1},
    {"kp2", SDLK_KP_2},
    {"kp3", SDLK_KP_3},
    {"kp4", SDLK_KP_4},
    {"kp5", SDLK_KP_5},
    {"kp6", SDLK_KP_6},
    {"kp7", SDLK_KP_7},
    {"kp8", SDLK_KP_8},
    {"kp9", SDLK_KP_9},
    {"kpperiod", SDLK_KP_PERIOD},
    {"kpcomma", SDLK_KP_COMMA},
    {"kpslash", SDLK_KP_DIVIDE},
    {"kpasterisk", SDLK_KP_MULTIPLY},
    {"kpminus", SDLK_KP_MINUS},
    {"kpplus", SDLK_KP_PLUS},
    {"kpequals", SDLK_KP_EQUALS},
    {"kpenter", SDLK_KP_ENTER},

    // mouse
    {"leftbutton", SDL_BUTTON_LEFT},
    {"rightbutton", SDL_BUTTON_RIGHT},
    {"middlebutton", SDL_BUTTON_MIDDLE},
    {"sidebuttonback", SDL_BUTTON_X1},
    {"sidebuttonforward", SDL_BUTTON_X2},
    {"mousewheelup", SDL_MOUSE_WHEEL_UP},
    {"mousewheeldown", SDL_MOUSE_WHEEL_DOWN},
    {"mousewheelleft", SDL_MOUSE_WHEEL_LEFT},
    {"mousewheelright", SDL_MOUSE_WHEEL_RIGHT},

    // no binding
    {"unmapped", SDL_UNMAPPED},
};

void ParseInputConfig(const std::string game_id);

class InputBinding {
public:
    InputID keys[3];
    InputBinding(InputID k1 = InputID(), InputID k2 = InputID(), InputID k3 = InputID()) {
        // we format the keys so comparing them will be very fast, because we will only have to
        // compare 3 sorted elements, where the only possible duplicate item is 0

        // duplicate entries get changed to one original, one null
        if (k1 == k2 && k1 != InputID()) {
            k2 = InputID();
        }
        if (k1 == k3 && k1 != InputID()) {
            k3 = InputID();
        }
        if (k3 == k2 && k2 != InputID()) {
            k2 = InputID();
        }
        // this sorts them
        if (k1 <= k2 && k1 <= k3) {
            keys[0] = k1;
            if (k2 <= k3) {
                keys[1] = k2;
                keys[2] = k3;
            } else {
                keys[1] = k3;
                keys[2] = k2;
            }
        } else if (k2 <= k1 && k2 <= k3) {
            keys[0] = k2;
            if (k1 <= k3) {
                keys[1] = k1;
                keys[2] = k3;
            } else {
                keys[1] = k3;
                keys[2] = k1;
            }
        } else {
            keys[0] = k3;
            if (k1 <= k2) {
                keys[1] = k1;
                keys[2] = k2;
            } else {
                keys[1] = k2;
                keys[3] = k1;
            }
        }
    }
    // copy ctor
    InputBinding(const InputBinding& o) {
        keys[0] = o.keys[0];
        keys[1] = o.keys[1];
        keys[2] = o.keys[2];
    }

    inline bool operator==(const InputBinding& o) {
        // InputID() signifies an unused slot
        return (keys[0] == o.keys[0] || keys[0] == InputID() || o.keys[0] == InputID()) &&
               (keys[1] == o.keys[1] || keys[1] == InputID() || o.keys[1] == InputID()) &&
               (keys[2] == o.keys[2] || keys[2] == InputID() || o.keys[2] == InputID());
        // it is already very fast,
        // but reverse order makes it check the actual keys first instead of possible 0-s,
        // potenially skipping the later expressions of the three-way AND
    }
    inline int KeyCount() const {
        return (keys[0].IsValid() ? 1 : 0) + (keys[1].IsValid() ? 1 : 0) +
               (keys[2].IsValid() ? 1 : 0);
    }
    // Sorts by the amount of non zero keys - left side is 'bigger' here
    bool operator<(const InputBinding& other) const {
        return KeyCount() > other.KeyCount();
    }
    inline bool IsEmpty() {
        return !(keys[0].IsValid() || keys[1].IsValid() || keys[2].IsValid());
    }
    std::string ToString() {
        switch (KeyCount()) {
        case 1:
            return fmt::format("({})", keys[0].ToString());
        case 2:
            return fmt::format("({}, {})", keys[0].ToString(), keys[1].ToString());
        case 3:
            return fmt::format("({}, {}, {})", keys[0].ToString(), keys[1].ToString(),
                               keys[2].ToString());
        default:
            return "Empty";
        }
    }

    // returns an InputEvent based on the event type (keyboard, mouse buttons/wheel, or controller)
    static InputEvent GetInputEventFromSDLEvent(const SDL_Event& e);
};

class ControllerOutput {
public:
    static GameControllers controllers;
    static void GetGetGamepadIndexFromSDLJoystickID(const SDL_JoystickID id) {}
    static void LinkJoystickAxes();

    u32 button;
    u32 axis;
    u8 gamepad_id;
    // these are only used as s8,
    // but I added some padding to avoid overflow if it's activated by multiple inputs
    // axis_plus and axis_minus pairs share a common new_param, the other outputs have their own
    s16 old_param;
    s16* new_param;
    bool old_button_state, new_button_state, state_changed, positive_axis;

    ControllerOutput(const u32 b, u32 a = SDL_GAMEPAD_AXIS_INVALID, bool p = true) {
        button = b;
        axis = a;
        new_param = new s16(0);
        old_param = 0;
        positive_axis = p;
        gamepad_id = 0;
    }
    ControllerOutput(const ControllerOutput& o) : button(o.button), axis(o.axis) {
        new_param = new s16(*o.new_param);
    }
    ~ControllerOutput() {
        delete new_param;
    }
    inline bool operator==(const ControllerOutput& o) const { // fucking consts everywhere
        return button == o.button && axis == o.axis;
    }
    inline bool operator!=(const ControllerOutput& o) const {
        return button != o.button || axis != o.axis;
    }
    std::string ToString() const {
        return fmt::format("({}, {}, {})", (s32)button, (int)axis, old_param);
    }
    inline bool IsButton() const {
        return axis == SDL_GAMEPAD_AXIS_INVALID && button != SDL_GAMEPAD_BUTTON_INVALID;
    }
    inline bool IsAxis() const {
        return axis != SDL_GAMEPAD_AXIS_INVALID && button == SDL_GAMEPAD_BUTTON_INVALID;
    }

    void ResetUpdate();
    void AddUpdate(InputEvent event);
    void FinalizeUpdate(u8 gamepad_index);
};
class BindingConnection {
public:
    InputBinding binding;
    ControllerOutput* output;
    u32 axis_param;
    InputID toggle;

    BindingConnection(InputBinding b, ControllerOutput* out, u32 param = 0, InputID t = InputID()) {
        binding = b;
        axis_param = param;
        output = out;
        toggle = t;
    }
    BindingConnection& operator=(const BindingConnection& o) {
        binding = o.binding;
        output = o.output;
        axis_param = o.axis_param;
        toggle = o.toggle;
        return *this;
    }
    bool operator<(const BindingConnection& other) const {
        // a button is a higher priority than an axis, as buttons can influence axes
        // (e.g. joystick_halfmode)
        if (output->IsButton() &&
            (other.output->IsAxis() && (other.output->axis != SDL_GAMEPAD_AXIS_LEFT_TRIGGER &&
                                        other.output->axis != SDL_GAMEPAD_AXIS_RIGHT_TRIGGER))) {
            return true;
        }
        if (binding < other.binding) {
            return true;
        }
        return false;
    }
    bool HasGamepadInput() {
        for (auto& key : binding.keys) {
            if (key.type == InputType::Controller || key.type == InputType::Axis) {
                return true;
            }
        }
        return false;
    }
    BindingConnection CopyWithChangedGamepadId(u8 gamepad);
    InputEvent ProcessBinding();
};

class ControllerAllOutputs {
public:
    static constexpr u64 output_count = 41;
    std::array<ControllerOutput, output_count> data = {
        // Important: these have to be the first, or else they will update in the wrong order
        ControllerOutput(LEFTJOYSTICK_HALFMODE),
        ControllerOutput(RIGHTJOYSTICK_HALFMODE),
        ControllerOutput(KEY_TOGGLE),
        ControllerOutput(MOUSE_GYRO_ROLL_MODE),

        // Button mappings
        ControllerOutput(SDL_GAMEPAD_BUTTON_NORTH),           // Triangle
        ControllerOutput(SDL_GAMEPAD_BUTTON_EAST),            // Circle
        ControllerOutput(SDL_GAMEPAD_BUTTON_SOUTH),           // Cross
        ControllerOutput(SDL_GAMEPAD_BUTTON_WEST),            // Square
        ControllerOutput(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER),   // L1
        ControllerOutput(SDL_GAMEPAD_BUTTON_LEFT_STICK),      // L3
        ControllerOutput(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER),  // R1
        ControllerOutput(SDL_GAMEPAD_BUTTON_RIGHT_STICK),     // R3
        ControllerOutput(SDL_GAMEPAD_BUTTON_START),           // Options
        ControllerOutput(SDL_GAMEPAD_BUTTON_TOUCHPAD_LEFT),   // TouchPad
        ControllerOutput(SDL_GAMEPAD_BUTTON_TOUCHPAD_CENTER), // TouchPad
        ControllerOutput(SDL_GAMEPAD_BUTTON_TOUCHPAD_RIGHT),  // TouchPad
        ControllerOutput(SDL_GAMEPAD_BUTTON_DPAD_UP),         // Up
        ControllerOutput(SDL_GAMEPAD_BUTTON_DPAD_DOWN),       // Down
        ControllerOutput(SDL_GAMEPAD_BUTTON_DPAD_LEFT),       // Left
        ControllerOutput(SDL_GAMEPAD_BUTTON_DPAD_RIGHT),      // Right

        // Axis mappings
        // ControllerOutput(SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_AXIS_LEFTX, false),
        // ControllerOutput(SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_AXIS_LEFTY, false),
        // ControllerOutput(SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_AXIS_RIGHTX, false),
        // ControllerOutput(SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_AXIS_RIGHTY, false),
        ControllerOutput(SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_AXIS_LEFTX),
        ControllerOutput(SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_AXIS_LEFTY),
        ControllerOutput(SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_AXIS_RIGHTX),
        ControllerOutput(SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_AXIS_RIGHTY),

        ControllerOutput(SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_AXIS_LEFT_TRIGGER),
        ControllerOutput(SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER),

        ControllerOutput(HOTKEY_FULLSCREEN),
        ControllerOutput(HOTKEY_PAUSE),
        ControllerOutput(HOTKEY_SIMPLE_FPS),
        ControllerOutput(HOTKEY_QUIT),
        ControllerOutput(HOTKEY_RELOAD_INPUTS),
        ControllerOutput(HOTKEY_TOGGLE_MOUSE_TO_JOYSTICK),
        ControllerOutput(HOTKEY_TOGGLE_MOUSE_TO_GYRO),
        ControllerOutput(HOTKEY_TOGGLE_MOUSE_TO_TOUCHPAD),
        ControllerOutput(HOTKEY_RENDERDOC),
        ControllerOutput(HOTKEY_SCREENSHOT_WITH_OVERLAYS),
        ControllerOutput(HOTKEY_ADD_VIRTUAL_USER),
        ControllerOutput(HOTKEY_REMOVE_VIRTUAL_USER),
        ControllerOutput(HOTKEY_VOLUME_UP),
        ControllerOutput(HOTKEY_VOLUME_DOWN),

        ControllerOutput(SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_AXIS_INVALID),
    };
    ControllerAllOutputs(u8 g) {
        for (int i = 0; i < output_count; i++) {
            data[i].gamepad_id = g;
        }
    }
};

// Updates the list of pressed keys with the given input.
// Returns whether the list was updated or not.
bool UpdatePressedKeys(InputEvent event);

void ActivateOutputsFromInputs();


struct GlobalKeyboardState {
    std::mutex queue_mutex;
    std::queue<OrbisImeEvent> event_queue;

    void PushEvent(const OrbisImeEvent& ev) {
        std::lock_guard<std::mutex> lock(queue_mutex);
        event_queue.push(ev);
    }

    bool PopEvent(OrbisImeEvent& out) {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (event_queue.empty())
            return false;
        out = event_queue.front();
        event_queue.pop();
        return true;
    }
};
extern GlobalKeyboardState g_keyboard_state;
extern std::queue<SceMouseData> g_mouse_state;
extern std::mutex g_mouse_mutex;

enum class OrbisImeKeycodeEnum : u16 {
    NoEvent = 0x00,
    ErrorRollover = 0x01,
    PostFail = 0x02,
    ErrorUndefined = 0x03,
    A = 0x04,
    B = 0x05,
    C = 0x06,
    D = 0x07,
    E = 0x08,
    F = 0x09,
    G = 0x0A,
    H = 0x0B,
    I = 0x0C,
    J = 0x0D,
    K = 0x0E,
    L = 0x0F,
    M = 0x10,
    N = 0x11,
    O = 0x12,
    P = 0x13,
    Q = 0x14,
    R = 0x15,
    S = 0x16,
    T = 0x17,
    U = 0x18,
    V = 0x19,
    W = 0x1A,
    X = 0x1B,
    Y = 0x1C,
    Z = 0x1D,
    Key1 = 0x1E,
    Key2 = 0x1F,
    Key3 = 0x20,
    Key4 = 0x21,
    Key5 = 0x22,
    Key6 = 0x23,
    Key7 = 0x24,
    Key8 = 0x25,
    Key9 = 0x26,
    Key0 = 0x27,
    Return = 0x28,
    Escape = 0x29,
    Backspace = 0x2A,
    Tab = 0x2B,
    Spacebar = 0x2C,
    Minus = 0x2D,
    Equal = 0x2E,
    LeftBracket = 0x2F,
    RightBracket = 0x30,
    Backslash = 0x31,
    NonUsPound = 0x32,
    Semicolon = 0x33,
    SingleQuote = 0x34,
    BackQuote = 0x35,
    Comma = 0x36,
    Period = 0x37,
    Slash = 0x38,
    CapsLock = 0x39,
    F1 = 0x3A,
    F2 = 0x3B,
    F3 = 0x3C,
    F4 = 0x3D,
    F5 = 0x3E,
    F6 = 0x3F,
    F7 = 0x40,
    F8 = 0x41,
    F9 = 0x42,
    F10 = 0x43,
    F11 = 0x44,
    F12 = 0x45,
    PrintScreen = 0x46,
    ScrollLock = 0x47,
    Pause = 0x48,
    Insert = 0x49,
    Home = 0x4A,
    PageUp = 0x4B,
    Delete = 0x4C,
    End = 0x4D,
    PageDown = 0x4E,
    RightArrow = 0x4F,
    LeftArrow = 0x50,
    DownArrow = 0x51,
    UpArrow = 0x52,
    KeypadNumLock = 0x53,
    KeypadSlash = 0x54,
    KeypadAsterisk = 0x55,
    KeypadMinus = 0x56,
    KeypadPlus = 0x57,
    KeypadEnter = 0x58,
    Keypad1 = 0x59,
    Keypad2 = 0x5A,
    Keypad3 = 0x5B,
    Keypad4 = 0x5C,
    Keypad5 = 0x5D,
    Keypad6 = 0x5E,
    Keypad7 = 0x5F,
    Keypad8 = 0x60,
    Keypad9 = 0x61,
    Keypad0 = 0x62,
    KeypadPeriod = 0x63,
    NonUsBackslash = 0x64,
    Application = 0x65,
    Power = 0x66,
    KeypadEqual = 0x67,
    F13 = 0x68,
    F14 = 0x69,
    F15 = 0x6A,
    F16 = 0x6B,
    F17 = 0x6C,
    F18 = 0x6D,
    F19 = 0x6E,
    F20 = 0x6F,
    F21 = 0x70,
    F22 = 0x71,
    F23 = 0x72,
    F24 = 0x73,
    Execute = 0x74,
    Help = 0x75,
    Menu = 0x76,
    Select = 0x77,
    Stop = 0x78,
    Again = 0x79,
    Undo = 0x7A,
    Cut = 0x7B,
    Copy = 0x7C,
    Paste = 0x7D,
    Find = 0x7E,
    Mute = 0x7F,
    VolumeUp = 0x80,
    VolumeDown = 0x81,
    LockingCapsLock = 0x82,
    LockingNumLock = 0x83,
    LockingScrollLock = 0x84,
    KeypadComma = 0x85,
    KeypadEqualSign = 0x86,
    International1 = 0x87,
    International2 = 0x88,
    International3 = 0x89,
    International4 = 0x8A,
    International5 = 0x8B,
    International6 = 0x8C,
    International7 = 0x8D,
    International8 = 0x8E,
    International9 = 0x8F,
    Lang1 = 0x90,
    Lang2 = 0x91,
    Lang3 = 0x92,
    Lang4 = 0x93,
    Lang5 = 0x94,
    Lang6 = 0x95,
    Lang7 = 0x96,
    Lang8 = 0x97,
    Lang9 = 0x98,
    AltErase = 0x99,
    SysReq = 0x9A,
    Cancel = 0x9B,
    Clear = 0x9C,
    Prior = 0x9D,
    Return2 = 0x9E,
    Separator = 0x9F,
    Out = 0xA0,
    Oper = 0xA1,
    ClearAgain = 0xA2,
    CrSelProps = 0xA3,
    ExSel = 0xA4,
    Keypad00 = 0xB0,
    Keypad000 = 0xB1,
    ThousandsSeparator = 0xB2,
    DecimalSeparator = 0xB3,
    CurrencyUnit = 0xB4,
    CurrencySubunit = 0xB5,
    KeypadLeftParenthesis = 0xB6,
    KeypadRightParenthesis = 0xB7,
    KeypadLeftCurlyBracket = 0xB8,
    KeypadRightCurlyBracket = 0xB9,
    KeypadTab = 0xBA,
    KeypadBackspace = 0xBB,
    KeypadA = 0xBC,
    KeypadB = 0xBD,
    KeypadC = 0xBE,
    KeypadD = 0xBF,
    KeypadE = 0xC0,
    KeypadF = 0xC1,
    KeypadXor = 0xC2,
    KeypadHat = 0xC3,
    KeypadPercent = 0xC4,
    KeypadLessThan = 0xC5,
    KeypadGreaterThan = 0xC6,
    KeypadAmpersand = 0xC7,
    KeypadLogicalAnd = 0xC8,
    KeypadOr = 0xC9,
    KeypadLogicalOr = 0xCA,
    KeypadColon = 0xCB,
    KeypadNumber = 0xCC,
    KeypadSpace = 0xCD,
    KeypadAtSign = 0xCE,
    KeypadExclamation = 0xCF,
    KeypadMemoryStore = 0xD0,
    KeypadMemoryRecall = 0xD1,
    KeypadMemoryClear = 0xD2,
    KeypadMemoryAdd = 0xD3,
    KeypadMemorySubtract = 0xD4,
    KeypadMemoryMultiply = 0xD5,
    KeypadMemoryDivide = 0xD6,
    KeypadPlusMinus = 0xD7,
    KeypadClear = 0xD8,
    KeypadClearEntry = 0xD9,
    KeypadBinary = 0xDA,
    KeypadOctal = 0xDB,
    KeypadDecimal = 0xDC,
    KeypadHexadecimal = 0xDD,
    LeftControl = 0xE0,
    LeftShift = 0xE1,
    LeftAlt = 0xE2,
    LeftGui = 0xE3,
    RightControl = 0xE4,
    RightShift = 0xE5,
    RightAlt = 0xE6,
    RightGui = 0xE7,
};

} // namespace Input
