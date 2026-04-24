// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "SDL3/SDL_events.h"
#include "SDL3/SDL_hints.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_properties.h"
#include "SDL3/SDL_timer.h"
#include "SDL3/SDL_video.h"
#include "common/assert.h"
#include "common/elf_info.h"
#include "core/debug_state.h"
#include "core/devtools/layer.h"
#include "core/emulator_settings.h"
#include "core/libraries/kernel/time.h"
#include "core/libraries/pad/pad.h"
#include "core/libraries/system/userservice.h"
#include "core/user_settings.h"
#include "imgui/renderer/imgui_core.h"
#include "input/controller.h"
#include "input/input_handler.h"
#include "input/input_mouse.h"
#include "sdl_window.h"
#include "video_core/renderdoc.h"
#include "input/input_handler.h"

#ifdef __APPLE__
#include "SDL3/SDL_metal.h"
#endif
#include <core/emulator_settings.h>
#include <core/libraries/ime/ime_common.h>
#include <core/libraries/ime/ime.h>

#define SCE_MOUSE_BUTTON_PRIMARY 0x00000001
#define SCE_MOUSE_BUTTON_SECONDARY 0x00000002
#define SCE_MOUSE_BUTTON_OPTIONAL 0x00000004

namespace Frontend {

using namespace Libraries::Pad;

static OrbisPadButtonDataOffset SDLGamepadToOrbisButton(u8 button) {
    using OPBDO = OrbisPadButtonDataOffset;

    switch (button) {
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return OPBDO::Down;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return OPBDO::Up;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return OPBDO::Left;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return OPBDO::Right;
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return OPBDO::Cross;
    case SDL_GAMEPAD_BUTTON_NORTH:
        return OPBDO::Triangle;
    case SDL_GAMEPAD_BUTTON_WEST:
        return OPBDO::Square;
    case SDL_GAMEPAD_BUTTON_EAST:
        return OPBDO::Circle;
    case SDL_GAMEPAD_BUTTON_START:
        return OPBDO::Options;
    case SDL_GAMEPAD_BUTTON_TOUCHPAD:
        return OPBDO::TouchPad;
    case SDL_GAMEPAD_BUTTON_BACK:
        return OPBDO::TouchPad;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return OPBDO::L1;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return OPBDO::R1;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return OPBDO::L3;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return OPBDO::R3;
    default:
        return OPBDO::None;
    }
}

static Uint32 SDLCALL PollController(void* userdata, SDL_TimerID timer_id, Uint32 interval) {
    auto* controller = reinterpret_cast<Input::GameController*>(userdata);
    controller->UpdateAxisSmoothing();
    controller->Gyro(0);
    controller->Acceleration(0);
    return interval;
}

static Uint32 SDLCALL PollControllerLightColour(void* userdata, SDL_TimerID timer_id,
                                                Uint32 interval) {
    auto* controller = reinterpret_cast<Input::GameController*>(userdata);
    controller->PollLightColour();
    return interval;
}

WindowSDL::WindowSDL(s32 width_, s32 height_, Input::GameControllers* controllers_,
                     std::string_view window_title)
    : width{width_}, height{height_}, controllers{*controllers_} {
    if (!SDL_SetHint(SDL_HINT_APP_NAME, "shadPS4")) {
        UNREACHABLE_MSG("Failed to set SDL window hint: {}", SDL_GetError());
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        UNREACHABLE_MSG("Failed to initialize SDL video subsystem: {}", SDL_GetError());
    }
    if (!SDL_Init(SDL_INIT_CAMERA)) {
        LOG_ERROR(Input, "Failed to initialize SDL camera subsystem: {}", SDL_GetError());
    }
    SDL_InitSubSystem(SDL_INIT_AUDIO);

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING,
                          std::string(window_title).c_str());
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
    SDL_SetNumberProperty(props, "flags", SDL_WINDOW_VULKAN);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (window == nullptr) {
        UNREACHABLE_MSG("Failed to create window handle: {}", SDL_GetError());
    }

    SDL_SetWindowMinimumSize(window, 640, 360);

