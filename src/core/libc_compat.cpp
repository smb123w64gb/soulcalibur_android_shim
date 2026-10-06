#include "core/libc_compat.h"
#include "core/hooks.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <csetjmp>
#include <windows.h>
#include <io.h>
#include <zlib.h>
#include <SDL2/SDL.h>

// -----------------------------------------------------------------------------
// Bionic Data Symbols
// -----------------------------------------------------------------------------
static uint32_t fake_stack_chk_guard = 0x595a5b5c;
static uint8_t  fake_sF[1024] = {0};
static short    fake_toupper_tab_[257] = {0};

// -----------------------------------------------------------------------------
// OpenSL ES GUID Data Symbols
// -----------------------------------------------------------------------------
typedef struct { uint32_t a; uint16_t b, c; uint8_t d[8]; } FakeSL_IID;
static FakeSL_IID s_SL_IID_ENGINE       = { 0x8D2E4940, 0x1686, 0x11DF, {0x91,0xB3,0x00,0x02,0xA5,0xD5,0xC5,0x1B} };
static FakeSL_IID s_SL_IID_PLAY         = { 0xEFD6C080, 0x1686, 0x11DF, {0x8C,0xDE,0x00,0x02,0xA5,0xD5,0xC5,0x1B} };
static FakeSL_IID s_SL_IID_BUFFERQUEUE  = { 0x48421060, 0x1687, 0x11DF, {0xBE,0x53,0x00,0x02,0xA5,0xD5,0xC5,0x1B} };
static FakeSL_IID s_SL_IID_VOLUME       = { 0x09E84E60, 0x1687, 0x11DF, {0x89,0x97,0x00,0x02,0xA5,0xD5,0xC5,0x1B} };
static FakeSL_IID s_SL_IID_SEEK         = { 0x4B376660, 0x1687, 0x11DF, {0x89,0x97,0x00,0x02,0xA5,0xD5,0xC5,0x1B} };
static FakeSL_IID s_SL_IID_PLAYBACKRATE = { 0x2E304460, 0x1687, 0x11DF, {0x89,0x97,0x00,0x02,0xA5,0xD5,0xC5,0x1B} };

// -----------------------------------------------------------------------------
// C++ Allocators and Runtime Lifecycle
// -----------------------------------------------------------------------------
static void* __cdecl hook_new(size_t s) { return malloc(s); }
static void  __cdecl hook_delete(void* p) { free(p); }

static int  __cdecl hook_cxa_atexit(void (*f)(void*), void* a, void* d) { return 0; }
static void __cdecl hook_cxa_finalize(void* d) {}

static void* __cdecl hook_dlsym(void* handle, const char* name) {
    return HookRegistry::getInstance().resolve(name);
}

static void __cdecl hook_exit(int code) { exit(code); }
static void __cdecl hook_abort() { abort(); }

static void __cdecl hook_stack_chk_fail() {
    printf("[FATAL] Stack check failed!\n");
    abort();
}

static int* __cdecl hook_errno_stub() {
    return _errno();
}

// -----------------------------------------------------------------------------
// Protected Stdio Subsystem
// Prevents Bionic FILE* / __sF handles from dereferencing invalid MSVC CRT handles
// -----------------------------------------------------------------------------
#define MAX_TRACKED_FILES 128
static FILE* g_tracked_files[MAX_TRACKED_FILES] = {nullptr};

static void track_file(FILE* fp) {
    if (!fp) return;
    for (int i = 0; i < MAX_TRACKED_FILES; i++) {
        if (!g_tracked_files[i]) { g_tracked_files[i] = fp; return; }
    }
}

static void untrack_file(FILE* fp) {
    if (!fp) return;
    for (int i = 0; i < MAX_TRACKED_FILES; i++) {
        if (g_tracked_files[i] == fp) { g_tracked_files[i] = nullptr; return; }
    }
}

