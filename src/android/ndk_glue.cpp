#include "android/ndk_glue.h"
#include "core/hooks.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>

static android_app_mock* g_app_ptr = nullptr;

void set_global_app_ptr(android_app_mock* app) {
    g_app_ptr = app;
}

void dispatch_raw_input_event(MockInputEvent* ev) {
    if (g_app_ptr && g_app_ptr->onInputEvent) {
        g_app_ptr->onInputEvent(g_app_ptr, ev);
    }
}

// -----------------------------------------------------------------------------
// Input Queue & Looper Stubs
// -----------------------------------------------------------------------------
static int32_t __cdecl hook_AInputEvent_getType(void* ev)     { return static_cast<MockInputEvent*>(ev)->type; }
static int32_t __cdecl hook_AInputEvent_getSource(void* ev)   { return static_cast<MockInputEvent*>(ev)->source; }
static int32_t __cdecl hook_AInputEvent_getDeviceId(void* ev) { return static_cast<MockInputEvent*>(ev)->deviceId; }
static int32_t __cdecl hook_AKeyEvent_getKeyCode(void* ev)    { return static_cast<MockInputEvent*>(ev)->keyCode; }
static int32_t __cdecl hook_AKeyEvent_getAction(void* ev)     { return static_cast<MockInputEvent*>(ev)->keyAction; }
static int32_t __cdecl hook_AMotionEvent_getAction(void* ev)  { return static_cast<MockInputEvent*>(ev)->motionAction; }
static size_t  __cdecl hook_AMotionEvent_getPointerCount(void* ev) { return 1; }
static int32_t __cdecl hook_AMotionEvent_getPointerId(void* ev, size_t p) { return 0; }
static float   __cdecl hook_AMotionEvent_getX(void* ev, size_t p) {
    auto* e = static_cast<MockInputEvent*>(ev);
    return (e->source & AINPUT_SOURCE_JOYSTICK) ? e->axis_x : e->x;
}
static float   __cdecl hook_AMotionEvent_getY(void* ev, size_t p) {
    auto* e = static_cast<MockInputEvent*>(ev);
    return (e->source & AINPUT_SOURCE_JOYSTICK) ? e->axis_y : e->y;
}

static void* __cdecl hook_ALooper_prepare(int o) { return (void*)0x2000; }
static int   __cdecl hook_ALooper_pollAll(int t, int* fd, int* ev, void** d) { return -1; }
static void  __cdecl hook_ALooper_addFd(void* l, int fd, int id, int ev, void* cb, void* d) {}
static void  __cdecl hook_AInputQueue_attachLooper(void* q, void* l, int id, void* cb, void* d) {}
static void  __cdecl hook_AInputQueue_detachLooper(void* q) {}
static int   __cdecl hook_AInputQueue_getEvent(void* q, void** ev) { return -1; }
static int   __cdecl hook_AInputQueue_preDispatchEvent(void* q, void* ev) { return 0; }
static void  __cdecl hook_AInputQueue_finishEvent(void* q, void* ev, int h) {}

// Native Activity & Window
static void  __cdecl hook_ANativeActivity_setWindowFlags(void* a, uint32_t ad, uint32_t d) {}
static void  __cdecl hook_ANativeActivity_finish(void* a) {}
static int32_t __cdecl hook_ANativeWindow_setBuffersGeometry(void* w, int32_t wi, int32_t h, int32_t f) { return 0; }

// Sensors
static void* __cdecl hook_ASensorManager_getInstance() { return (void*)0x1000; }
static void* __cdecl hook_ASensorManager_getDefaultSensor(void* m, int t) { return (void*)0x1000; }
static void* __cdecl hook_ASensorManager_createEventQueue(void* m, void* l, int id, void* cb, void* d) { return (void*)0x1000; }
static int   __cdecl hook_ASensorEventQueue_enableSensor(void* q, void* s) { return 0; }
static int   __cdecl hook_ASensorEventQueue_disableSensor(void* q, void* s) { return 0; }
static int   __cdecl hook_ASensorEventQueue_setEventRate(void* q, void* s, int r) { return 0; }
static int   __cdecl hook_ASensorEventQueue_getEvents(void* q, void* ev, size_t c) { return 0; }