    bool error = false;
    const SDL_DisplayID displayIndex = SDL_GetDisplayForWindow(window);
    if (displayIndex < 0) {
        LOG_ERROR(Frontend, "Error getting display index: {}", SDL_GetError());
        error = true;
    }
    const SDL_DisplayMode* displayMode;
    if ((displayMode = SDL_GetCurrentDisplayMode(displayIndex)) == 0) {
        LOG_ERROR(Frontend, "Error getting display mode: {}", SDL_GetError());
        error = true;
    }
    if (!error) {
        SDL_SetWindowFullscreenMode(
            window, EmulatorSettings.GetFullScreenMode() == "Fullscreen" ? displayMode : NULL);
    }
    SDL_SetWindowFullscreen(window, EmulatorSettings.IsFullScreen());
    SDL_SyncWindow(window);

    SDL_InitSubSystem(SDL_INIT_GAMEPAD);

#if defined(SDL_PLATFORM_WIN32)
    window_info.type = WindowSystemType::Windows;
    window_info.render_surface = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
#elif defined(SDL_PLATFORM_LINUX) || defined(__FreeBSD__)
    // SDL doesn't have a platform define for FreeBSD AAAAAAAAAA
    if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0) {
        window_info.type = WindowSystemType::X11;
        window_info.display_connection = SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
        window_info.render_surface = (void*)SDL_GetNumberProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    } else if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
        window_info.type = WindowSystemType::Wayland;
        window_info.display_connection = SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
        window_info.render_surface = SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
    }
#elif defined(SDL_PLATFORM_MACOS)
    window_info.type = WindowSystemType::Metal;
    window_info.render_surface = SDL_Metal_GetLayer(SDL_Metal_CreateView(window));
#endif
    // input handler init-s
    Input::ControllerOutput::LinkJoystickAxes();
    Input::ParseInputConfig(std::string(Common::ElfInfo::Instance().GameSerial()));
    controllers.TryOpenSDLControllers();

    if (EmulatorSettings.IsBackgroundControllerInput()) {
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    }
}

WindowSDL::~WindowSDL() = default;

void WindowSDL::WaitEvent() {
    // Called on main thread
    SDL_Event event;

    if (!SDL_WaitEvent(&event)) {
        return;
    }

    if (ImGui::Core::ProcessEvent(&event)) {
        return;
    }

    switch (event.type) {
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
        OnResize();
        break;
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_EXPOSED:
        is_shown = event.type == SDL_EVENT_WINDOW_EXPOSED;
        OnResize();
        break;
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_MOUSE_WHEEL_OFF:
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        OnKeyboardMouseInput(&event);
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
    case SDL_EVENT_GAMEPAD_REMOVED:
        controllers.TryOpenSDLControllers();
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
        OnGamepadEvent(&event);
        break;
    case SDL_EVENT_QUIT:
        is_open = false;
        break;
    case SDL_EVENT_QUIT_DIALOG:
        Overlay::ToggleQuitWindow();
        break;
    case SDL_EVENT_TOGGLE_FULLSCREEN: {
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) {
            SDL_SetWindowFullscreen(window, 0);
        } else {
            SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
        }
        break;
    }
    case SDL_EVENT_TOGGLE_PAUSE:
        if (DebugState.IsGuestThreadsPaused()) {
            LOG_INFO(Frontend, "Game Resumed");
            DebugState.ResumeGuestThreads();
        } else {
            LOG_INFO(Frontend, "Game Paused");
            DebugState.PauseGuestThreads();
        }
        break;
    case SDL_EVENT_CHANGE_CONTROLLER:
        UNREACHABLE_MSG("todo");
        break;
    case SDL_EVENT_TOGGLE_SIMPLE_FPS:
        Overlay::ToggleSimpleFps();
        break;
    case SDL_EVENT_RELOAD_INPUTS:
        Input::ParseInputConfig(std::string(Common::ElfInfo::Instance().GameSerial()));
        break;
    case SDL_EVENT_MOUSE_TO_JOYSTICK:
        SDL_SetWindowRelativeMouseMode(this->GetSDLWindow(),
                                       Input::ToggleMouseModeTo(Input::MouseMode::Joystick));
        break;
    case SDL_EVENT_MOUSE_TO_GYRO:
        SDL_SetWindowRelativeMouseMode(this->GetSDLWindow(),
                                       Input::ToggleMouseModeTo(Input::MouseMode::Gyro));
        break;
    case SDL_EVENT_MOUSE_TO_TOUCHPAD:
        SDL_SetWindowRelativeMouseMode(this->GetSDLWindow(),
                                       Input::ToggleMouseModeTo(Input::MouseMode::Touchpad));
        SDL_SetWindowRelativeMouseMode(this->GetSDLWindow(), false);
        break;
    case SDL_EVENT_ADD_VIRTUAL_USER:
        for (int i = 0; i < 4; i++) {
            if (controllers[i]->user_id == -1) {
                auto u = UserManagement.GetUserByPlayerIndex(i + 1);
                if (!u) {
                    break;
                }
                controllers[i]->user_id = u->user_id;
                UserManagement.LoginUser(u, i + 1);
                break;
            }
        }
        break;
    case SDL_EVENT_REMOVE_VIRTUAL_USER:
        LOG_INFO(Input, "Remove user");
        for (int i = 3; i >= 0; i--) {
            if (controllers[i]->user_id != -1) {
                UserManagement.LogoutUser(UserManagement.GetUserByID(controllers[i]->user_id));
                controllers[i]->user_id = -1;
                break;
            }
        }
        break;
    case SDL_EVENT_RDOC_CAPTURE:
        if (VideoCore::IsRenderDocLoaded()) {
            VideoCore::TriggerCapture();
        } else {
            VideoCore::RequestScreenshot(VideoCore::ScreenshotRequest::GameOnly);
        }
        break;
    case SDL_EVENT_SCREENSHOT_WITH_OVERLAYS:
        VideoCore::RequestScreenshot(VideoCore::ScreenshotRequest::WithOverlays);
        break;
    default:
        break;
    }
}