static FILE* translate_fp(FILE* fp) {
    if (!fp) return nullptr;
    uintptr_t p = (uintptr_t)fp;
    uintptr_t sF = (uintptr_t)fake_sF;

    if (p >= sF && p < sF + sizeof(fake_sF)) {
        uintptr_t offset = p - sF;
        if (offset < 84) return stdin;
        if (offset < 168) return stdout;
        return stderr;
    }

    if (fp == stdin || fp == stdout || fp == stderr) return fp;

    for (int i = 0; i < MAX_TRACKED_FILES; i++) {
        if (g_tracked_files[i] == fp) return fp;
    }
    return nullptr;
}

static FILE* __cdecl hook_fopen(const char* path, const char* mode) {
    if (!path || !mode) return nullptr;
    FILE* f = fopen(path, mode);
    if (!f) {
        char alt[512];
        snprintf(alt, sizeof(alt), "assets/%s", path);
        f = fopen(alt, mode);
    }
    if (f) track_file(f);
    return f;
}

static int __cdecl hook_fclose(FILE* fp) {
    FILE* v = translate_fp(fp);
    if (!v || v == stdin || v == stdout || v == stderr) return 0;
    untrack_file(v);
    return fclose(v);
}

static size_t __cdecl hook_fread(void* p, size_t s, size_t n, FILE* fp) {
    FILE* v = translate_fp(fp);
    return v ? fread(p, s, n, v) : 0;
}

static size_t __cdecl hook_fwrite(const void* p, size_t s, size_t n, FILE* fp) {
    FILE* v = translate_fp(fp);
    return v ? fwrite(p, s, n, v) : 0;
}

static int __cdecl hook_fseek(FILE* fp, long o, int w) {
    FILE* v = translate_fp(fp);
    return v ? fseek(v, o, w) : -1;
}

static long __cdecl hook_ftell(FILE* fp) {
    FILE* v = translate_fp(fp);
    return v ? ftell(v) : -1;
}

static int __cdecl hook_fflush(FILE* fp) {
    FILE* v = translate_fp(fp);
    return v ? fflush(v) : 0;
}

static int __cdecl hook_fputc(int c, FILE* fp) {
    FILE* v = translate_fp(fp);
    return v ? fputc(c, v) : c;
}

static int __cdecl hook_fprintf(FILE* fp, const char* fmt, ...) {
    FILE* v = translate_fp(fp);
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len > 0) {
        if (v && v != stdout && v != stderr && v != stdin) {
            fputs(buf, v);
        } else {
            printf("[Game Log] %s", buf);
            fflush(stdout);
        }
    }
    return len;
}

static int __cdecl hook_printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

static int __cdecl hook_sprintf(char* buffer, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsprintf(buffer, fmt, ap);
    va_end(ap);
    return ret;
}

static void __cdecl hook_qsort(void* base, size_t num, size_t size, int (__cdecl *compar)(const void*, const void*)) {
    qsort(base, num, size, compar);
}

static void* __cdecl hook_memchr(const void* s, int c, size_t n) {
    return (void*)memchr(s, c, n);
}

// -----------------------------------------------------------------------------
// POSIX I/O & Memory Mapping
// -----------------------------------------------------------------------------
static int __cdecl hook_pipe(int fildes[2]) { return _pipe(fildes, 4096, 0); }
static int __cdecl hook_read(int fd, void* buf, unsigned int count) { return _read(fd, buf, count); }
static int __cdecl hook_write(int fd, const void* buf, unsigned int count) { return _write(fd, buf, count); }
static int __cdecl hook_close(int fd) { return _close(fd); }

