#include "core/hooks.h"
#include <cstdio>
#include <cstring>
#include <SDL.h>
#include <SDL_opengl.h>

HookRegistry& HookRegistry::getInstance() {
    static HookRegistry instance;
    return instance;
}

void HookRegistry::registerHook(const std::string& name, void* func) {
    m_hooks[name] = func;
}

void* HookRegistry::resolve(const char* name) {
    if (!name) return nullptr;

    // 1. Look up in the modular hook map
    auto it = m_hooks.find(name);
    if (it != m_hooks.end()) {
        return it->second;
    }

    // 2. Fallback to OpenGL driver
    if (strncmp(name, "gl", 2) == 0) {
        void* p = SDL_GL_GetProcAddress(name);
        if (p) return p;
    }

    printf("[Symbol Unresolved] %s\n", name);
    return nullptr;
}