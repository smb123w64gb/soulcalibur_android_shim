#include "core/elf_loader.h"
#include "core/hooks.h"
#include "audio/opensles_mock.h"
#include "input/gamepad_bridge.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>

// -----------------------------------------------------------------------------
// Self-Contained ELF32 Definitions
// -----------------------------------------------------------------------------
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
} Elf32_Phdr;

typedef struct {
    int32_t  d_tag;
    union { uint32_t d_val; uint32_t d_ptr; } d_un;
} Elf32_Dyn;

typedef struct {
    uint32_t r_offset, r_info;
} Elf32_Rel;

typedef struct {
    uint32_t st_name, st_value, st_size;
    uint8_t  st_info, st_other;
    uint16_t st_shndx;
} Elf32_Sym;

#define PT_LOAD       1
#define PT_DYNAMIC    2
#define DT_NULL       0
#define DT_STRTAB     5
#define DT_SYMTAB     6
#define DT_REL        17
#define DT_RELSZ      18
#define DT_JMPREL     23
#define DT_PLTRELSZ   2
#define DT_INIT_ARRAY 25
#define DT_INIT_ARRAYSZ 27

#define ELF32_R_SYM(val)  ((val) >> 8)
#define ELF32_R_TYPE(val) ((val) & 0xff)

#define R_386_32       1
#define R_386_GLOB_DAT 6
#define R_386_JMP_SLOT 7
#define R_386_RELATIVE 8

static void trap_unresolved_symbol() {
    printf("\n[FATAL] Executed an unresolved symbol stub!\n");
    getchar();
    exit(1);
}

// -----------------------------------------------------------------------------
// NrPng Protection Hook (Prevents Crash on Non-PNG Payloads)
// -----------------------------------------------------------------------------
static const uint8_t s_fallback_png[67] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
    0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89,
    0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54,
    0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00, 0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4,
    0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
    0xAE, 0x42, 0x60, 0x82
};

typedef void (*NrPng_setData_t)(void* nrPng, const char* data, int size);
static NrPng_setData_t real_NrPng_setData = nullptr;
static uint8_t s_nrpng_orig_bytes[5] = {0};

static void patch_nrpng_trampoline(void* target, bool install);

static void hook_NrPng_setData(void* nrPng, const char* data, int size) {
    const char* use_data = data;
    int use_size = size;

    if (!data || size < 8) {
        printf("[NrPng] Skipping invalid/empty data (%d bytes) -> using 1x1 fallback\n", size);
        use_data = reinterpret_cast<const char*>(s_fallback_png);
        use_size = sizeof(s_fallback_png);
    } else {
        const uint8_t* u = reinterpret_cast<const uint8_t*>(data);
        if (u[0] != 0x89 || u[1] != 0x50 || u[2] != 0x4E || u[3] != 0x47) {
            printf("[NrPng] Non-PNG data (%d bytes, magic: %02X %02X %02X %02X) -> using 1x1 fallback\n",
                   size, u[0], u[1], u[2], u[3]);
            use_data = reinterpret_cast<const char*>(s_fallback_png);
            use_size = sizeof(s_fallback_png);
        }
    }

    if (real_NrPng_setData) {
        patch_nrpng_trampoline(reinterpret_cast<void*>(real_NrPng_setData), false);
        real_NrPng_setData(nrPng, use_data, use_size);
        patch_nrpng_trampoline(reinterpret_cast<void*>(real_NrPng_setData), true);
    }
}

static void patch_nrpng_trampoline(void* target, bool install) {
    if (!target) return;
    uint8_t* p = reinterpret_cast<uint8_t*>(target);
    DWORD oldProt;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProt);

    if (install) {
        if (s_nrpng_orig_bytes[0] == 0) {
            memcpy(s_nrpng_orig_bytes, p, 5);
        }
        uint32_t rel = reinterpret_cast<uint32_t>(hook_NrPng_setData) - (reinterpret_cast<uint32_t>(p) + 5);
        uint8_t jmp[5] = { 0xE9, 0, 0, 0, 0 };
        memcpy(&jmp[1], &rel, 4);
        memcpy(p, jmp, 5);
    } else {
        memcpy(p, s_nrpng_orig_bytes, 5);
    }

    VirtualProtect(p, 5, oldProt, &oldProt);
}

// -----------------------------------------------------------------------------
// Voice Engine Diagnostics
// -----------------------------------------------------------------------------
typedef void (*SoundEngine_playVoice_t)(void* engine, int charId, int voiceId, float pitch);
static SoundEngine_playVoice_t real_SoundEngine_playVoice = nullptr;

static void hook_SoundEngine_playVoice(void* engine, int charId, int voiceId, float pitch) {
    printf("[Voice Play] charId=%d, voiceId=%d, requested_pitch=%.4f\n", charId, voiceId, pitch);
    if (real_SoundEngine_playVoice) {
        real_SoundEngine_playVoice(engine, charId, voiceId, pitch);
    }
}

