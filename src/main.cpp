#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <windows.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include "core/elf_loader.h"
#include "core/hooks.h"
#include "core/libc_compat.h"
#include "graphics/viewport.h"
#include "graphics/egl_shim.h"
#include "graphics/gl_wrappers.h"
#include "audio/opensles_mock.h"
#include "android/jni_env.h"
#include "android/ndk_glue.h"
#include "input/gamepad_bridge.h"

// --- Global Host State ---
static SDL_Window*         g_window     = nullptr;
static SDL_GLContext       g_gl_context = nullptr;
static SDL_GameController* g_controller = nullptr;
static volatile int        g_running    = 1;

ViewportState g_viewport;

static void toggle_fullscreen() {
    if (!g_window) return;
    Uint32 flags = SDL_GetWindowFlags(g_window);
    bool is_fs = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;

    SDL_SetWindowFullscreen(g_window, is_fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);

    int nw = 0, nh = 0;
    SDL_GetWindowSize(g_window, &nw, &nh);
    g_viewport.update(nw, nh);
}

static void open_first_controller() {
    if (g_controller) {
        SDL_GameControllerClose(g_controller);
        g_controller = nullptr;
    }
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            g_controller = SDL_GameControllerOpen(i);
            if (g_controller) {
                printf("[Input] Active Gamepad: %s\n", SDL_GameControllerName(g_controller));
                break;
            }
        }
    }
}

static DWORD WINAPI game_thread_entry(LPVOID arg) {
    auto* app = reinterpret_cast<android_app_mock*>(arg);
    typedef void (*android_main_t)(android_app_mock*);
    auto run_game = reinterpret_cast<android_main_t>(app->userData);

    if (g_window && g_gl_context) {
        SDL_GL_MakeCurrent(g_window, g_gl_context);
    }
    printf("[Thread] Starting android_main in background thread...\n");
    run_game(app);
    return 0;
}

static LONG WINAPI WindowsCrashHandler(EXCEPTION_POINTERS *ep) {
    uint32_t addr = (uint32_t)ep->ExceptionRecord->ExceptionAddress;
    HMODULE hFaultMod = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)addr, &hFaultMod);
    char faultMod[MAX_PATH] = "Unknown";
    if (hFaultMod) {
        GetModuleFileNameA(hFaultMod, faultMod, sizeof(faultMod));
        char *p = strrchr(faultMod, '\\');
        if (p) memmove(faultMod, p + 1, strlen(p + 1) + 1);
    }

    printf("\n======================================================\n");
    printf("[WINDOWS CRASH] Exception 0x%08X\n", (unsigned int)ep->ExceptionRecord->ExceptionCode);
    printf("Fault Address:    0x%08X [%s + 0x%X]\n", addr, faultMod, hFaultMod ? (addr - (uint32_t)hFaultMod) : 0);
    printf("Registers:\n");
    printf("  EAX: 0x%08X  EBX: 0x%08X  ECX: 0x%08X  EDX: 0x%08X\n",
           (unsigned int)ep->ContextRecord->Eax, (unsigned int)ep->ContextRecord->Ebx,
           (unsigned int)ep->ContextRecord->Ecx, (unsigned int)ep->ContextRecord->Edx);
    printf("  ESI: 0x%08X  EDI: 0x%08X  ESP: 0x%08X  EBP: 0x%08X\n",
           (unsigned int)ep->ContextRecord->Esi, (unsigned int)ep->ContextRecord->Edi,
           (unsigned int)ep->ContextRecord->Esp, (unsigned int)ep->ContextRecord->Ebp);
    printf("======================================================\n");
    fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;
}