void WindowSDL::InitTimers() {
    for (int i = 0; i < 4; ++i) {
        SDL_AddTimer(4, &PollController, controllers[i]);
    }
    SDL_AddTimer(33, Input::MousePolling, (void*)controllers[0]);
}

void WindowSDL::RequestKeyboard() {
    if (keyboard_grab == 0) {
        SDL_RunOnMainThread(
            [](void* userdata) { SDL_StartTextInput(static_cast<SDL_Window*>(userdata)); }, window,
            true);
    }
    keyboard_grab++;
}

void WindowSDL::ReleaseKeyboard() {
    ASSERT(keyboard_grab > 0);
    keyboard_grab--;
    if (keyboard_grab == 0) {
        SDL_RunOnMainThread(
            [](void* userdata) { SDL_StopTextInput(static_cast<SDL_Window*>(userdata)); }, window,
            true);
    }
}

void WindowSDL::CaptureMouse(bool capture) {
    SDL_SetWindowRelativeMouseMode(window, capture);
    if (capture)
        SDL_HideCursor();
    else
        SDL_ShowCursor();
}

void WindowSDL::SetShouldIgnoreCustomMappings(bool ignore) 
{
    should_ignore_custom_mappings = ignore;
}

void WindowSDL::OnResize() {
    SDL_GetWindowSizeInPixels(window, &width, &height);
    ImGui::Core::OnResize();
}

Uint32 wheelOffCallback(void* og_event, Uint32 timer_id, Uint32 interval) {
    SDL_Event off_event = *(SDL_Event*)og_event;
    off_event.type = SDL_EVENT_MOUSE_WHEEL_OFF;
    SDL_PushEvent(&off_event);
    delete (SDL_Event*)og_event;
    return 0;
}

