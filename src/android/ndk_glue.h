#pragma once
#include <cstdint>
#include <cstddef>

#define AINPUT_EVENT_TYPE_KEY        1
#define AINPUT_EVENT_TYPE_MOTION     2

#define AINPUT_SOURCE_KEYBOARD       0x00000101
#define AINPUT_SOURCE_DPAD           0x00000201
#define AINPUT_SOURCE_GAMEPAD        0x00000401
#define AINPUT_SOURCE_TOUCHSCREEN    0x00001002
#define AINPUT_SOURCE_JOYSTICK       0x01000010

#define AKEY_EVENT_ACTION_DOWN       0
#define AKEY_EVENT_ACTION_UP         1
#define AMOTION_EVENT_ACTION_DOWN    0
#define AMOTION_EVENT_ACTION_UP      1
#define AMOTION_EVENT_ACTION_MOVE    2

#define AKEYCODE_BACK                4
#define AKEYCODE_DPAD_UP             19
#define AKEYCODE_DPAD_DOWN           20
#define AKEYCODE_DPAD_LEFT           21
#define AKEYCODE_DPAD_RIGHT          22
#define AKEYCODE_ENTER               66
#define AKEYCODE_MENU                82
#define AKEYCODE_BUTTON_A            96
#define AKEYCODE_BUTTON_B            97
#define AKEYCODE_BUTTON_X            99
#define AKEYCODE_BUTTON_Y            100
#define AKEYCODE_BUTTON_L1           102
#define AKEYCODE_BUTTON_R1           103
#define AKEYCODE_BUTTON_START        108
#define AKEYCODE_ESCAPE              111

enum {
    APP_CMD_INIT_WINDOW = 1,
    APP_CMD_GAINED_FOCUS = 6,
};

struct FakeNativeActivity {
    void *callbacks, *vm, *env, *clazz;
    const char *internalPath, *externalPath;
    int32_t sdkVersion;
    void *instance, *assetManager;
    const char *obbPath;
};

struct android_app_mock {
    void* userData;
    void (*onAppCmd)(struct android_app_mock* app, int32_t cmd);
    int32_t (*onInputEvent)(struct android_app_mock* app, void* event);
    struct FakeNativeActivity* activity;
    void *config, *savedState;
    size_t savedStateSize;
    void *looper, *inputQueue, *window;
    uint8_t reserved[256];
};

struct MockInputEvent {
    int32_t type, source, deviceId, keyAction, keyCode, motionAction;
    float x, y, axis_x, axis_y;
};

void set_global_app_ptr(android_app_mock* app);
void dispatch_raw_input_event(MockInputEvent* ev);