int main(int argc, char* argv[]) {
    SetUnhandledExceptionFilter(WindowsCrashHandler);

    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);

    printf("Starting Native Windows Soulcalibur Port (Modular MSVC/MinGW)...\n");

    CreateDirectoryA("save", nullptr);
    CreateDirectoryA("assets", nullptr);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        printf("SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    g_window = SDL_CreateWindow(
        "Soulcalibur (Native Windows Port)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    g_gl_context = SDL_GL_CreateContext(g_window);
    g_viewport.update(1280, 720);

    // Initialize all modular subsystems
    init_libc_compat();
    init_gl_wrappers();
    init_mock_opensles();
    init_fake_jni();

    LoadedSO so;
    if (!load_elf_so("libsoul.so", &so)) {
        printf("Failed to load libsoul.so\n");
        return 1;
    }

    // Android App Glue Setup
    static FakeNativeActivity fake_activity;
    fake_activity.vm           = get_fake_jvm_ptr();
    fake_activity.env          = get_fake_jni_env_ptr();
    fake_activity.clazz        = reinterpret_cast<void*>(0x4000);
    fake_activity.assetManager = reinterpret_cast<void*>(0x5000);
    fake_activity.internalPath = "./save/";
    fake_activity.externalPath = "./save/";
    fake_activity.obbPath      = "./assets/";
    fake_activity.sdkVersion   = 19;

    static android_app_mock fake_app;
    memset(&fake_app, 0, sizeof(fake_app));
    fake_app.activity = &fake_activity;
    fake_app.config   = reinterpret_cast<void*>(0x7000);
    fake_app.looper   = reinterpret_cast<void*>(0x2000);
    fake_app.window   = reinterpret_cast<void*>(g_window);
    fake_app.userData = so.entry_android_main;
    set_global_app_ptr(&fake_app);

    // Release context from Main Thread so Game Thread can claim it
    SDL_GL_MakeCurrent(g_window, nullptr);

    HANDLE hGameThread = CreateThread(nullptr, 0, game_thread_entry, &fake_app, 0, nullptr);

    while (fake_app.onAppCmd == nullptr) Sleep(10);

    fake_app.onAppCmd(&fake_app, APP_CMD_INIT_WINDOW);
    fake_app.onAppCmd(&fake_app, APP_CMD_GAINED_FOCUS);

    open_first_controller();

    float stick_x = 0.0f, stick_y = 0.0f;
    int key_left = 0, key_right = 0, key_up = 0, key_down = 0;
    int dpad_left = 0, dpad_right = 0, dpad_up = 0, dpad_down = 0;

    // --- Main Event Loop ---
    while (g_running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                printf("[Shutdown] SDL_QUIT received...\n");
                g_running = 0;
                break;
            }

            // Window Resizing
            if (ev.type == SDL_WINDOWEVENT) {
                if (ev.window.event == SDL_WINDOWEVENT_RESIZED ||
                    ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    g_viewport.update(ev.window.data1, ev.window.data2);
                }
            }

            // Controller Hotplugging
            if (ev.type == SDL_CONTROLLERDEVICEADDED) {
                printf("[Input] Gamepad connected/reconnected\n");
                open_first_controller();
            } else if (ev.type == SDL_CONTROLLERDEVICEREMOVED) {
                printf("[Input] Gamepad disconnected\n");
                open_first_controller();
            }

            // Keyboard Handling
            if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
                int down = (ev.type == SDL_KEYDOWN) ? 1 : 0;

                // Alt + Enter Fullscreen
                if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_RETURN && (ev.key.keysym.mod & KMOD_ALT)) {
                    toggle_fullscreen();
                    continue;
                }

                // Keyboard Start / Pause
                if (ev.key.keysym.sym == SDLK_RETURN ||
                    ev.key.keysym.sym == SDLK_KP_ENTER ||
                    ev.key.keysym.sym == SDLK_SPACE ||
                    ev.key.keysym.sym == SDLK_p) {
                    if (down) GamePadMgrBridge::dispatchStartPress(1);
                    continue;
                }

                // Keyboard Android Back Button
                if (ev.key.keysym.sym == SDLK_ESCAPE ||
                    ev.key.keysym.sym == SDLK_BACKSPACE) {
                    MockInputEvent ev_back;
                    memset(&ev_back, 0, sizeof(ev_back));
                    ev_back.type      = AINPUT_EVENT_TYPE_KEY;
                    ev_back.source    = AINPUT_SOURCE_KEYBOARD;
                    ev_back.deviceId  = 1;
                    ev_back.keyCode   = AKEYCODE_BACK;
                    ev_back.keyAction = down ? AKEY_EVENT_ACTION_DOWN : AKEY_EVENT_ACTION_UP;
                    dispatch_raw_input_event(&ev_back);
                    continue;
                }

                // Directional Keys (WASD / Arrows)
                int dpad_code = 0;
                switch (ev.key.keysym.sym) {
                    case SDLK_a: case SDLK_LEFT:  key_left = down;  dpad_code = AKEYCODE_DPAD_LEFT;  break;
                    case SDLK_d: case SDLK_RIGHT: key_right = down; dpad_code = AKEYCODE_DPAD_RIGHT; break;
                    case SDLK_w: case SDLK_UP:    key_up = down;    dpad_code = AKEYCODE_DPAD_UP;    break;
                    case SDLK_s: case SDLK_DOWN:  key_down = down;  dpad_code = AKEYCODE_DPAD_DOWN;  break;

                    // Combat Buttons
                    case SDLK_j: case SDLK_z: GamePadMgrBridge::dispatchButton(AKEYCODE_BUTTON_A, down); break;
                    case SDLK_u: case SDLK_x: GamePadMgrBridge::dispatchButton(AKEYCODE_BUTTON_X, down); break;
                    case SDLK_i: case SDLK_c: GamePadMgrBridge::dispatchButton(AKEYCODE_BUTTON_Y, down); break;
                    case SDLK_k: case SDLK_v: GamePadMgrBridge::dispatchButton(AKEYCODE_BUTTON_B, down); break;
                    case SDLK_q:              GamePadMgrBridge::dispatchButton(AKEYCODE_BUTTON_L1, down); break;
                    case SDLK_e:              GamePadMgrBridge::dispatchButton(AKEYCODE_BUTTON_R1, down); break;
                }
                if (dpad_code != 0) {
                    GamePadMgrBridge::dispatchDpad(dpad_code, down);
                }
            }

            // Gamepad Analog Stick
            if (ev.type == SDL_CONTROLLERAXISMOTION && g_controller) {
                if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    float ax = (float)SDL_GameControllerGetAxis(g_controller, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
                    float ay = (float)SDL_GameControllerGetAxis(g_controller, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
                    if (fabsf(ax) < 0.20f) ax = 0.0f;
                    if (fabsf(ay) < 0.20f) ay = 0.0f;
                    stick_x = ax;
                    stick_y = ay;
                }
            }

            // Gamepad Buttons
            // Gamepad Buttons
            if ((ev.type == SDL_CONTROLLERBUTTONDOWN || ev.type == SDL_CONTROLLERBUTTONUP) && g_controller) {
                int down = (ev.type == SDL_CONTROLLERBUTTONDOWN) ? 1 : 0;

                // Both Start and Back trigger Pause (AKEYCODE_BUTTON_SELECT with KEYBOARD source)
                if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_START ||
                    ev.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                    GamePadMgrBridge::dispatchStartPress(down);
                    continue;
                }

                int code = 0, dpad_code = 0;
                switch (ev.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_A:             code = AKEYCODE_BUTTON_A; break;  // Guard
                    case SDL_CONTROLLER_BUTTON_X:             code = AKEYCODE_BUTTON_X; break;  // Horizontal Attack
                    case SDL_CONTROLLER_BUTTON_Y:             code = AKEYCODE_BUTTON_Y; break;  // Vertical Attack
                    case SDL_CONTROLLER_BUTTON_B:             code = AKEYCODE_BUTTON_B; break;  // Kick
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  code = AKEYCODE_BUTTON_A; break;  // L1 = Guard
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: code = AKEYCODE_BUTTON_R1; break; // R1 = A+B+K Macro

                    case SDL_CONTROLLER_BUTTON_DPAD_UP:    dpad_up = down;    dpad_code = AKEYCODE_DPAD_UP;    break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  dpad_down = down;  dpad_code = AKEYCODE_DPAD_DOWN;  break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  dpad_left = down;  dpad_code = AKEYCODE_DPAD_LEFT;  break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: dpad_right = down; dpad_code = AKEYCODE_DPAD_RIGHT; break;
                }

                if (dpad_code != 0) GamePadMgrBridge::dispatchDpad(dpad_code, down);
                if (code != 0)      GamePadMgrBridge::dispatchButton(code, down);
            }

            // Mouse Touch Translation
            if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP) {
                float gx = 0.0f, gy = 0.0f;
                g_viewport.windowToGame((float)ev.button.x, (float)ev.button.y, &gx, &gy);

                MockInputEvent mock;
                memset(&mock, 0, sizeof(mock));
                mock.type         = AINPUT_EVENT_TYPE_MOTION;
                mock.source       = AINPUT_SOURCE_TOUCHSCREEN;
                mock.x            = gx;
                mock.y            = gy;
                mock.motionAction = (ev.type == SDL_MOUSEBUTTONDOWN) ? AMOTION_EVENT_ACTION_DOWN : AMOTION_EVENT_ACTION_UP;
                dispatch_raw_input_event(&mock);
            }
        }

        if (!g_running) break;

        // Combine stick & keyboard deflection
        float cur_x = stick_x, cur_y = stick_y;
        if (cur_x == 0.0f && cur_y == 0.0f) {
            cur_x = (float)((key_right || dpad_right) - (key_left || dpad_left));
            cur_y = (float)((key_down || dpad_down) - (key_up || dpad_up));
        }

        static float s_last_sent_x = 0.0f;
        static float s_last_sent_y = 0.0f;

        // Dispatch motion only on state changes (prevents queue congestion)
        if (fabsf(cur_x - s_last_sent_x) > 0.05f || fabsf(cur_y - s_last_sent_y) > 0.05f) {
            GamePadMgrBridge::dispatchMotionFrame(cur_x, cur_y);
            s_last_sent_x = cur_x;
            s_last_sent_y = cur_y;
        }

        Sleep(16);
    }

    // --- Fast, Clean Shutdown Sequence ---
    printf("[Shutdown] Step 1: Stopping audio mixer...\n");
    shutdown_mock_opensles();

    printf("[Shutdown] Step 2: Terminating background game thread...\n");
    if (hGameThread) {
        TerminateThread(hGameThread, 0);
        CloseHandle(hGameThread);
    }

    printf("[Shutdown] Step 3: Releasing SDL & OpenGL resources...\n");
    if (g_controller) {
        SDL_GameControllerClose(g_controller);
        g_controller = nullptr;
    }
    if (g_gl_context) {
        SDL_GL_MakeCurrent(g_window, nullptr);
        SDL_GL_DeleteContext(g_gl_context);
    }
    if (g_window) {
        SDL_DestroyWindow(g_window);
    }

    SDL_Quit();
    printf("[Shutdown] Clean exit complete.\n");
    ExitProcess(0);
}