u16 MapSDLKeyToOrbisKeycode(SDL_Keycode key) {
    // If the key is a scancode-based key (mask set), extract the low byte.
    if (key & SDLK_SCANCODE_MASK) {
        // The scancode is the lower 16 bits (actually lower byte? but safe to mask)
        u32 scancode = key & ~SDLK_SCANCODE_MASK;
        // Scancode values match HID usage IDs directly.
        if (scancode <= 0xE7) {
            return static_cast<u16>(scancode);
        }
        // Some extended keys might have extra bits; fallback to 0.
        return 0;
    }

    // Printable characters (ASCII range 0x20-0x7F, plus a few extended like 0xB1)
    // Map common symbols to HID usage IDs.
    switch (key) {
    case SDLK_RETURN:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Return);
    case SDLK_ESCAPE:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Escape);
    case SDLK_BACKSPACE:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Backspace);
    case SDLK_TAB:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Tab);
    case SDLK_SPACE:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Spacebar);
    case SDLK_EXCLAIM: // '!' -> Shift+1 -> usage 0x1E (Key1)
    case SDLK_1:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Key1);
    case SDLK_DBLAPOSTROPHE: // '"' -> Shift+'
    case SDLK_APOSTROPHE:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::SingleQuote);
    case SDLK_HASH: // '#' -> Shift+3
    case SDLK_3:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Key3);
    case SDLK_DOLLAR: // '$' -> Shift+4
    case SDLK_4:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Key4);
    case SDLK_PERCENT: // '%' -> Shift+5
    case SDLK_5:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Key5);
    case SDLK_AMPERSAND: // '&' -> Shift+7
    case SDLK_7:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Key7);
    case SDLK_LEFTPAREN: // '(' -> Shift+9
    case SDLK_9:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Key9);
    case SDLK_RIGHTPAREN: // ')' -> Shift+0
    case SDLK_0:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Key0);
    case SDLK_ASTERISK: // '*' -> Shift+8
    case SDLK_8:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Key8);
    case SDLK_PLUS: // '+' -> Shift+=
    case SDLK_EQUALS:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Equal);
    case SDLK_COMMA:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Comma);
    case SDLK_MINUS:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Minus);
    case SDLK_PERIOD:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Period);
    case SDLK_SLASH:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Slash);
    case SDLK_COLON: // ':' -> Shift+;
    case SDLK_SEMICOLON:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Semicolon);
    case SDLK_LESS:     // '<' -> Shift+,
    case SDLK_GREATER:  // '>' -> Shift+.
    case SDLK_QUESTION: // '?' -> Shift+/
    case SDLK_AT:       // '@' -> Shift+2
    case SDLK_2:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Key2);
    case SDLK_LEFTBRACKET:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::LeftBracket);
    case SDLK_BACKSLASH:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Backslash);
    case SDLK_RIGHTBRACKET:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::RightBracket);
    case SDLK_CARET: // '^' -> Shift+6
    case SDLK_6:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Key6);
    case SDLK_UNDERSCORE: // '_' -> Shift+-
    case SDLK_GRAVE:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::BackQuote);
    case SDLK_LEFTBRACE:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::LeftBracket); // same as [?
    case SDLK_PIPE:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Backslash);
    case SDLK_RIGHTBRACE:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::RightBracket);
    case SDLK_TILDE:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::BackQuote);
    case SDLK_DELETE:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Delete);
    case SDLK_PLUSMINUS:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::KeypadPlusMinus);
    // Letters: ASCII a-z
    case SDLK_A:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::A);
    case SDLK_B:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::B);
    case SDLK_C:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::C);
    case SDLK_D:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::D);
    case SDLK_E:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::E);
    case SDLK_F:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::F);
    case SDLK_G:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::G);
    case SDLK_H:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::H);
    case SDLK_I:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::I);
    case SDLK_J:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::J);
    case SDLK_K:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::K);
    case SDLK_L:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::L);
    case SDLK_M:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::M);
    case SDLK_N:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::N);
    case SDLK_O:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::O);
    case SDLK_P:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::P);
    case SDLK_Q:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Q);
    case SDLK_R:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::R);
    case SDLK_S:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::S);
    case SDLK_T:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::T);
    case SDLK_U:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::U);
    case SDLK_V:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::V);
    case SDLK_W:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::W);
    case SDLK_X:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::X);
    case SDLK_Y:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Y);
    case SDLK_Z:
        return static_cast<u16>(Input::OrbisImeKeycodeEnum::Z);
    default:
        break;
    }

    // If we have an extended key with the extended mask (0x20000000), try to map it.
    if (key & SDLK_EXTENDED_MASK) {
        // For simplicity, fall back to scancode extraction if the key is not recognized.
        u32 scancode = key & ~(SDLK_SCANCODE_MASK | SDLK_EXTENDED_MASK);
        if (scancode <= 0xE7)
            return static_cast<u16>(scancode);
    }

    return 0; // Unknown
}

