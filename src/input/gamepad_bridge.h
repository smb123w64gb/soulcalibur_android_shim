#pragma once
#include <cstdint>

struct GamePadMgrBridge {
    static void init(void* pGamePadMgrInstance, void* pSetButtonFunc);
    static void dispatchStartPress(int is_down);
    static void dispatchButton(int android_code, int is_down);
    static void dispatchDpad(int android_code, int is_down);
    static void dispatchMotionFrame(float sx, float sy);
    static void updateFrame();
};