// Configuration
static void* __cdecl hook_AConfiguration_new() { return malloc(128); }
static void  __cdecl hook_AConfiguration_delete(void* c) { free(c); }
static void  __cdecl hook_AConfiguration_fromAssetManager(void* c, void* a) {}
static void  __cdecl hook_AConfiguration_getLanguage(void* c, char* out) { strcpy(out, "en"); }
static void  __cdecl hook_AConfiguration_getCountry(void* c, char* out) { strcpy(out, "US"); }

// Asset Management
struct LocalAsset { FILE* fp; size_t length; };

static void* __cdecl hook_AAssetManager_open(void* mgr, const char* filename, int mode) {
    char path[512];
    snprintf(path, sizeof(path), "assets/%s", filename);
    FILE* f = fopen(path, "rb");
    if (!f) f = fopen(filename, "rb");
    if (!f) return nullptr;

    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    fseek(f, 0, SEEK_SET);

    auto* a = static_cast<LocalAsset*>(malloc(sizeof(LocalAsset)));
    a->fp = f; 
    a->length = len;
    return a;
}

static int __cdecl hook_AAsset_read(LocalAsset* a, void* buf, size_t c) {
    return (a && a->fp) ? static_cast<int>(fread(buf, 1, c, a->fp)) : -1;
}

static off_t __cdecl hook_AAsset_seek(LocalAsset* a, off_t o, int w) {
    return (a && a->fp) ? (fseek(a->fp, (long)o, w), (off_t)ftell(a->fp)) : -1;
}

static off_t __cdecl hook_AAsset_getLength(LocalAsset* a) {
    return a ? (off_t)a->length : 0;
}

static off_t __cdecl hook_AAsset_getRemainingLength(LocalAsset* a) {
    if (!a || !a->fp) return 0;
    long cur = ftell(a->fp);
    return a->length > (size_t)cur ? (off_t)(a->length - cur) : 0;
}

static void __cdecl hook_AAsset_close(LocalAsset* a) {
    if (a) { 
        if (a->fp) fclose(a->fp); 
        free(a); 
    }
}
#define AMOTION_EVENT_AXIS_X         0
#define AMOTION_EVENT_AXIS_Y         1
#define AMOTION_EVENT_AXIS_HAT_X     15
#define AMOTION_EVENT_AXIS_HAT_Y     16

static float __cdecl hook_AMotionEvent_getAxisValue(const void* ev, int32_t axis, size_t p) {
    if (!ev) return 0.0f;
    auto* e = static_cast<const MockInputEvent*>(ev);
    if (axis == AMOTION_EVENT_AXIS_X || axis == AMOTION_EVENT_AXIS_HAT_X) return e->axis_x;
    if (axis == AMOTION_EVENT_AXIS_Y || axis == AMOTION_EVENT_AXIS_HAT_Y) return e->axis_y;
    return 0.0f;
}


// -----------------------------------------------------------------------------
// Symbol Registrations
// -----------------------------------------------------------------------------
REGISTER_SYMBOL_HOOK("AMotionEvent_getAxisValue", hook_AMotionEvent_getAxisValue);
REGISTER_SYMBOL_HOOK("AInputEvent_getType", hook_AInputEvent_getType);
REGISTER_SYMBOL_HOOK("AInputEvent_getSource", hook_AInputEvent_getSource);
REGISTER_SYMBOL_HOOK("AInputEvent_getDeviceId", hook_AInputEvent_getDeviceId);
REGISTER_SYMBOL_HOOK("AKeyEvent_getKeyCode", hook_AKeyEvent_getKeyCode);
REGISTER_SYMBOL_HOOK("AKeyEvent_getAction", hook_AKeyEvent_getAction);
REGISTER_SYMBOL_HOOK("AMotionEvent_getAction", hook_AMotionEvent_getAction);
REGISTER_SYMBOL_HOOK("AMotionEvent_getPointerCount", hook_AMotionEvent_getPointerCount);
REGISTER_SYMBOL_HOOK("AMotionEvent_getPointerId", hook_AMotionEvent_getPointerId);
REGISTER_SYMBOL_HOOK("AMotionEvent_getX", hook_AMotionEvent_getX);
REGISTER_SYMBOL_HOOK("AMotionEvent_getY", hook_AMotionEvent_getY);
REGISTER_SYMBOL_HOOK("AMotionEvent_getRawX", hook_AMotionEvent_getX);
REGISTER_SYMBOL_HOOK("AMotionEvent_getRawY", hook_AMotionEvent_getY);