char16_t GetCharacterFromKey(SDL_Keycode key, SDL_Keymod mod_state, bool is_keypad) {
    // Non‑printable keys return 0
    if (key < SDLK_SPACE || key > SDLK_DELETE) {
        return 0;
    }

    // For keypad keys, we might want to map them separately; for simplicity, treat as unshifted
    // digit.
    if (is_keypad) {
        // Keypad digits produce the same character as main keyboard digits (no shift effect)
        switch (key) {
        case SDLK_KP_0:
            return u'0';
        case SDLK_KP_1:
            return u'1';
        case SDLK_KP_2:
            return u'2';
        case SDLK_KP_3:
            return u'3';
        case SDLK_KP_4:
            return u'4';
        case SDLK_KP_5:
            return u'5';
        case SDLK_KP_6:
            return u'6';
        case SDLK_KP_7:
            return u'7';
        case SDLK_KP_8:
            return u'8';
        case SDLK_KP_9:
            return u'9';
        case SDLK_KP_PERIOD:
            return u'.';
        case SDLK_KP_DIVIDE:
            return u'/';
        case SDLK_KP_MULTIPLY:
            return u'*';
        case SDLK_KP_MINUS:
            return u'-';
        case SDLK_KP_PLUS:
            return u'+';
        case SDLK_KP_ENTER:
            return u'\r';
        default:
            return 0;
        }
    }

    // Determine if Shift is active (either left or right)
    bool shift = (mod_state & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT)) != 0;
    bool caps = (mod_state & SDL_KMOD_CAPS) != 0;

    // For letters: apply Shift XOR Caps to determine case
    if (key >= SDLK_A && key <= SDLK_Z) {
        bool upper = shift ^ caps;                  // XOR: Caps inverts the effect of Shift
        char16_t base = static_cast<char16_t>(key); // SDLK_a = 'a' (97)
        if (upper) {
            return base - u'a' + u'A';
        } else {
            return base;
        }
    }

    // For numbers and symbols: use a lookup table for shifted versions
    static const struct {
        SDL_Keycode unshifted;
        char16_t unshifted_char;
        char16_t shifted_char;
    } symbol_map[] = {
        {SDLK_SPACE, u' ', u' '},
        {SDLK_EXCLAIM, u'!', u'!'}, // Actually exclam is already shifted; handle via key
        {SDLK_APOSTROPHE, u'\'', u'"'},
        {SDLK_HASH, u'#', u'#'},
        {SDLK_DOLLAR, u'$', u'$'},
        {SDLK_PERCENT, u'%', u'%'},
        {SDLK_AMPERSAND, u'&', u'&'},
        {SDLK_LEFTPAREN, u'(', u'('},
        {SDLK_RIGHTPAREN, u')', u')'},
        {SDLK_ASTERISK, u'*', u'*'},
        {SDLK_PLUS, u'+', u'+'},
        {SDLK_COMMA, u',', u'<'},
        {SDLK_MINUS, u'-', u'_'},
        {SDLK_PERIOD, u'.', u'>'},
        {SDLK_SLASH, u'/', u'?'},
        {SDLK_0, u'0', u')'},
        {SDLK_1, u'1', u'!'},
        {SDLK_2, u'2', u'@'},
        {SDLK_3, u'3', u'#'},
        {SDLK_4, u'4', u'$'},
        {SDLK_5, u'5', u'%'},
        {SDLK_6, u'6', u'^'},
        {SDLK_7, u'7', u'&'},
        {SDLK_8, u'8', u'*'},
        {SDLK_9, u'9', u'('},
        {SDLK_COLON, u':', u':'},
        {SDLK_SEMICOLON, u';', u':'},
        {SDLK_LESS, u'<', u'<'},
        {SDLK_EQUALS, u'=', u'+'},
        {SDLK_GREATER, u'>', u'>'},
        {SDLK_QUESTION, u'?', u'?'},
        {SDLK_AT, u'@', u'@'},
        {SDLK_LEFTBRACKET, u'[', u'{'},
        {SDLK_BACKSLASH, u'\\', u'|'},
        {SDLK_RIGHTBRACKET, u']', u'}'},
        {SDLK_CARET, u'^', u'^'},
        {SDLK_UNDERSCORE, u'_', u'_'},
        {SDLK_GRAVE, u'`', u'~'},
        {SDLK_LEFTBRACE, u'{', u'{'},
        {SDLK_PIPE, u'|', u'|'},
        {SDLK_RIGHTBRACE, u'}', u'}'},
        {SDLK_TILDE, u'~', u'~'},
        {SDLK_DELETE, u'\x7F', u'\x7F'},
    };

    for (const auto& entry : symbol_map) {
        if (key == entry.unshifted) {
            return shift ? entry.shifted_char : entry.unshifted_char;
        }
    }

    // Fallback: return the keycode as a character if it's within ASCII range (unlikely)
    if (key >= 0x20 && key <= 0x7E) {
        return static_cast<char16_t>(key);
    }
    return 0;
}

