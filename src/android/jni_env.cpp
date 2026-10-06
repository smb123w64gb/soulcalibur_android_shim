#include "android/jni_env.h"
#include "core/hooks.h"
#include <cstdio>
#include <cstring>
#include <cstdint>

static void* fake_jnienv_table[300];
static void* fake_jvm_table[32];
static void* fake_jni_env_ptr = fake_jnienv_table;
static void* fake_jvm_ptr     = fake_jvm_table;

static const char s_game_version[] = "1.0.15\0\0\0\0";
static void* fake_byte_array_handle = (void*)0x6001;
static void* fake_string_handle     = (void*)0x6002;

#define MID_GET_VERSION_NAME        ((void*)0x5001)
#define MID_GET_PAD_NUM             ((void*)0x5002)
#define MID_HAS_START_BUTTON        ((void*)0x5003)
#define MID_PLAY_GAME_IS_SIGNED_IN  ((void*)0x5004)
#define MID_IS_JOYSTICK             ((void*)0x5005)
#define MID_GET_PUB_DATA            ((void*)0x5006)
#define MID_GET_BUTTON_LIST         ((void*)0x5007)
#define MID_GENERIC                 ((void*)0x5099)

void* get_fake_jni_env_ptr() { return &fake_jni_env_ptr; }
void* get_fake_jvm_ptr()     { return &fake_jvm_ptr; }

static void* __cdecl jni_generic_stub(void* env, ...) { 
    return (void*)0x1234; 
}

static int32_t __cdecl jvm_GetEnv(void* vm, void** penv, int32_t v) { 
    if (penv) *penv = &fake_jni_env_ptr; 
    return 0; 
}
static int32_t __cdecl jvm_AttachCurrentThread(void* vm, void** penv, void* a) { 
    if (penv) *penv = &fake_jni_env_ptr; 
    return 0; 
}
static int32_t __cdecl jvm_DetachCurrentThread(void* vm) { return 0; }

static void* __cdecl jni_FindClass(void* env, const char* name) { 
    printf("[JNI] FindClass: %s\n", name ? name : "null");
    return (void*)0x4000; 
}

static void* __cdecl jni_GetObjectClass(void* env, void* obj) {
    printf("[JNI] GetObjectClass for %p\n", obj);
    return (void*)0x4000;
}

static const char* __cdecl jni_GetStringUTFChars(void* env, void* str, uint8_t* isCopy) {
    if (isCopy) *isCopy = 0;
    return s_game_version;
}

static int32_t __cdecl jni_GetStringUTFLength(void* env, void* str) {
    return (int32_t)strlen(s_game_version);
}

static void __cdecl jni_ReleaseStringUTFChars(void* env, void* str, const char* chars) {}

static void* __cdecl jni_GetMethodID(void* env, void* clazz, const char* name, const char* sig) {
    printf("[JNI] GetMethodID: %s, sig: %s\n", name ? name : "null", sig ? sig : "null");
    if (name) {
        if (strcmp(name, "getVersionName") == 0)       return MID_GET_VERSION_NAME;
        if (strcmp(name, "getPadNum") == 0)            return MID_GET_PAD_NUM;
        if (strcmp(name, "hasStartButton") == 0)       return MID_HAS_START_BUTTON;
        if (strcmp(name, "playGameIsSignedIn") == 0)   return MID_PLAY_GAME_IS_SIGNED_IN;
        if (strcmp(name, "isJoyStick") == 0)           return MID_IS_JOYSTICK;
        if (strcmp(name, "getPubData") == 0)           return MID_GET_PUB_DATA;
        if (strcmp(name, "getButtonList") == 0)        return MID_GET_BUTTON_LIST;
    }
    return MID_GENERIC;
}

static void* __cdecl jni_GetStaticMethodID(void* env, void* clazz, const char* name, const char* sig) { 
    printf("[JNI] GetStaticMethodID: %s, sig: %s\n", name ? name : "null", sig ? sig : "null");
    return MID_GENERIC; 
}

static void* __cdecl jni_CallObjectMethod(void* env, void* obj, void* methodID, ...) {
    printf("[JNI] CallObjectMethod: methodID=%p\n", methodID);
    if (methodID == MID_GET_VERSION_NAME) {
        // Return byte array handle (or string handle)
        return fake_byte_array_handle;
    }
    if (methodID == MID_GET_PUB_DATA) {
        return nullptr;
    }
    return (void*)0x6000;
}

static void* __cdecl jni_CallStaticObjectMethod(void* env, void* clazz, void* methodID, ...) { 
    printf("[JNI] CallStaticObjectMethod: methodID=%p\n", methodID);
    return (void*)0x6000; 
}