REGISTER_SYMBOL_HOOK("ALooper_prepare", hook_ALooper_prepare);
REGISTER_SYMBOL_HOOK("ALooper_pollAll", hook_ALooper_pollAll);
REGISTER_SYMBOL_HOOK("ALooper_addFd", hook_ALooper_addFd);
REGISTER_SYMBOL_HOOK("AInputQueue_attachLooper", hook_AInputQueue_attachLooper);
REGISTER_SYMBOL_HOOK("AInputQueue_detachLooper", hook_AInputQueue_detachLooper);
REGISTER_SYMBOL_HOOK("AInputQueue_getEvent", hook_AInputQueue_getEvent);
REGISTER_SYMBOL_HOOK("AInputQueue_preDispatchEvent", hook_AInputQueue_preDispatchEvent);
REGISTER_SYMBOL_HOOK("AInputQueue_finishEvent", hook_AInputQueue_finishEvent);

REGISTER_SYMBOL_HOOK("ANativeActivity_setWindowFlags", hook_ANativeActivity_setWindowFlags);
REGISTER_SYMBOL_HOOK("ANativeActivity_finish", hook_ANativeActivity_finish);
REGISTER_SYMBOL_HOOK("ANativeWindow_setBuffersGeometry", hook_ANativeWindow_setBuffersGeometry);

REGISTER_SYMBOL_HOOK("ASensorManager_getInstance", hook_ASensorManager_getInstance);
REGISTER_SYMBOL_HOOK("ASensorManager_getDefaultSensor", hook_ASensorManager_getDefaultSensor);
REGISTER_SYMBOL_HOOK("ASensorManager_createEventQueue", hook_ASensorManager_createEventQueue);
REGISTER_SYMBOL_HOOK("ASensorEventQueue_enableSensor", hook_ASensorEventQueue_enableSensor);
REGISTER_SYMBOL_HOOK("ASensorEventQueue_disableSensor", hook_ASensorEventQueue_disableSensor);
REGISTER_SYMBOL_HOOK("ASensorEventQueue_setEventRate", hook_ASensorEventQueue_setEventRate);
REGISTER_SYMBOL_HOOK("ASensorEventQueue_getEvents", hook_ASensorEventQueue_getEvents);

REGISTER_SYMBOL_HOOK("AConfiguration_new", hook_AConfiguration_new);
REGISTER_SYMBOL_HOOK("AConfiguration_delete", hook_AConfiguration_delete);
REGISTER_SYMBOL_HOOK("AConfiguration_fromAssetManager", hook_AConfiguration_fromAssetManager);
REGISTER_SYMBOL_HOOK("AConfiguration_getLanguage", hook_AConfiguration_getLanguage);
REGISTER_SYMBOL_HOOK("AConfiguration_getCountry", hook_AConfiguration_getCountry);

REGISTER_SYMBOL_HOOK("AAssetManager_open", hook_AAssetManager_open);
REGISTER_SYMBOL_HOOK("AAsset_read", hook_AAsset_read);
REGISTER_SYMBOL_HOOK("AAsset_seek", hook_AAsset_seek);
REGISTER_SYMBOL_HOOK("AAsset_getLength", hook_AAsset_getLength);
REGISTER_SYMBOL_HOOK("AAsset_getRemainingLength", hook_AAsset_getRemainingLength);
REGISTER_SYMBOL_HOOK("AAsset_close", hook_AAsset_close);