// -----------------------------------------------------------------------------
// Symbol Finding and Relocation
// -----------------------------------------------------------------------------
void* find_symbol(LoadedSO* so, const char* target_name) {
    if (!so->symtab || !so->strtab) return nullptr;
    auto* symtab = reinterpret_cast<Elf32_Sym*>(so->symtab);
    for (int i = 1; reinterpret_cast<uint8_t*>(&symtab[i]) < reinterpret_cast<const uint8_t*>(so->strtab); i++) {
        if (symtab[i].st_name == 0) continue;
        const char* name = so->strtab + symtab[i].st_name;
        if (strcmp(name, target_name) == 0) {
            return reinterpret_cast<void*>(so->base_addr + symtab[i].st_value);
        }
    }
    return nullptr;
}

bool load_elf_so(const char* filename, LoadedSO* out) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        printf("Failed to open %s\n", filename);
        return false;
    }
    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    auto* file_data = reinterpret_cast<uint8_t*>(malloc(file_size));
    fread(file_data, 1, file_size, f);
    fclose(f);

    auto* ehdr = reinterpret_cast<Elf32_Ehdr*>(file_data);
    auto* phdrs = reinterpret_cast<Elf32_Phdr*>(file_data + ehdr->e_phoff);

    uint32_t min_vaddr = 0xFFFFFFFF, max_vaddr = 0;
    Elf32_Phdr* dyn_phdr = nullptr;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            if (phdrs[i].p_vaddr < min_vaddr) min_vaddr = phdrs[i].p_vaddr;
            if (phdrs[i].p_vaddr + phdrs[i].p_memsz > max_vaddr) max_vaddr = phdrs[i].p_vaddr + phdrs[i].p_memsz;
        } else if (phdrs[i].p_type == PT_DYNAMIC) {
            dyn_phdr = &phdrs[i];
        }
    }

    size_t mem_size = max_vaddr - min_vaddr;
    printf("[Loader] Allocating 0x%X bytes via VirtualAlloc...\n", (unsigned int)mem_size);

    auto* base = reinterpret_cast<uint8_t*>(VirtualAlloc(nullptr, mem_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!base) {
        free(file_data);
        return false;
    }
    printf("[Loader] Mapped libsoul.so base address: %p\n", base);

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            uint8_t* seg_dest = base + (phdrs[i].p_vaddr - min_vaddr);
            memcpy(seg_dest, file_data + phdrs[i].p_offset, phdrs[i].p_filesz);
            if (phdrs[i].p_memsz > phdrs[i].p_filesz) {
                memset(seg_dest + phdrs[i].p_filesz, 0, phdrs[i].p_memsz - phdrs[i].p_filesz);
            }
        }
    }

    auto* dyn = reinterpret_cast<Elf32_Dyn*>(base + (dyn_phdr->p_vaddr - min_vaddr));
    const char* strtab = nullptr;
    Elf32_Sym*  symtab = nullptr;
    Elf32_Rel   *rel_dyn = nullptr, *rel_plt = nullptr;
    size_t      rel_dyn_sz = 0, rel_plt_sz = 0;
    uint32_t*   init_array = nullptr;
    size_t      init_array_sz = 0;

    for (int i = 0; dyn[i].d_tag != DT_NULL; i++) {
        switch (dyn[i].d_tag) {
            case DT_STRTAB:       strtab = reinterpret_cast<const char*>(base + dyn[i].d_un.d_ptr); break;
            case DT_SYMTAB:       symtab = reinterpret_cast<Elf32_Sym*>(base + dyn[i].d_un.d_ptr); break;
            case DT_REL:          rel_dyn = reinterpret_cast<Elf32_Rel*>(base + dyn[i].d_un.d_ptr); break;
            case DT_RELSZ:        rel_dyn_sz = dyn[i].d_un.d_val; break;
            case DT_JMPREL:       rel_plt = reinterpret_cast<Elf32_Rel*>(base + dyn[i].d_un.d_ptr); break;
            case DT_PLTRELSZ:     rel_plt_sz = dyn[i].d_un.d_val; break;
            case DT_INIT_ARRAY:   init_array = reinterpret_cast<uint32_t*>(base + dyn[i].d_un.d_ptr); break;
            case DT_INIT_ARRAYSZ: init_array_sz = dyn[i].d_un.d_val; break;
        }
    }

    out->base_addr = base;
    out->total_size = mem_size;
    out->strtab = strtab;
    out->symtab = symtab;

    Elf32_Rel* tables[2] = { rel_dyn, rel_plt };
    size_t sizes[2] = { rel_dyn_sz, rel_plt_sz };

    for (int t = 0; t < 2; t++) {
        if (!tables[t]) continue;
        int count = sizes[t] / sizeof(Elf32_Rel);
        for (int i = 0; i < count; i++) {
            Elf32_Rel* rel = &tables[t][i];
            uint32_t type = ELF32_R_TYPE(rel->r_info);
            uint32_t sym_idx = ELF32_R_SYM(rel->r_info);
            auto* target = reinterpret_cast<uint32_t*>(base + rel->r_offset);

            if (type == R_386_RELATIVE) {
                *target += (uint32_t)base;
            } else if (type == R_386_JMP_SLOT || type == R_386_GLOB_DAT || type == R_386_32) {
                const char* sym_name = strtab + symtab[sym_idx].st_name;
                void* resolved = HookRegistry::getInstance().resolve(sym_name);
                if (resolved) {
                    *target = reinterpret_cast<uint32_t>(resolved);
                } else {
                    *target = reinterpret_cast<uint32_t>(trap_unresolved_symbol);
                }
            }
        }
    }
    printf("[Loader] Relocations applied successfully.\n");

    // In-Memory Engine Patches
    void* lic = find_symbol(out, "_Z19waitForLicenseCheckP11android_appP6engine");
    if (!lic) lic = base + 0x000c99c0;
    uint8_t ret1[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 }; // mov eax, 1; ret
    memcpy(lic, ret1, 6);

    void* exp = find_symbol(out, "_Z25waitForExpansionFileReadyP11android_appP6engine");
    if (!exp) exp = base + 0x000c9a30;
    memcpy(exp, ret1, 6);

    void* gdp = find_symbol(out, "_ZN10JniService11getDataPathEv");
    if (gdp) {
        static const char* save_path = "./save/";
        uint8_t patch_path[] = { 0xB8, 0x00, 0x00, 0x00, 0x00, 0xC3 };
        *reinterpret_cast<uint32_t*>(&patch_path[1]) = reinterpret_cast<uint32_t>(save_path);
        memcpy(gdp, patch_path, sizeof(patch_path));
    }

    uint8_t* play_check = base + 0x000fd1bc;
    if (play_check[0] == 0x74 && play_check[1] == 0x63) {
        play_check[0] = 0x90; play_check[1] = 0x90;
    }
    uint8_t* thread_loop = base + 0x000fcb3b;
    if (thread_loop[0] == 0x75 && thread_loop[1] == 0x5B) {
        thread_loop[0] = 0xEB;
    }

    void* sp3_tick = find_symbol(out, "SoundPlayer3_tickQueue");
    if (!sp3_tick) sp3_tick = find_symbol(out, "_Z22SoundPlayer3_tickQueueP12SoundPlayer3i");
    if (!sp3_tick) sp3_tick = find_symbol(out, "_ZN12SoundPlayer39tickQueueEi");
    if (sp3_tick) {
        set_soundplayer3_tick_queue(sp3_tick);
        printf("[Audio] Registered SoundPlayer3_tickQueue at %p\n", sp3_tick);
    }

    typedef void (*nrthread_ctor_t)(void*);
    auto nrthread_ctor = reinterpret_cast<nrthread_ctor_t>(base + 0x000dae10);
    void* sp3_inst = find_symbol(out, "_ZN12SoundPlayer38instanceE");
    if (!sp3_inst) sp3_inst = find_symbol(out, "_ZN12SoundPlayer312soundPlayer3E");
    if (sp3_inst) {
        void** thread_ptr = reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(sp3_inst) + 0x30);
        nrthread_ctor(thread_ptr);
    }

    void* padMgr = find_symbol(out, "GamePadMgr_gamePadMgr");
    if (!padMgr) padMgr = find_symbol(out, "_ZN10GamePadMgr10gamePadMgrE");
    void* setBtn = find_symbol(out, "_ZN10GamePadMgr9setButtonEiji");
    GamePadMgrBridge::init(padMgr, setBtn);

    // NrPng::setData Entry Hook
    real_NrPng_setData = reinterpret_cast<NrPng_setData_t>(find_symbol(out, "_ZN5NrPng7setDataEPKci"));
    if (!real_NrPng_setData) real_NrPng_setData = reinterpret_cast<NrPng_setData_t>(find_symbol(out, "NrPng_setData"));
    if (real_NrPng_setData) {
        patch_nrpng_trampoline(reinterpret_cast<void*>(real_NrPng_setData), true);
        printf("[Patch] Hooked NrPng::setData entry point\n");
    }

    // Voice Engine Diagnostics
    real_SoundEngine_playVoice = reinterpret_cast<SoundEngine_playVoice_t>(
        find_symbol(out, "_ZN11SoundEngine9playVoiceEiif"));
    if (real_SoundEngine_playVoice) {
        HookRegistry::getInstance().registerHook("_ZN11SoundEngine9playVoiceEiif", 
            reinterpret_cast<void*>(hook_SoundEngine_playVoice));
    }

    // Execute INIT_ARRAY Constructors
    if (init_array && init_array_sz > 0) {
        int count = init_array_sz / sizeof(uint32_t);
        printf("[Loader] Executing %d constructors from INIT_ARRAY...\n", count);
        for (int i = 0; i < count; i++) {
            if (init_array[i]) {
                typedef void (*init_func_t)();
                uint32_t addr = init_array[i];
                auto fn = reinterpret_cast<init_func_t>((addr >= (uint32_t)base && addr < (uint32_t)(base + mem_size))
                                                        ? addr : (uint32_t)(base + addr));
                fn();
            }
        }
    }

    out->entry_android_main = find_symbol(out, "android_main");
    free(file_data);
    return true;
}