static int32_t __cdecl jni_CallBooleanMethod(void* env, void* obj, void* methodID, ...) {
    printf("[JNI] CallBooleanMethod: methodID=%p\n", methodID);
    if (methodID == MID_HAS_START_BUTTON || methodID == MID_IS_JOYSTICK) return 1;
    if (methodID == MID_PLAY_GAME_IS_SIGNED_IN) return 0;
    return 1;
}

static int32_t __cdecl jni_CallIntMethod(void* env, void* obj, void* methodID, ...) {
    printf("[JNI] CallIntMethod: methodID=%p\n", methodID);
    if (methodID == MID_GET_PAD_NUM) return 1;
    if (methodID == MID_GET_BUTTON_LIST) return 0xFFFFFFFF;
    return 0;
}

static void __cdecl jni_CallVoidMethod(void* env, void* obj, void* methodID, ...) {}
static void __cdecl jni_CallStaticVoidMethod(void* env, void* clazz, void* methodID, ...) {}

static int32_t __cdecl jni_GetArrayLength(void* env, void* array) { 
    printf("[JNI] GetArrayLength for %p\n", array);
    return (int32_t)strlen(s_game_version); 
}

static void* __cdecl jni_GetByteArrayElements(void* env, void* array, uint8_t* isCopy) { 
    printf("[JNI] GetByteArrayElements for %p\n", array);
    if (isCopy) *isCopy = 0; 
    return (void*)s_game_version; 
}

static void __cdecl jni_ReleaseByteArrayElements(void* env, void* array, void* elems, int32_t mode) {}

static void __cdecl jni_GetByteArrayRegion(void* env, void* array, int32_t start, int32_t len, int8_t* buf) {
    printf("[JNI] GetByteArrayRegion: start=%d, len=%d\n", start, len);
    if (buf && len > 0) {
        memcpy(buf, s_game_version + start, len);
    }
}

static int32_t __cdecl jni_GetJavaVM(void* env, void** vm) { if (vm) *vm = &fake_jvm_ptr; return 0; }

// Direct C symbols that libsoul.so may link directly:
static uint32_t __cdecl hook_JniService_getButtonList(void* jni, int deviceId) { return 0xFFFFFFFF; }
static int __cdecl hook_JniService_isJoyStick(void* jni, int deviceId) { return 1; }
static int __cdecl hook_JniService_getPadNum(void* jni) { return 1; }

void init_fake_jni() {
    for (int i = 0; i < 300; i++) fake_jnienv_table[i] = (void*)jni_generic_stub;
    fake_jnienv_table[6]   = (void*)jni_FindClass;
    fake_jnienv_table[31]  = (void*)jni_GetObjectClass;
    fake_jnienv_table[33]  = (void*)jni_GetMethodID;
    fake_jnienv_table[34]  = (void*)jni_CallObjectMethod;
    fake_jnienv_table[37]  = (void*)jni_CallBooleanMethod;
    fake_jnienv_table[49]  = (void*)jni_CallIntMethod;
    fake_jnienv_table[61]  = (void*)jni_CallVoidMethod;
    fake_jnienv_table[113] = (void*)jni_GetStaticMethodID;
    fake_jnienv_table[114] = (void*)jni_CallStaticObjectMethod;
    fake_jnienv_table[141] = (void*)jni_CallStaticVoidMethod;
    fake_jnienv_table[168] = (void*)jni_GetStringUTFLength;
    fake_jnienv_table[169] = (void*)jni_GetStringUTFChars;
    fake_jnienv_table[170] = (void*)jni_ReleaseStringUTFChars;
    fake_jnienv_table[171] = (void*)jni_GetArrayLength;
    fake_jnienv_table[184] = (void*)jni_GetByteArrayElements;
    fake_jnienv_table[192] = (void*)jni_ReleaseByteArrayElements;
    fake_jnienv_table[200] = (void*)jni_GetByteArrayRegion;
    fake_jnienv_table[219] = (void*)jni_GetJavaVM;

    for (int i = 0; i < 32; i++) fake_jvm_table[i] = (void*)jni_generic_stub;
    fake_jvm_table[4] = (void*)jvm_AttachCurrentThread;
    fake_jvm_table[5] = (void*)jvm_DetachCurrentThread;
    fake_jvm_table[6] = (void*)jvm_GetEnv;

    REGISTER_SYMBOL_HOOK("_Z24JniService_getButtonListP10JniServicei", hook_JniService_getButtonList);
    REGISTER_SYMBOL_HOOK("JniService_getButtonList", hook_JniService_getButtonList);
    REGISTER_SYMBOL_HOOK("_Z20JniService_isJoyStickP10JniServicei", hook_JniService_isJoyStick);
    REGISTER_SYMBOL_HOOK("JniService_isJoyStick", hook_JniService_isJoyStick);
    REGISTER_SYMBOL_HOOK("_Z20JniService_getPadNumP10JniService", hook_JniService_getPadNum);
    REGISTER_SYMBOL_HOOK("JniService_getPadNum", hook_JniService_getPadNum);
}