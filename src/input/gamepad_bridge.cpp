#include "input/gamepad_bridge.h"
#include "android/ndk_glue.h"
#include <cstdio>
#include <cstring>
#include <cmath>

typedef bool (*GamePadMgr_setButton_t)(void* mgr, int deviceId, uint32_t buttonMask, int action);

static uint8_t*               g_pGamePadMgr = nullptr;
static GamePadMgr_setButton_t g_realSetButton = nullptr;
static int                    s_virtual_touch_active = 0;

void GamePadMgrBridge::init(void* pGamePadMgrInstance, void* pSetButtonFunc) {
    g_pGamePadMgr   = reinterpret_cast<uint8_t*>(pGamePadMgrInstance);
    g_realSetButton = reinterpret_cast<GamePadMgr_setButton_t>(pSetButtonFunc);
    printf("[Input] GamePadMgrBridge initialized at %p (setButton: %p)\n", g_pGamePadMgr, g_realSetButton);
}

static void enqueue_event_token(float val) {
    if (!g_pGamePadMgr) return;
    auto* pCount    = reinterpret_cast<int32_t*>(g_pGamePadMgr + 984);
    auto* pWritePos = reinterpret_cast<int32_t*>(g_pGamePadMgr + 976);
    auto* pQueue    = reinterpret_cast<uint32_t*>(g_pGamePadMgr + 576);

    if (*pCount < 100) {
        uint32_t raw;
        memcpy(&raw, &val, sizeof(raw));
        pQueue[*pWritePos] = raw;
        *pWritePos = (*pWritePos + 1) % 100;
        (*pCount)++;
    }
}

static void inject_button(int deviceId, uint32_t buttonMask, int action) {
    if (!g_pGamePadMgr) return;
    if (g_realSetButton) {
        g_realSetButton(g_pGamePadMgr, deviceId, buttonMask, action);
        return;
    }
    int32_t count = *reinterpret_cast<int32_t*>(g_pGamePadMgr + 984);
    if (count + 4 <= 100) {
        enqueue_event_token(3.0f);
        enqueue_event_token(static_cast<float>(deviceId));
        enqueue_event_token(static_cast<float>(buttonMask));
        enqueue_event_token(static_cast<float>(action));
    }
}

void GamePadMgrBridge::dispatchStartPress(int is_down) {
    printf("[Input] Start / Pause button %s\n", is_down ? "PRESSED" : "RELEASED");
    int action = is_down ? 0 : 1;

    if (g_pGamePadMgr) {
        for (int i = 0; i < 16; i++) {
            uint8_t* pad = g_pGamePadMgr + (i * 36);
            auto* pActive = reinterpret_cast<uint8_t*>(pad + 0x04);
            auto* pDevId  = reinterpret_cast<int32_t*>(pad + 0x00);
            auto* pSupported = reinterpret_cast<uint32_t*>(pad + 0x20);

            if (i == 0 && !(*pActive)) {
                *pActive = 1;
                *pDevId = 1;
            }
            if (*pActive) {
                *pSupported |= (1 << 8); // Start button bit
                if (is_down) {
                    *reinterpret_cast<uint8_t*>(pad + 0x0E) = 1; // m_pauseTrigger
                    *reinterpret_cast<uint8_t*>(pad + 0x0D) = 1; // m_pauseActive
                }
            }
        }
        inject_button(1, 0x8000, action);
        inject_button(1, 0x0100, action);
    }

    MockInputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type      = AINPUT_EVENT_TYPE_KEY;
    ev.source    = AINPUT_SOURCE_GAMEPAD;
    ev.deviceId  = 1;
    ev.keyCode   = AKEYCODE_BUTTON_START;
    ev.keyAction = is_down ? AKEY_EVENT_ACTION_DOWN : AKEY_EVENT_ACTION_UP;
    dispatch_raw_input_event(&ev);

    ev.source  = AINPUT_SOURCE_KEYBOARD;
    ev.keyCode = AKEYCODE_ESCAPE;
    dispatch_raw_input_event(&ev);
}

void GamePadMgrBridge::dispatchButton(int android_code, int is_down) {
    if (android_code == 0) return;
    MockInputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type      = AINPUT_EVENT_TYPE_KEY;
    ev.source    = AINPUT_SOURCE_GAMEPAD | AINPUT_SOURCE_KEYBOARD;
    ev.deviceId  = 1;
    ev.keyCode   = android_code;
    ev.keyAction = is_down ? AKEY_EVENT_ACTION_DOWN : AKEY_EVENT_ACTION_UP;
    dispatch_raw_input_event(&ev);
}

void GamePadMgrBridge::dispatchDpad(int android_code, int is_down) {
    if (android_code == 0) return;
    MockInputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type      = AINPUT_EVENT_TYPE_KEY;
    ev.source    = AINPUT_SOURCE_KEYBOARD | AINPUT_SOURCE_DPAD;
    ev.deviceId  = 1;
    ev.keyCode   = android_code;
    ev.keyAction = is_down ? AKEY_EVENT_ACTION_DOWN : AKEY_EVENT_ACTION_UP;
    dispatch_raw_input_event(&ev);
}

void GamePadMgrBridge::dispatchMotionFrame(float sx, float sy) {
    MockInputEvent joy_ev;
    memset(&joy_ev, 0, sizeof(joy_ev));
    joy_ev.type         = AINPUT_EVENT_TYPE_MOTION;
    joy_ev.source       = AINPUT_SOURCE_JOYSTICK;
    joy_ev.deviceId     = 1;
    joy_ev.motionAction = AMOTION_EVENT_ACTION_MOVE;
    joy_ev.axis_x       = sx;
    joy_ev.axis_y       = sy;
    joy_ev.x            = sx;
    joy_ev.y            = sy;
    dispatch_raw_input_event(&joy_ev);

    const float STICK_CENTER_X = 190.0f;
    const float STICK_CENTER_Y = 530.0f;
    const float STICK_RADIUS   = 90.0f;

    MockInputEvent touch_ev;
    memset(&touch_ev, 0, sizeof(touch_ev));
    touch_ev.type     = AINPUT_EVENT_TYPE_MOTION;
    touch_ev.source   = AINPUT_SOURCE_TOUCHSCREEN;
    touch_ev.deviceId = 0;

    if (fabsf(sx) > 0.1f || fabsf(sy) > 0.1f) {
        touch_ev.x = STICK_CENTER_X + (sx * STICK_RADIUS);
        touch_ev.y = STICK_CENTER_Y + (sy * STICK_RADIUS);
        if (!s_virtual_touch_active) {
            touch_ev.motionAction = AMOTION_EVENT_ACTION_DOWN;
            s_virtual_touch_active = 1;
        } else {
            touch_ev.motionAction = AMOTION_EVENT_ACTION_MOVE;
        }
        dispatch_raw_input_event(&touch_ev);
    } else if (s_virtual_touch_active) {
        touch_ev.x = STICK_CENTER_X;
        touch_ev.y = STICK_CENTER_Y;
        touch_ev.motionAction = AMOTION_EVENT_ACTION_UP;
        s_virtual_touch_active = 0;
        dispatch_raw_input_event(&touch_ev);
    }
}