OrbisImeKeycode MakeOrbisKeycode(const SDL_Event* event, u32 user_id) {
    OrbisImeKeycode kc = {};
    kc.keycode = static_cast<u16>(event->key.scancode); // Direct USB HID usage ID
    kc.user_id = user_id;
    kc.resource_id = 0;
    kc.type = OrbisImeKeyboardType::ENGLISH_US; // Assume standard keyboard
    // Get timestamp (SDL provides event->key.timestamp in ms; convert to OrbisRtcTick)
    kc.timestamp.tick = event->key.timestamp * 1000; // Convert ms to microseconds? Check definition


    // Build status flags (OrbisImeKeycodeState)
    u32 status = (u32)OrbisImeKeycodeState::KEYCODE_VALID;
    SDL_Keymod mod = event->key.mod;
    if (mod & SDL_KMOD_LCTRL)
        status |= (u32)OrbisImeKeycodeState::MODIFIER_L_CTRL;
    if (mod & SDL_KMOD_RCTRL)
        status |= (u32)OrbisImeKeycodeState::MODIFIER_R_CTRL;
    if (mod & SDL_KMOD_LSHIFT)
        status |= (u32)OrbisImeKeycodeState::MODIFIER_L_SHIFT;
    if (mod & SDL_KMOD_RSHIFT)
        status |= (u32)OrbisImeKeycodeState::MODIFIER_R_SHIFT;
    if (mod & SDL_KMOD_LALT)
        status |= (u32)OrbisImeKeycodeState::MODIFIER_L_ALT;
    if (mod & SDL_KMOD_RALT)
        status |= (u32)OrbisImeKeycodeState::MODIFIER_R_ALT;
    if (mod & SDL_KMOD_LGUI)
        status |= (u32)OrbisImeKeycodeState::MODIFIER_L_GUI;
    if (mod & SDL_KMOD_RGUI)
        status |= (u32)OrbisImeKeycodeState::MODIFIER_R_GUI;
    if (mod & SDL_KMOD_CAPS)
        status |= (u32)OrbisImeKeycodeState::LED_CAPS_LOCK;
    if (mod & SDL_KMOD_NUM)
        status |= (u32)OrbisImeKeycodeState::LED_NUM_LOCK;
    // ScrollLock not available in mod, query separately if needed
    if (event->key.repeat)
        status |= (u32)OrbisImeKeycodeState::CONTINUOUS_EVENT;

    kc.status = status;

    // Determine if this key is from the keypad (scancode range 0x54-0x67 etc.)
    bool is_keypad = (event->key.scancode >= SDL_SCANCODE_KP_DIVIDE &&
                      event->key.scancode <= SDL_SCANCODE_KP_EQUALS);
    kc.character = GetCharacterFromKey(event->key.key, mod, is_keypad);

    return kc;
}