static void* __cdecl hook_mmap(void* addr, size_t len, int prot, int flags, int fd, off_t offset) {
    return VirtualAlloc(addr, len, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

static int __cdecl hook_munmap(void* addr, size_t len) {
    return VirtualFree(addr, 0, MEM_RELEASE) ? 0 : -1;
}

static int __cdecl hook_setjmp(jmp_buf env) { return setjmp(env); }
static void __cdecl hook_longjmp(jmp_buf env, int val) { longjmp(env, val); }

// -----------------------------------------------------------------------------
// Time & Random
// -----------------------------------------------------------------------------
struct my_timeval { long tv_sec; long tv_usec; };
struct my_timespec { long tv_sec; long tv_nsec; };

static int __cdecl hook_gettimeofday(my_timeval* tv, void* tz) {
    if (tv) {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        unsigned long long t = (((unsigned long long)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        t -= 116444736000000000ULL;
        tv->tv_sec  = (long)(t / 10000000ULL);
        tv->tv_usec = (long)((t % 10000000ULL) / 10);
    }
    return 0;
}

static int __cdecl hook_clock_gettime(int clk_id, my_timespec* tp) {
    if (tp) {
        static LARGE_INTEGER freq;
        static int init = 0;
        if (!init) { QueryPerformanceFrequency(&freq); init = 1; }
        LARGE_INTEGER count;
        QueryPerformanceCounter(&count);
        tp->tv_sec  = (long)(count.QuadPart / freq.QuadPart);
        tp->tv_nsec = (long)(((count.QuadPart % freq.QuadPart) * 1000000000ULL) / freq.QuadPart);
    }
    return 0;
}

static void __cdecl hook_usleep(unsigned int usec) {
    DWORD ms = usec / 1000;
    if (ms == 0) ms = 1;
    Sleep(ms);
}

static clock_t __cdecl hook_clock() { return clock(); }
static time_t  __cdecl hook_time(time_t* arg) { return time(arg); }
static tm*     __cdecl hook_gmtime(const time_t* timep) { return gmtime(timep); }

static unsigned long long g_rand48_seed = 1;
static void __cdecl hook_srand48(long seed) { g_rand48_seed = (((unsigned long long)seed) << 16) | 0x330E; }
static long __cdecl hook_lrand48() {
    g_rand48_seed = (0x5DEECE66DULL * g_rand48_seed + 0xB) & 0xFFFFFFFFFFFFULL;
    return (long)(g_rand48_seed >> 17);
}

// -----------------------------------------------------------------------------
// Disambiguated Math Wrappers
// -----------------------------------------------------------------------------
static float __cdecl hook_floorf(float x) { return floorf(x); }
static float __cdecl hook_sinf(float x)   { return sinf(x); }
static float __cdecl hook_cosf(float x)   { return cosf(x); }
static float __cdecl hook_sqrtf(float x)  { return sqrtf(x); }
static float __cdecl hook_atan2f(float y, float x) { return atan2f(y, x); }
static float __cdecl hook_powf(float x, float y)   { return powf(x, y); }
static float __cdecl hook_modff(float x, float* iptr) {
    double i;
    float f = (float)modf((double)x, &i);
    if (iptr) *iptr = (float)i;
    return f;
}
static float __cdecl hook_ldexpf(float x, int exp) { return (float)ldexp((double)x, exp); }

static double __cdecl hook_floor(double x) { return ::floor(x); }
static double __cdecl hook_sin(double x)   { return ::sin(x); }
static double __cdecl hook_cos(double x)   { return ::cos(x); }
static double __cdecl hook_tan(double x)   { return ::tan(x); }
static double __cdecl hook_sqrt(double x)  { return ::sqrt(x); }
static double __cdecl hook_atan2(double y, double x) { return ::atan2(y, x); }
static double __cdecl hook_pow(double x, double y)   { return ::pow(x, y); }
static double __cdecl hook_log(double x)   { return ::log(x); }
static double __cdecl hook_modf(double x, double* iptr) { return ::modf(x, iptr); }
static double __cdecl hook_frexp(double x, int* exp)    { return ::frexp(x, exp); }

// -----------------------------------------------------------------------------
// Android Log
// -----------------------------------------------------------------------------
static int __cdecl hook_android_log_print(int p, const char* t, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("[%s] ", t ? t : "NDK");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    return 0;
}

// -----------------------------------------------------------------------------
// POSIX Threads Emulation using SDL2 primitives
// -----------------------------------------------------------------------------
static int __cdecl hook_pthread_mutex_init(void** mutex, void* attr) {
    if (mutex) *mutex = SDL_CreateMutex();
    return 0;
}
static int __cdecl hook_pthread_mutex_lock(void** mutex) {
    if (mutex && *mutex) return SDL_LockMutex((SDL_mutex*)*mutex);
    return 0;
}
static int __cdecl hook_pthread_mutex_unlock(void** mutex) {
    if (mutex && *mutex) return SDL_UnlockMutex((SDL_mutex*)*mutex);
    return 0;
}
static int __cdecl hook_pthread_mutex_destroy(void** mutex) {
    if (mutex && *mutex) { SDL_DestroyMutex((SDL_mutex*)*mutex); *mutex = nullptr; }
    return 0;
}
static int __cdecl hook_pthread_cond_init(void** cond, void* attr) {
    if (cond) *cond = SDL_CreateCond();
    return 0;
}
static int __cdecl hook_pthread_cond_wait(void** cond, void** mutex) {
    if (cond && *cond && mutex && *mutex) {
        return SDL_CondWait((SDL_cond*)*cond, (SDL_mutex*)*mutex);
    }
    return 0;
}
static int __cdecl hook_pthread_cond_broadcast(void** cond) {
    if (cond && *cond) return SDL_CondBroadcast((SDL_cond*)*cond);
    return 0;
}
static int __cdecl hook_pthread_cond_destroy(void** cond) {
    if (cond && *cond) { SDL_DestroyCond((SDL_cond*)*cond); *cond = nullptr; }
    return 0;
}

typedef struct {
    void* (*func)(void*);
    void* arg;
} PthreadWrapperArg;

static int SDLCALL pthread_sdl_runner(void* data) {
    auto* a = (PthreadWrapperArg*)data;
    void* (*func)(void*) = a->func;
    void* arg = a->arg;
    free(a);
    func(arg);
    return 0;
}

static int __cdecl hook_pthread_create(void** thread, void* attr, void* (*start_routine)(void*), void* arg) {
    auto* a = (PthreadWrapperArg*)malloc(sizeof(PthreadWrapperArg));
    a->func = start_routine;
    a->arg = arg;
    SDL_Thread* t = SDL_CreateThread(pthread_sdl_runner, "PthreadSDL", a);
    if (thread) *thread = t;
    return (t != nullptr) ? 0 : -1;
}
static int __cdecl hook_pthread_join(void* thread, void** retval) {
    if (thread) SDL_WaitThread((SDL_Thread*)thread, nullptr);
    return 0;
}
static int __cdecl hook_pthread_attr_init(void* attr) { return 0; }
static int __cdecl hook_pthread_attr_setdetachstate(void* attr, int state) { return 0; }

// -----------------------------------------------------------------------------
// Initialization & Registration Table
// -----------------------------------------------------------------------------
extern "C" void init_libc_compat() {
    for (int i = 0; i < 257; i++) {
        fake_toupper_tab_[i] = (i >= 'a' && i <= 'z') ? (short)(i - 32) : (short)i;
    }

    // Bionic & Life Cycle
    REGISTER_SYMBOL_HOOK("__stack_chk_guard", &fake_stack_chk_guard);
    REGISTER_SYMBOL_HOOK("__stack_chk_fail", hook_stack_chk_fail);
    REGISTER_SYMBOL_HOOK("__sF", fake_sF);
    REGISTER_SYMBOL_HOOK("_toupper_tab_", &fake_toupper_tab_[1]);
    REGISTER_SYMBOL_HOOK("__errno", hook_errno_stub);
    REGISTER_SYMBOL_HOOK("__cxa_atexit", hook_cxa_atexit);
    REGISTER_SYMBOL_HOOK("__cxa_finalize", hook_cxa_finalize);
    REGISTER_SYMBOL_HOOK("dlsym", hook_dlsym);

    // OpenSL ES GUIDs
    REGISTER_SYMBOL_HOOK("SL_IID_ENGINE", &s_SL_IID_ENGINE);
    REGISTER_SYMBOL_HOOK("SL_IID_PLAY", &s_SL_IID_PLAY);
    REGISTER_SYMBOL_HOOK("SL_IID_BUFFERQUEUE", &s_SL_IID_BUFFERQUEUE);
    REGISTER_SYMBOL_HOOK("SL_IID_VOLUME", &s_SL_IID_VOLUME);
    REGISTER_SYMBOL_HOOK("SL_IID_SEEK", &s_SL_IID_SEEK);
    REGISTER_SYMBOL_HOOK("SL_IID_PLAYBACKRATE", &s_SL_IID_PLAYBACKRATE);

    // C++ Allocators
    REGISTER_SYMBOL_HOOK("_Znwj", hook_new);
    REGISTER_SYMBOL_HOOK("_Znaj", hook_new);
    REGISTER_SYMBOL_HOOK("_ZdlPv", hook_delete);
    REGISTER_SYMBOL_HOOK("_ZdaPv", hook_delete);

    // Time & System
    REGISTER_SYMBOL_HOOK("gettimeofday", hook_gettimeofday);
    REGISTER_SYMBOL_HOOK("clock_gettime", hook_clock_gettime);
    REGISTER_SYMBOL_HOOK("clock", hook_clock);
    REGISTER_SYMBOL_HOOK("time", hook_time);
    REGISTER_SYMBOL_HOOK("gmtime", hook_gmtime);
    REGISTER_SYMBOL_HOOK("usleep", hook_usleep);
    REGISTER_SYMBOL_HOOK("srand48", hook_srand48);
    REGISTER_SYMBOL_HOOK("lrand48", hook_lrand48);

    // POSIX I/O & Memory
    REGISTER_SYMBOL_HOOK("mmap", hook_mmap);
    REGISTER_SYMBOL_HOOK("munmap", hook_munmap);
    REGISTER_SYMBOL_HOOK("pipe", hook_pipe);
    REGISTER_SYMBOL_HOOK("read", hook_read);
    REGISTER_SYMBOL_HOOK("write", hook_write);
    REGISTER_SYMBOL_HOOK("close", hook_close);
    REGISTER_SYMBOL_HOOK("setjmp", hook_setjmp);
    REGISTER_SYMBOL_HOOK("longjmp", hook_longjmp);

    // POSIX Threads
    REGISTER_SYMBOL_HOOK("pthread_mutex_init", hook_pthread_mutex_init);
    REGISTER_SYMBOL_HOOK("pthread_mutex_lock", hook_pthread_mutex_lock);
    REGISTER_SYMBOL_HOOK("pthread_mutex_unlock", hook_pthread_mutex_unlock);
    REGISTER_SYMBOL_HOOK("pthread_mutex_destroy", hook_pthread_mutex_destroy);
    REGISTER_SYMBOL_HOOK("pthread_cond_init", hook_pthread_cond_init);
    REGISTER_SYMBOL_HOOK("pthread_cond_wait", hook_pthread_cond_wait);
    REGISTER_SYMBOL_HOOK("pthread_cond_broadcast", hook_pthread_cond_broadcast);
    REGISTER_SYMBOL_HOOK("pthread_cond_destroy", hook_pthread_cond_destroy);
    REGISTER_SYMBOL_HOOK("pthread_create", hook_pthread_create);
    REGISTER_SYMBOL_HOOK("pthread_join", hook_pthread_join);
    REGISTER_SYMBOL_HOOK("pthread_attr_init", hook_pthread_attr_init);
    REGISTER_SYMBOL_HOOK("pthread_attr_setdetachstate", hook_pthread_attr_setdetachstate);

    // Protected Stdio & Strings
    REGISTER_SYMBOL_HOOK("fopen", hook_fopen);
    REGISTER_SYMBOL_HOOK("fclose", hook_fclose);
    REGISTER_SYMBOL_HOOK("fread", hook_fread);
    REGISTER_SYMBOL_HOOK("fwrite", hook_fwrite);
    REGISTER_SYMBOL_HOOK("fseek", hook_fseek);
    REGISTER_SYMBOL_HOOK("ftell", hook_ftell);
    REGISTER_SYMBOL_HOOK("fflush", hook_fflush);
    REGISTER_SYMBOL_HOOK("fputc", hook_fputc);
    REGISTER_SYMBOL_HOOK("fprintf", hook_fprintf);

    REGISTER_SYMBOL_HOOK("malloc", malloc);
    REGISTER_SYMBOL_HOOK("free", free);
    REGISTER_SYMBOL_HOOK("calloc", calloc);
    REGISTER_SYMBOL_HOOK("realloc", realloc);
    REGISTER_SYMBOL_HOOK("memcpy", memcpy);
    REGISTER_SYMBOL_HOOK("memset", memset);
    REGISTER_SYMBOL_HOOK("memmove", memmove);
    REGISTER_SYMBOL_HOOK("memcmp", memcmp);
    REGISTER_SYMBOL_HOOK("memchr", hook_memchr);
    REGISTER_SYMBOL_HOOK("strlen", strlen);
    REGISTER_SYMBOL_HOOK("strcpy", strcpy);
    REGISTER_SYMBOL_HOOK("strcat", strcat);
    REGISTER_SYMBOL_HOOK("strcmp", strcmp);
    REGISTER_SYMBOL_HOOK("strdup", _strdup);
    REGISTER_SYMBOL_HOOK("strerror", strerror);
    REGISTER_SYMBOL_HOOK("strtod", strtod);
    REGISTER_SYMBOL_HOOK("atoi", atoi);
    REGISTER_SYMBOL_HOOK("sprintf", hook_sprintf);
    REGISTER_SYMBOL_HOOK("printf", hook_printf);
    REGISTER_SYMBOL_HOOK("qsort", hook_qsort);
    REGISTER_SYMBOL_HOOK("exit", hook_exit);
    REGISTER_SYMBOL_HOOK("abort", hook_abort);

    // Single-precision Math
    REGISTER_SYMBOL_HOOK("floorf", hook_floorf);
    REGISTER_SYMBOL_HOOK("sinf", hook_sinf);
    REGISTER_SYMBOL_HOOK("cosf", hook_cosf);
    REGISTER_SYMBOL_HOOK("sqrtf", hook_sqrtf);
    REGISTER_SYMBOL_HOOK("atan2f", hook_atan2f);
    REGISTER_SYMBOL_HOOK("powf", hook_powf);
    REGISTER_SYMBOL_HOOK("modff", hook_modff);
    REGISTER_SYMBOL_HOOK("ldexpf", hook_ldexpf);

    // Double-precision Math
    REGISTER_SYMBOL_HOOK("floor", hook_floor);
    REGISTER_SYMBOL_HOOK("sin", hook_sin);
    REGISTER_SYMBOL_HOOK("cos", hook_cos);
    REGISTER_SYMBOL_HOOK("tan", hook_tan);
    REGISTER_SYMBOL_HOOK("sqrt", hook_sqrt);
    REGISTER_SYMBOL_HOOK("atan2", hook_atan2);
    REGISTER_SYMBOL_HOOK("pow", hook_pow);
    REGISTER_SYMBOL_HOOK("log", hook_log);
    REGISTER_SYMBOL_HOOK("modf", hook_modf);
    REGISTER_SYMBOL_HOOK("frexp", hook_frexp);

    // Zlib
    REGISTER_SYMBOL_HOOK("crc32", crc32);
    REGISTER_SYMBOL_HOOK("inflate", inflate);
    REGISTER_SYMBOL_HOOK("inflateInit_", inflateInit_);
    REGISTER_SYMBOL_HOOK("inflateReset", inflateReset);
    REGISTER_SYMBOL_HOOK("inflateEnd", inflateEnd);
    REGISTER_SYMBOL_HOOK("deflate", deflate);
    REGISTER_SYMBOL_HOOK("deflateInit2_", deflateInit2_);
    REGISTER_SYMBOL_HOOK("deflateReset", deflateReset);
    REGISTER_SYMBOL_HOOK("deflateEnd", deflateEnd);

    // Android Log
    REGISTER_SYMBOL_HOOK("__android_log_print", hook_android_log_print);
}