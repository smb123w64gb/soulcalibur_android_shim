#include "graphics/egl_shim.h"
#include "graphics/viewport.h"
#include "core/hooks.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

static void* __cdecl hook_eglGetDisplay(void* d) { return (void*)0x8001; }
static int   __cdecl hook_eglInitialize(void* d, int* maj, int* min) { if (maj) *maj = 1; if (min) *min = 4; return 1; }
static int   __cdecl hook_eglChooseConfig(void* d, const int* a, void** c, int cs, int* nc) { if (c && cs > 0) c[0] = (void*)0x8002; if (nc) *nc = 1; return 1; }
static void* __cdecl hook_eglCreateContext(void* d, void* c, void* sc, const int* a) { return (void*)0x8003; }
static void* __cdecl hook_eglCreateWindowSurface(void* d, void* c, void* w, const int* a) { return (void*)0x8004; }
static int   __cdecl hook_eglQuerySurface(void* d, void* s, int attr, int* v) {
    if (!v) return 0;
    if (attr == 0x3057) { *v = 1280; return 1; }
    if (attr == 0x3056) { *v = 720;  return 1; }
    *v = 0; return 1;
}
static int __cdecl hook_eglMakeCurrent(void* d, void* dr, void* rd, void* c) { return 1; }

static int __cdecl hook_eglSwapBuffers(void* d, void* s) {
    SDL_Window* win = SDL_GL_GetCurrentWindow();
    if (win) {
        if (g_viewport.vp_x > 0 || g_viewport.vp_y > 0) {
            glEnable(GL_SCISSOR_TEST);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            if (g_viewport.vp_x > 0) {
                glScissor(0, 0, g_viewport.vp_x, g_viewport.window_h);
                glClear(GL_COLOR_BUFFER_BIT);
                glScissor(g_viewport.vp_x + g_viewport.vp_w, 0, g_viewport.vp_x + 2, g_viewport.window_h);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            if (g_viewport.vp_y > 0) {
                glScissor(0, 0, g_viewport.window_w, g_viewport.vp_y);
                glClear(GL_COLOR_BUFFER_BIT);
                glScissor(0, g_viewport.vp_y + g_viewport.vp_h, g_viewport.window_w, g_viewport.vp_y + 2);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            glDisable(GL_SCISSOR_TEST);
        }
        SDL_GL_SwapWindow(win);
    }
    return 1;
}
static int __cdecl hook_eglGetConfigAttrib(void* d, void* c, int a, int* v) {
    if (v) *v = 0;
    return 1;
}
static int __cdecl hook_eglDestroyContext(void* d, void* c) { return 1; }
static int __cdecl hook_eglDestroySurface(void* d, void* s) { return 1; }

REGISTER_SYMBOL_HOOK("eglGetConfigAttrib", hook_eglGetConfigAttrib);
REGISTER_SYMBOL_HOOK("eglDestroyContext", hook_eglDestroyContext);
REGISTER_SYMBOL_HOOK("eglDestroySurface", hook_eglDestroySurface);
REGISTER_SYMBOL_HOOK("eglGetDisplay", hook_eglGetDisplay);
REGISTER_SYMBOL_HOOK("eglInitialize", hook_eglInitialize);
REGISTER_SYMBOL_HOOK("eglChooseConfig", hook_eglChooseConfig);
REGISTER_SYMBOL_HOOK("eglCreateContext", hook_eglCreateContext);
REGISTER_SYMBOL_HOOK("eglCreateWindowSurface", hook_eglCreateWindowSurface);
REGISTER_SYMBOL_HOOK("eglQuerySurface", hook_eglQuerySurface);
REGISTER_SYMBOL_HOOK("eglMakeCurrent", hook_eglMakeCurrent);
REGISTER_SYMBOL_HOOK("eglSwapBuffers", hook_eglSwapBuffers);
REGISTER_SYMBOL_HOOK("eglTerminate", hook_eglMakeCurrent);