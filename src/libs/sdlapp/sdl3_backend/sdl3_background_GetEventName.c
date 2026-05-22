/**
 * @file   sdl3_background_GetEventName.c
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Převod SDL3 událostí na textové názvy pro ladící výpisy.
 *
 * Obsahuje jedinou funkci sdl3_backend_GetEventName, která pro daný
 * typ SDL události vrátí jeho textovou reprezentaci (název konstanty).
 *
 * @par Changelog:
 * - 2026-03-14: Proběhla kompletní revize a refaktorizace. Vytvořeny unit testy.
 *
 * @par Licence:
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <SDL3/SDL.h>

/**
 * @brief Vrátí textový název SDL události pro ladící účely.
 *
 * Pro známý typ SDL události vrátí odpovídající řetězec (např. "SDL_EVENT_QUIT").
 * Pro neznámé události vrátí "UNKNOWN_EVENT".
 *
 * @param event_type Typ SDL události (Uint32).
 * @return Konstantní řetězec s názvem události.
 */
const char *sdl3_backend_GetEventName(Uint32 event_type) {
    switch (event_type) {
        /* Application events */
        case SDL_EVENT_QUIT: return "SDL_EVENT_QUIT";
        case SDL_EVENT_TERMINATING: return "SDL_EVENT_TERMINATING";
        case SDL_EVENT_LOW_MEMORY: return "SDL_EVENT_LOW_MEMORY";
        case SDL_EVENT_WILL_ENTER_BACKGROUND: return "SDL_EVENT_WILL_ENTER_BACKGROUND";
        case SDL_EVENT_DID_ENTER_BACKGROUND: return "SDL_EVENT_DID_ENTER_BACKGROUND";
        case SDL_EVENT_WILL_ENTER_FOREGROUND: return "SDL_EVENT_WILL_ENTER_FOREGROUND";
        case SDL_EVENT_DID_ENTER_FOREGROUND: return "SDL_EVENT_DID_ENTER_FOREGROUND";
        case SDL_EVENT_LOCALE_CHANGED: return "SDL_EVENT_LOCALE_CHANGED";
        case SDL_EVENT_SYSTEM_THEME_CHANGED: return "SDL_EVENT_SYSTEM_THEME_CHANGED";

        /* Display events */
        case SDL_EVENT_DISPLAY_ORIENTATION: return "SDL_EVENT_DISPLAY_ORIENTATION";
        case SDL_EVENT_DISPLAY_ADDED: return "SDL_EVENT_DISPLAY_ADDED";
        case SDL_EVENT_DISPLAY_REMOVED: return "SDL_EVENT_DISPLAY_REMOVED";
        case SDL_EVENT_DISPLAY_MOVED: return "SDL_EVENT_DISPLAY_MOVED";
        case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED: return "SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED";
        case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED: return "SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED";
        case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED: return "SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED";

        /* Window events */
        case SDL_EVENT_WINDOW_SHOWN: return "SDL_EVENT_WINDOW_SHOWN";
        case SDL_EVENT_WINDOW_HIDDEN: return "SDL_EVENT_WINDOW_HIDDEN";
        case SDL_EVENT_WINDOW_EXPOSED: return "SDL_EVENT_WINDOW_EXPOSED";
        case SDL_EVENT_WINDOW_MOVED: return "SDL_EVENT_WINDOW_MOVED";
        case SDL_EVENT_WINDOW_RESIZED: return "SDL_EVENT_WINDOW_RESIZED";
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: return "SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED";
        case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED: return "SDL_EVENT_WINDOW_METAL_VIEW_RESIZED";
        case SDL_EVENT_WINDOW_MINIMIZED: return "SDL_EVENT_WINDOW_MINIMIZED";
        case SDL_EVENT_WINDOW_MAXIMIZED: return "SDL_EVENT_WINDOW_MAXIMIZED";
        case SDL_EVENT_WINDOW_RESTORED: return "SDL_EVENT_WINDOW_RESTORED";
        case SDL_EVENT_WINDOW_MOUSE_ENTER: return "SDL_EVENT_WINDOW_MOUSE_ENTER";
        case SDL_EVENT_WINDOW_MOUSE_LEAVE: return "SDL_EVENT_WINDOW_MOUSE_LEAVE";
        case SDL_EVENT_WINDOW_FOCUS_GAINED: return "SDL_EVENT_WINDOW_FOCUS_GAINED";
        case SDL_EVENT_WINDOW_FOCUS_LOST: return "SDL_EVENT_WINDOW_FOCUS_LOST";
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: return "SDL_EVENT_WINDOW_CLOSE_REQUESTED";
        case SDL_EVENT_WINDOW_HIT_TEST: return "SDL_EVENT_WINDOW_HIT_TEST";
        case SDL_EVENT_WINDOW_ICCPROF_CHANGED: return "SDL_EVENT_WINDOW_ICCPROF_CHANGED";
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED: return "SDL_EVENT_WINDOW_DISPLAY_CHANGED";
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: return "SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED";
        case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED: return "SDL_EVENT_WINDOW_SAFE_AREA_CHANGED";
        case SDL_EVENT_WINDOW_OCCLUDED: return "SDL_EVENT_WINDOW_OCCLUDED";
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN: return "SDL_EVENT_WINDOW_ENTER_FULLSCREEN";
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: return "SDL_EVENT_WINDOW_LEAVE_FULLSCREEN";
        case SDL_EVENT_WINDOW_DESTROYED: return "SDL_EVENT_WINDOW_DESTROYED";
        case SDL_EVENT_WINDOW_HDR_STATE_CHANGED: return "SDL_EVENT_WINDOW_HDR_STATE_CHANGED";

        /* Keyboard events */
        case SDL_EVENT_KEY_DOWN: return "SDL_EVENT_KEY_DOWN";
        case SDL_EVENT_KEY_UP: return "SDL_EVENT_KEY_UP";
        case SDL_EVENT_TEXT_EDITING: return "SDL_EVENT_TEXT_EDITING";
        case SDL_EVENT_TEXT_INPUT: return "SDL_EVENT_TEXT_INPUT";
        case SDL_EVENT_KEYMAP_CHANGED: return "SDL_EVENT_KEYMAP_CHANGED";
        case SDL_EVENT_KEYBOARD_ADDED: return "SDL_EVENT_KEYBOARD_ADDED";
        case SDL_EVENT_KEYBOARD_REMOVED: return "SDL_EVENT_KEYBOARD_REMOVED";
        case SDL_EVENT_TEXT_EDITING_CANDIDATES: return "SDL_EVENT_TEXT_EDITING_CANDIDATES";

        /* Mouse events */
        case SDL_EVENT_MOUSE_MOTION: return "SDL_EVENT_MOUSE_MOTION";
        case SDL_EVENT_MOUSE_BUTTON_DOWN: return "SDL_EVENT_MOUSE_BUTTON_DOWN";
        case SDL_EVENT_MOUSE_BUTTON_UP: return "SDL_EVENT_MOUSE_BUTTON_UP";
        case SDL_EVENT_MOUSE_WHEEL: return "SDL_EVENT_MOUSE_WHEEL";
        case SDL_EVENT_MOUSE_ADDED: return "SDL_EVENT_MOUSE_ADDED";
        case SDL_EVENT_MOUSE_REMOVED: return "SDL_EVENT_MOUSE_REMOVED";

        /* Joystick events */
        case SDL_EVENT_JOYSTICK_AXIS_MOTION: return "SDL_EVENT_JOYSTICK_AXIS_MOTION";
        case SDL_EVENT_JOYSTICK_BALL_MOTION: return "SDL_EVENT_JOYSTICK_BALL_MOTION";
        case SDL_EVENT_JOYSTICK_HAT_MOTION: return "SDL_EVENT_JOYSTICK_HAT_MOTION";
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN: return "SDL_EVENT_JOYSTICK_BUTTON_DOWN";
        case SDL_EVENT_JOYSTICK_BUTTON_UP: return "SDL_EVENT_JOYSTICK_BUTTON_UP";
        case SDL_EVENT_JOYSTICK_ADDED: return "SDL_EVENT_JOYSTICK_ADDED";
        case SDL_EVENT_JOYSTICK_REMOVED: return "SDL_EVENT_JOYSTICK_REMOVED";
        case SDL_EVENT_JOYSTICK_BATTERY_UPDATED: return "SDL_EVENT_JOYSTICK_BATTERY_UPDATED";
        case SDL_EVENT_JOYSTICK_UPDATE_COMPLETE: return "SDL_EVENT_JOYSTICK_UPDATE_COMPLETE";

        /* Gamepad events */
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: return "SDL_EVENT_GAMEPAD_AXIS_MOTION";
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN: return "SDL_EVENT_GAMEPAD_BUTTON_DOWN";
        case SDL_EVENT_GAMEPAD_BUTTON_UP: return "SDL_EVENT_GAMEPAD_BUTTON_UP";
        case SDL_EVENT_GAMEPAD_ADDED: return "SDL_EVENT_GAMEPAD_ADDED";
        case SDL_EVENT_GAMEPAD_REMOVED: return "SDL_EVENT_GAMEPAD_REMOVED";
        case SDL_EVENT_GAMEPAD_REMAPPED: return "SDL_EVENT_GAMEPAD_REMAPPED";
        case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN: return "SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN";
        case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION: return "SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION";
        case SDL_EVENT_GAMEPAD_TOUCHPAD_UP: return "SDL_EVENT_GAMEPAD_TOUCHPAD_UP";
        case SDL_EVENT_GAMEPAD_SENSOR_UPDATE: return "SDL_EVENT_GAMEPAD_SENSOR_UPDATE";
        case SDL_EVENT_GAMEPAD_UPDATE_COMPLETE: return "SDL_EVENT_GAMEPAD_UPDATE_COMPLETE";
        case SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED: return "SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED";

        /* Touch events */
        case SDL_EVENT_FINGER_DOWN: return "SDL_EVENT_FINGER_DOWN";
        case SDL_EVENT_FINGER_UP: return "SDL_EVENT_FINGER_UP";
        case SDL_EVENT_FINGER_MOTION: return "SDL_EVENT_FINGER_MOTION";
        case SDL_EVENT_FINGER_CANCELED: return "SDL_EVENT_FINGER_CANCELED";

        /* Clipboard events */
        case SDL_EVENT_CLIPBOARD_UPDATE: return "SDL_EVENT_CLIPBOARD_UPDATE";

        /* Drag and drop events */
        case SDL_EVENT_DROP_FILE: return "SDL_EVENT_DROP_FILE";
        case SDL_EVENT_DROP_TEXT: return "SDL_EVENT_DROP_TEXT";
        case SDL_EVENT_DROP_BEGIN: return "SDL_EVENT_DROP_BEGIN";
        case SDL_EVENT_DROP_COMPLETE: return "SDL_EVENT_DROP_COMPLETE";
        case SDL_EVENT_DROP_POSITION: return "SDL_EVENT_DROP_POSITION";

        /* Audio hotplug events */
        case SDL_EVENT_AUDIO_DEVICE_ADDED: return "SDL_EVENT_AUDIO_DEVICE_ADDED";
        case SDL_EVENT_AUDIO_DEVICE_REMOVED: return "SDL_EVENT_AUDIO_DEVICE_REMOVED";
        case SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED: return "SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED";

        /* Sensor events */
        case SDL_EVENT_SENSOR_UPDATE: return "SDL_EVENT_SENSOR_UPDATE";

        /* Pressure-sensitive pen events */
        case SDL_EVENT_PEN_PROXIMITY_IN: return "SDL_EVENT_PEN_PROXIMITY_IN";
        case SDL_EVENT_PEN_PROXIMITY_OUT: return "SDL_EVENT_PEN_PROXIMITY_OUT";
        case SDL_EVENT_PEN_DOWN: return "SDL_EVENT_PEN_DOWN";
        case SDL_EVENT_PEN_UP: return "SDL_EVENT_PEN_UP";
        case SDL_EVENT_PEN_BUTTON_DOWN: return "SDL_EVENT_PEN_BUTTON_DOWN";
        case SDL_EVENT_PEN_BUTTON_UP: return "SDL_EVENT_PEN_BUTTON_UP";
        case SDL_EVENT_PEN_MOTION: return "SDL_EVENT_PEN_MOTION";
        case SDL_EVENT_PEN_AXIS: return "SDL_EVENT_PEN_AXIS";

        /* Camera hotplug events */
        case SDL_EVENT_CAMERA_DEVICE_ADDED: return "SDL_EVENT_CAMERA_DEVICE_ADDED";
        case SDL_EVENT_CAMERA_DEVICE_REMOVED: return "SDL_EVENT_CAMERA_DEVICE_REMOVED";
        case SDL_EVENT_CAMERA_DEVICE_APPROVED: return "SDL_EVENT_CAMERA_DEVICE_APPROVED";
        case SDL_EVENT_CAMERA_DEVICE_DENIED: return "SDL_EVENT_CAMERA_DEVICE_DENIED";

        /* Render events */
        case SDL_EVENT_RENDER_TARGETS_RESET: return "SDL_EVENT_RENDER_TARGETS_RESET";
        case SDL_EVENT_RENDER_DEVICE_RESET: return "SDL_EVENT_RENDER_DEVICE_RESET";
        case SDL_EVENT_RENDER_DEVICE_LOST: return "SDL_EVENT_RENDER_DEVICE_LOST";

        /* Other events */
        case SDL_EVENT_POLL_SENTINEL: return "SDL_EVENT_POLL_SENTINEL";

        default: return "UNKNOWN_EVENT";
    }
}
