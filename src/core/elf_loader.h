#pragma once
#include <cstdint>
#include <cstddef>

typedef struct {
    uint8_t*    base_addr;
    size_t      total_size;
    void*       entry_android_main;
    const char* strtab;
    void*       symtab;
} LoadedSO;

bool load_elf_so(const char* filename, LoadedSO* out);
void* find_symbol(LoadedSO* so, const char* target_name);