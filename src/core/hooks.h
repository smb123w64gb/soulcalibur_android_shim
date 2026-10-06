#pragma once
#include <string>
#include <unordered_map>
#include <cstdint>

class HookRegistry {
public:
    static HookRegistry& getInstance();
    void registerHook(const std::string& name, void* func);
    void* resolve(const char* name);

private:
    HookRegistry() = default;
    std::unordered_map<std::string, void*> m_hooks;
};

// Helper function to register a hook and return a dummy int
inline int register_hook_helper(const char* name, void* func) {
    HookRegistry::getInstance().registerHook(name, func);
    return 0;
}

#define HOOK_CONCAT_INNER(a, b) a##b
#define HOOK_CONCAT(a, b) HOOK_CONCAT_INNER(a, b)

// Clean, standard C++ static initialization (no constructor name mismatches)
#define REGISTER_SYMBOL_HOOK(sym_name, func_ptr) \
    static const int HOOK_CONCAT(_hook_init_, __COUNTER__) = \
        register_hook_helper(sym_name, reinterpret_cast<void*>(func_ptr));