void WindowSDL::OnKeyboardMouseInput(const SDL_Event* event) {
    using Libraries::Pad::OrbisPadButtonDataOffset;

    // get the event's id, if it's keyup or keydown
    const bool input_down = event->type == SDL_EVENT_KEY_DOWN ||
                            event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                            event->type == SDL_EVENT_MOUSE_WHEEL;
    Input::InputEvent input_event = Input::InputBinding::GetInputEventFromSDLEvent(*event);

    // if it's a wheel event, make a timer that turns it off after a set time
    if (event->type == SDL_EVENT_MOUSE_WHEEL) {
        const SDL_Event* copy = new SDL_Event(*event);
        SDL_AddTimer(33, wheelOffCallback, (void*)copy);
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION || event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        event->type == SDL_EVENT_MOUSE_BUTTON_UP || event->type == SDL_EVENT_MOUSE_WHEEL) {
        SceMouseData mouseData{};
        mouseData.timestamp = SDL_GetTicks();
        mouseData.connected = true;
        
        const SDL_MouseButtonFlags buttonState = SDL_GetMouseState(nullptr, nullptr);
        if (buttonState & SDL_BUTTON_MASK(SDL_BUTTON_LEFT))
            mouseData.buttons |= SCE_MOUSE_BUTTON_PRIMARY;
        if (buttonState & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT))
            mouseData.buttons |= SCE_MOUSE_BUTTON_SECONDARY;
        if (buttonState & SDL_BUTTON_MASK(SDL_BUTTON_X1))
            mouseData.buttons |= SCE_MOUSE_BUTTON_OPTIONAL;

        switch (event->type) {
        case SDL_EVENT_MOUSE_MOTION:
            mouseData.xAxis = static_cast<int32_t>(event->motion.xrel);
            mouseData.yAxis = static_cast<int32_t>(event->motion.yrel);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            mouseData.wheel = static_cast<int32_t>(event->wheel.y);
            mouseData.tilt = static_cast<int32_t>(event->wheel.x);
            break;
        default:
            break;
        }

        std::lock_guard<std::mutex> lock(Input::g_mouse_mutex);

        bool merged = false;
        if (event->type == SDL_EVENT_MOUSE_MOTION && !Input::g_mouse_state.empty()) {
            SceMouseData& back = Input::g_mouse_state.back();
            if (back.buttons == mouseData.buttons && back.wheel == 0 && back.tilt == 0) {
                back.xAxis += mouseData.xAxis;
                back.yAxis += mouseData.yAxis;
                back.timestamp = mouseData.timestamp;
                merged = true;
            }
        }

        if (!merged) {
            if (Input::g_mouse_state.size() >= 8) {
                Input::g_mouse_state.pop();
            }
            Input::g_mouse_state.push(mouseData);
        }
    }

    // add/remove it from the list
    if (!should_ignore_custom_mappings)
    {
        bool inputs_changed = Input::UpdatePressedKeys(input_event);

        // update bindings
        if (inputs_changed) {
            Input::ActivateOutputsFromInputs();
        }
    }

    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
        bool is_down = (event->type == SDL_EVENT_KEY_DOWN);
        bool is_repeat = (event->key.repeat != 0);

        // Respect keyboard option Repeat
        //if (!(ime_mgr->GetKeyboardOptions() & OrbisImeKeyboardOption::Repeat) && is_repeat) {
        //    return;
        //}

        OrbisImeEvent ev = {};
        if (is_down) {
            ev.id = is_repeat ? OrbisImeEventId::KeyboardKeycodeRepeat
                              : OrbisImeEventId::KeyboardKeycodeDown;
        } else {
            ev.id = OrbisImeEventId::KeyboardKeycodeUp;
        }

        // Fill the keycode structure
        ev.param.keycode = MakeOrbisKeycode(event, 0); // GetUserId from manager

        Input::g_keyboard_state.PushEvent(ev);
    }
}

void WindowSDL::OnGamepadEvent(const SDL_Event* event) {
    bool input_down = event->type == SDL_EVENT_GAMEPAD_AXIS_MOTION ||
                      event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    Input::InputEvent input_event = Input::InputBinding::GetInputEventFromSDLEvent(*event);

    // the touchpad button shouldn't be rebound to anything else,
    // as it would break the entire touchpad handling
    // You can still bind other things to it though
    if (event->gbutton.button == SDL_GAMEPAD_BUTTON_TOUCHPAD) {
        controllers[controllers.GetGamepadIndexFromJoystickId(event->gbutton.which)]->Button(
            OrbisPadButtonDataOffset::TouchPad, input_down);
        return;
    }

    u8 gamepad;

    switch (event->type) {
    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
        switch ((SDL_SensorType)event->gsensor.sensor) {
        case SDL_SENSOR_GYRO:
            gamepad = controllers.GetGamepadIndexFromJoystickId(event->gsensor.which);
            if (gamepad < 5) {
                controllers[gamepad]->UpdateGyro(event->gsensor.data);
            }
            break;
        case SDL_SENSOR_ACCEL:
            gamepad = controllers.GetGamepadIndexFromJoystickId(event->gsensor.which);
            if (gamepad < 5) {
                controllers[gamepad]->UpdateAcceleration(event->gsensor.data);
            }
            break;
        default:
            break;
        }
        return;
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
        controllers[controllers.GetGamepadIndexFromJoystickId(event->gtouchpad.which)]
            ->SetTouchpadState(event->gtouchpad.finger,
                               event->type != SDL_EVENT_GAMEPAD_TOUCHPAD_UP, event->gtouchpad.x,
                               event->gtouchpad.y);
        return;
    default:
        break;
    }

    // add/remove it from the list
    bool inputs_changed = Input::UpdatePressedKeys(input_event);

    if (inputs_changed) {
        // update bindings
        Input::ActivateOutputsFromInputs();
    }
}

} // namespace Frontend
