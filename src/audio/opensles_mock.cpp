#include "audio/opensles_mock.h"
#include "core/hooks.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <SDL2/SDL.h>

// Forward declarations of OpenSL ES types
typedef void (*slBufferQueueCallback)(void* caller, void* pContext);

typedef struct {
    uint32_t formatType, numChannels, samplesPerSec, bitsPerSample, containerSize, channelMask, endianness;
} FakeSLDataFormat_PCM;

typedef struct { void *pLocator, *pFormat; } FakeSLDataSource;
typedef struct { uint32_t count, playIndex; } FakeBufferQueueState;

typedef struct { uint32_t a; uint16_t b, c; uint8_t d[8]; } FakeSL_IID;
static const FakeSL_IID s_SL_IID_ENGINE_CHECK       = { 0x8D2E4940, 0x1686, 0x11DF, {0x91,0xB3,0x00,0x02,0xA5,0xD5,0xC5,0x1B} };
static const FakeSL_IID s_SL_IID_PLAY_CHECK         = { 0xEFD6C080, 0x1686, 0x11DF, {0x8C,0xDE,0x00,0x02,0xA5,0xD5,0xC5,0x1B} };
static const FakeSL_IID s_SL_IID_BUFFERQUEUE_CHECK  = { 0x48421060, 0x1687, 0x11DF, {0xBE,0x53,0x00,0x02,0xA5,0xD5,0xC5,0x1B} };
static const FakeSL_IID s_SL_IID_VOLUME_CHECK       = { 0x09E84E60, 0x1687, 0x11DF, {0x89,0x97,0x00,0x02,0xA5,0xD5,0xC5,0x1B} };
static const FakeSL_IID s_SL_IID_PLAYBACKRATE_CHECK = { 0x2E304460, 0x1687, 0x11DF, {0x89,0x97,0x00,0x02,0xA5,0xD5,0xC5,0x1B} };

static inline int is_valid_read_ptr(const void* ptr) {
    uintptr_t p = (uintptr_t)ptr;
    if (p < 0x10000) return 0;
    if (p == 0x8D2E4940 || p == 0xEFD6C080 || p == 0x48421060 ||
        p == 0x09E84E60 || p == 0x4B376660 || p == 0x2E304460) return 0;
    return 1;
}

static inline int safe_is_iid(const void* iid, const FakeSL_IID* expected) {
    if (!iid || !expected) return 0;
    uintptr_t val = (uintptr_t)iid;

    if (val == expected->a) return 1;
    if (iid == (const void*)expected) return 1;

    if (is_valid_read_ptr(iid)) {
        uint32_t first_dword = *(const uint32_t*)iid;
        if (first_dword == expected->a) return 1;
        if (is_valid_read_ptr((const void*)(uintptr_t)first_dword)) {
            if (*(const uint32_t*)(uintptr_t)first_dword == expected->a) return 1;
        }
    }
    return 0;
}

static void* mock_sl_engine_obj_vtable[32];
static void* mock_sl_engine_itf_vtable[32];
static void* mock_sl_player_obj_vtable[32];
static void* mock_sl_player_itf_vtable[32];
static void* mock_sl_bq_itf_vtable[32];
static void* mock_sl_volume_itf_vtable[32];
static void* mock_sl_rate_itf_vtable[32];

static void* g_engine_obj_inst = mock_sl_engine_obj_vtable;
static void* g_engine_itf_inst = mock_sl_engine_itf_vtable;
static void* g_output_mix_inst = mock_sl_engine_obj_vtable;

typedef struct {
    void* vtable;
    slBufferQueueCallback callback;
    void* context;
    int queued_buffers;
} MockPlayerBQ;

typedef struct {
    void* vtable;
    void* parent_player;
} MockPlayerPlay;

typedef struct {
    void* vtable;
    void* parent_player;
    int16_t millibels;
} MockPlayerVolume;

typedef struct {
    void* vtable;
    void* parent_player;
    int16_t rate;
} MockPlayerRate;

typedef struct {
    void*            vtable;
    MockPlayerBQ     bq;
    MockPlayerPlay   play;
    MockPlayerVolume volume;
    MockPlayerRate   rate;

    float            volume_scale;
    uint32_t         play_state; // 1 = STOPPED, 2 = PAUSED, 3 = PLAYING

    int              src_channels;
    int              src_rate;
    int              is_streaming;
    SDL_AudioStream* stream;

    volatile int     callback_pending;
    volatile int     refill_requested;
} MockAudioPlayer;

#define MAX_ACTIVE_PLAYERS 32
static MockAudioPlayer* g_active_players[MAX_ACTIVE_PLAYERS] = {nullptr};

// Low-power audio synchronization handles (Single definition)
static SDL_mutex*        g_audio_mutex     = nullptr;
static SDL_cond*         g_audio_cond      = nullptr;
static SDL_Thread*       g_audio_thread    = nullptr;
static SDL_AudioDeviceID g_audio_device    = 0;
static volatile int      g_audio_running   = 1;
static int               g_output_rate     = 44100;
static int               g_output_channels = 2;

typedef void* (*SoundPlayer3_tickQueue_t)(void* p, int divisor);
static SoundPlayer3_tickQueue_t real_SoundPlayer3_tickQueue = nullptr;

extern "C" void set_soundplayer3_tick_queue(void* fn) {
    real_SoundPlayer3_tickQueue = reinterpret_cast<SoundPlayer3_tickQueue_t>(fn);
    printf("[Audio] Registered SoundPlayer3_tickQueue at %p\n", real_SoundPlayer3_tickQueue);
}

static int32_t __cdecl mock_sl_ok(void* self, ...) { return 0; }

// --- Volume Interface ---
static int32_t __cdecl mock_sl_volume_set_level(void* self, int16_t level) {
    auto* vol = reinterpret_cast<MockPlayerVolume*>(self);
    if (vol && vol->parent_player) {
        auto* player = reinterpret_cast<MockAudioPlayer*>(vol->parent_player);
        SDL_LockMutex(g_audio_mutex);
        vol->millibels = level;
        if (level <= -9600) {
            player->volume_scale = 0.0f;
        } else {
            player->volume_scale = powf(10.0f, (float)level / 2000.0f);
            if (player->volume_scale > 1.0f) player->volume_scale = 1.0f;
        }
        SDL_UnlockMutex(g_audio_mutex);
    }
    return 0;
}
static int32_t __cdecl mock_sl_volume_get_level(void* self, int16_t* pLevel) {
    if (pLevel) {
        auto* vol = reinterpret_cast<MockPlayerVolume*>(self);
        *pLevel = vol ? vol->millibels : 0;
    }
    return 0;
}
static int32_t __cdecl mock_sl_volume_get_max(void* self, int16_t* pMaxLevel) {
    if (pMaxLevel) *pMaxLevel = 0;
    return 0;
}

// --- PlaybackRate Interface ---
static int32_t __cdecl mock_sl_rate_set_rate(void* self, int16_t rate) {
    auto* r = reinterpret_cast<MockPlayerRate*>(self);
    if (r && r->parent_player) {
        auto* player = reinterpret_cast<MockAudioPlayer*>(r->parent_player);
        SDL_LockMutex(g_audio_mutex);
        r->rate = rate;
        if (player->stream && rate > 0) {
            int effective_rate = (player->src_rate * 1000) / rate;
            SDL_FreeAudioStream(player->stream);
            player->stream = SDL_NewAudioStream(
                AUDIO_S16SYS, player->src_channels, effective_rate,
                AUDIO_S16SYS, g_output_channels, g_output_rate
            );
        }
        SDL_UnlockMutex(g_audio_mutex);
    }
    return 0;
}
static int32_t __cdecl mock_sl_rate_get_rate(void* self, int16_t* pRate) {
    auto* r = reinterpret_cast<MockPlayerRate*>(self);
    if (pRate) *pRate = r ? r->rate : 1000;
    return 0;
}
static int32_t __cdecl mock_sl_rate_get_rate_range(void* self, uint8_t idx, int16_t* pMin, int16_t* pMax, int16_t* pStep, uint32_t* pCaps) {
    if (pMin)  *pMin  = 500;
    if (pMax)  *pMax  = 2000;
    if (pStep) *pStep = 1;
    if (pCaps) *pCaps = 0;
    return 0;
}

// --- Play Interface ---
static int32_t __cdecl mock_sl_set_play_state(void* self, uint32_t state) {
    auto* play = reinterpret_cast<MockPlayerPlay*>(self);
    if (play && play->parent_player) {
        auto* player = reinterpret_cast<MockAudioPlayer*>(play->parent_player);
        SDL_LockMutex(g_audio_mutex);
        player->play_state = state;
        if (state == 1) { // STOPPED
            player->callback_pending = 0;
            player->refill_requested = 0;
            player->bq.queued_buffers = 0;
        }
        SDL_UnlockMutex(g_audio_mutex);
    }
    return 0;
}
static int32_t __cdecl mock_sl_get_play_state(void* self, uint32_t* pState) {
    if (pState) {
        auto* play = reinterpret_cast<MockPlayerPlay*>(self);
        if (play && play->parent_player) {
            auto* player = reinterpret_cast<MockAudioPlayer*>(play->parent_player);
            SDL_LockMutex(g_audio_mutex);
            *pState = player->play_state ? player->play_state : 1;
            SDL_UnlockMutex(g_audio_mutex);
        } else {
            *pState = 1;
        }
    }
    return 0;
}

// --- BufferQueue Interface ---
static int32_t __cdecl mock_sl_get_state(void* self, void* pState) {
    if (pState) {
        auto* state = reinterpret_cast<FakeBufferQueueState*>(pState);
        auto* bq = reinterpret_cast<MockPlayerBQ*>(self);
        SDL_LockMutex(g_audio_mutex);
        state->count = bq ? bq->queued_buffers : 0;
        state->playIndex = 0;
        SDL_UnlockMutex(g_audio_mutex);
    }
    return 0;
}
static int32_t __cdecl mock_sl_register_callback(void* self, slBufferQueueCallback callback, void* pContext) {
    auto* bq = reinterpret_cast<MockPlayerBQ*>(self);
    if (bq) {
        SDL_LockMutex(g_audio_mutex);
        bq->callback = callback;
        bq->context  = pContext;
        SDL_UnlockMutex(g_audio_mutex);
    }
    return 0;
}
static int32_t __cdecl mock_sl_clear(void* self) {
    auto* bq = reinterpret_cast<MockPlayerBQ*>(self);
    if (bq) {
        SDL_LockMutex(g_audio_mutex);
        bq->queued_buffers = 0;
        for (int i = 0; i < MAX_ACTIVE_PLAYERS; i++) {
            if (g_active_players[i] && &g_active_players[i]->bq == bq) {
                if (g_active_players[i]->stream) SDL_AudioStreamClear(g_active_players[i]->stream);
                g_active_players[i]->callback_pending = 0;
                g_active_players[i]->refill_requested = 0;
                break;
            }
        }
        SDL_UnlockMutex(g_audio_mutex);
    }
    return 0;
}
static int32_t __cdecl mock_sl_enqueue(void* self, const void* pBuf, uint32_t size) {
    auto* bq = reinterpret_cast<MockPlayerBQ*>(self);
    if (!bq || !pBuf || size == 0) return 0;

    SDL_LockMutex(g_audio_mutex);
    bq->queued_buffers++;
    MockAudioPlayer* player = nullptr;
    for (int i = 0; i < MAX_ACTIVE_PLAYERS; i++) {
        if (g_active_players[i] && &g_active_players[i]->bq == bq) {
            player = g_active_players[i];
            break;
        }
    }
    if (player && player->stream) {
        if (size >= 88200 && player->src_channels == 2) {
            player->is_streaming = 1;
        } else {
            player->is_streaming = 0;
        }

        player->refill_requested = 0;
        SDL_AudioStreamPut(player->stream, pBuf, size);
    }
    SDL_UnlockMutex(g_audio_mutex);
    return 0;
}

// --- Player Object Interface ---
static int32_t __cdecl mock_player_obj_get_interface(void* self, const void* iid, void** pInterface) {
    if (!pInterface) return 0;
    auto* player = reinterpret_cast<MockAudioPlayer*>(self);
    if (player->vtable != mock_sl_player_obj_vtable) {
        player = *reinterpret_cast<MockAudioPlayer**>(self);
    }

    if (safe_is_iid(iid, &s_SL_IID_BUFFERQUEUE_CHECK)) {
        *pInterface = &player->bq;
        return 0;
    }
    if (safe_is_iid(iid, &s_SL_IID_VOLUME_CHECK)) {
        *pInterface = &player->volume;
        return 0;
    }
    if (safe_is_iid(iid, &s_SL_IID_PLAYBACKRATE_CHECK)) {
        *pInterface = &player->rate;
        return 0;
    }
    if (safe_is_iid(iid, &s_SL_IID_PLAY_CHECK)) {
        *pInterface = &player->play;
        return 0;
    }

    *pInterface = &player->play;
    return 0;
}

static void __cdecl mock_player_obj_destroy(void* self) {
    auto* player = reinterpret_cast<MockAudioPlayer*>(self);
    if (!player) return;

    SDL_LockMutex(g_audio_mutex);
    player->play_state       = 1;
    player->bq.callback      = nullptr;
    player->callback_pending = 0;
    player->is_streaming     = 0;

    for (int i = 0; i < MAX_ACTIVE_PLAYERS; i++) {
        if (g_active_players[i] == player) {
            g_active_players[i] = nullptr;
            break;
        }
    }
    if (player->stream) {
        SDL_FreeAudioStream(player->stream);
        player->stream = nullptr;
    }
    SDL_UnlockMutex(g_audio_mutex);
}

// --- Engine Interface ---
static int32_t __cdecl mock_engine_obj_get_interface(void* self, const void* iid, void** pInterface) {
    if (pInterface) *pInterface = &g_engine_itf_inst;
    return 0;
}

static int32_t __cdecl mock_engine_create_audio_player(void* self, void** pPlayer, void* pSrc, void* pSink, uint32_t numItf, const void* iids, const void* req) {
    auto* player = reinterpret_cast<MockAudioPlayer*>(calloc(1, sizeof(MockAudioPlayer)));
    player->vtable               = mock_sl_player_obj_vtable;
    player->play.vtable          = mock_sl_player_itf_vtable;
    player->play.parent_player   = player;
    player->bq.vtable            = mock_sl_bq_itf_vtable;
    player->volume.vtable        = mock_sl_volume_itf_vtable;
    player->volume.parent_player = player;
    player->rate.vtable          = mock_sl_rate_itf_vtable;
    player->rate.parent_player   = player;
    player->rate.rate            = 1000;
    player->volume_scale         = 1.0f;
    player->play_state           = 1; // STOPPED

    player->src_channels = 2;
    player->src_rate     = 44100;

    if (pSrc) {
        auto* src = reinterpret_cast<FakeSLDataSource*>(pSrc);
        if (src->pFormat) {
            auto* pcm = reinterpret_cast<FakeSLDataFormat_PCM*>(src->pFormat);
            if (pcm->formatType == 2 || pcm->formatType == 1) { // SL_DATAFORMAT_PCM
                player->src_channels = pcm->numChannels ? pcm->numChannels : 2;
                uint32_t rate = pcm->samplesPerSec;
                if (rate > 100000) {
                    player->src_rate = rate / 1000;
                } else if (rate > 0) {
                    player->src_rate = rate;
                }

                if (player->src_rate >= 44000 && player->src_rate <= 44200) player->src_rate = 44100;
                else if (player->src_rate >= 22000 && player->src_rate <= 22100) player->src_rate = 22050;
                else if (player->src_rate >= 47900 && player->src_rate <= 48100) player->src_rate = 48000;

                printf("[Audio] Created player: %d channels, %d Hz (raw: %u)\n", 
                       player->src_channels, player->src_rate, rate);
            }
        }
    }

    player->stream = SDL_NewAudioStream(
        AUDIO_S16SYS, player->src_channels, player->src_rate,
        AUDIO_S16SYS, g_output_channels, g_output_rate
    );

    SDL_LockMutex(g_audio_mutex);
    for (int i = 0; i < MAX_ACTIVE_PLAYERS; i++) {
        if (!g_active_players[i]) {
            g_active_players[i] = player;
            break;
        }
    }
    SDL_UnlockMutex(g_audio_mutex);

    if (pPlayer) *pPlayer = player;
    return 0;
}

static int32_t __cdecl mock_engine_create_output_mix(void* self, void** pMix, uint32_t numItf, const void* iids, const void* req) {
    if (pMix) *pMix = &g_output_mix_inst;
    return 0;
}

// Master Audio Mixer Hardware Callback
#define MIX_SCRATCH_SAMPLES 16384
static void SDLCALL sdl_audio_mixer_callback(void* userdata, Uint8* stream, int len) {
    memset(stream, 0, len);
    int num_samples = len / sizeof(int16_t);
    if (num_samples > MIX_SCRATCH_SAMPLES) num_samples = MIX_SCRATCH_SAMPLES;

    int32_t mix_buffer[MIX_SCRATCH_SAMPLES];
    memset(mix_buffer, 0, num_samples * sizeof(int32_t));

    SDL_LockMutex(g_audio_mutex);
    for (int i = 0; i < MAX_ACTIVE_PLAYERS; i++) {
        MockAudioPlayer* player = g_active_players[i];
        if (!player || player->play_state != 3 || !player->stream) continue; // 3 = PLAYING

        int16_t temp_buf[MIX_SCRATCH_SAMPLES];
        int bytes_needed = num_samples * sizeof(int16_t);
        int got = SDL_AudioStreamGet(player->stream, temp_buf, bytes_needed);

        if (got > 0) {
            int got_samples = got / sizeof(int16_t);
            float scale = player->volume_scale;
            for (int s = 0; s < got_samples; s++) {
                mix_buffer[s] += (int32_t)(temp_buf[s] * scale);
            }
        }

        // Event-driven buffer refill check
        if (player->bq.callback && player->bq.queued_buffers > 0 &&
            !player->callback_pending && !player->refill_requested) {
            
            int avail = SDL_AudioStreamAvailable(player->stream);
            int threshold = player->is_streaming ? (g_output_rate * 2) : 0;

            if (avail <= threshold) {
                player->refill_requested = 1;
                player->bq.queued_buffers--;
                player->callback_pending = 1;

                if (g_audio_cond) {
                    SDL_CondSignal(g_audio_cond);
                }
            }
        }
    }
    SDL_UnlockMutex(g_audio_mutex);

    auto* out = reinterpret_cast<int16_t*>(stream);
    for (int s = 0; s < num_samples; s++) {
        int32_t val = (mix_buffer[s] * 80) / 100;
        if (val > 32767)  val = 32767;
        if (val < -32768) val = -32768;
        out[s] = (int16_t)val;
    }
}

static int SDLCALL audio_dispatch_thread(void* data) {
    SDL_LockMutex(g_audio_mutex);
    while (g_audio_running) {
        // Sleep on condition variable, with 20ms safety fallback
        SDL_CondWaitTimeout(g_audio_cond, g_audio_mutex, 20);

        if (!g_audio_running) break;

        for (int i = 0; i < MAX_ACTIVE_PLAYERS; i++) {
            MockAudioPlayer* player = g_active_players[i];
            if (player && player->play_state == 3 && player->callback_pending && player->bq.callback) {
                player->callback_pending = 0;
                slBufferQueueCallback cb = player->bq.callback;
                void* ctx                = player->bq.context;
                void* bq_ptr             = &player->bq;
                int is_bgm               = player->is_streaming;

                SDL_UnlockMutex(g_audio_mutex);

                if (is_bgm && real_SoundPlayer3_tickQueue && ctx) {
                    real_SoundPlayer3_tickQueue(ctx, 1);
                }

                cb(bq_ptr, ctx);

                SDL_LockMutex(g_audio_mutex);
            }
        }
    }
    SDL_UnlockMutex(g_audio_mutex);
    return 0;
}

static int __cdecl hook_slCreateEngine(void** outEngine, uint32_t n, void* o, uint32_t ni, void* iids, void* req) {
    if (outEngine) *outEngine = &g_engine_obj_inst;
    return 0;
}

REGISTER_SYMBOL_HOOK("slCreateEngine", hook_slCreateEngine);

extern "C" void init_mock_opensles() {
    for (int i = 0; i < 32; i++) {
        mock_sl_engine_obj_vtable[i] = (void*)mock_sl_ok;
        mock_sl_engine_itf_vtable[i] = (void*)mock_sl_ok;
        mock_sl_player_obj_vtable[i] = (void*)mock_sl_ok;
        mock_sl_player_itf_vtable[i] = (void*)mock_sl_ok;
        mock_sl_bq_itf_vtable[i]     = (void*)mock_sl_ok;
        mock_sl_volume_itf_vtable[i] = (void*)mock_sl_ok;
        mock_sl_rate_itf_vtable[i]   = (void*)mock_sl_ok;
    }

    mock_sl_engine_obj_vtable[3] = (void*)mock_engine_obj_get_interface;
    mock_sl_engine_itf_vtable[2] = (void*)mock_engine_create_audio_player;
    mock_sl_engine_itf_vtable[7] = (void*)mock_engine_create_output_mix;

    mock_sl_player_obj_vtable[3] = (void*)mock_player_obj_get_interface;
    mock_sl_player_obj_vtable[6] = (void*)mock_player_obj_destroy;

    mock_sl_player_itf_vtable[0] = (void*)mock_sl_set_play_state;
    mock_sl_player_itf_vtable[1] = (void*)mock_sl_get_play_state;

    mock_sl_bq_itf_vtable[0]     = (void*)mock_sl_enqueue;
    mock_sl_bq_itf_vtable[1]     = (void*)mock_sl_clear;
    mock_sl_bq_itf_vtable[2]     = (void*)mock_sl_get_state;
    mock_sl_bq_itf_vtable[3]     = (void*)mock_sl_register_callback;

    mock_sl_volume_itf_vtable[0] = (void*)mock_sl_volume_set_level;
    mock_sl_volume_itf_vtable[1] = (void*)mock_sl_volume_get_level;
    mock_sl_volume_itf_vtable[2] = (void*)mock_sl_volume_get_max;

    mock_sl_rate_itf_vtable[0]   = (void*)mock_sl_rate_set_rate;
    mock_sl_rate_itf_vtable[1]   = (void*)mock_sl_rate_get_rate;
    mock_sl_rate_itf_vtable[5]   = (void*)mock_sl_rate_get_rate_range;

    g_audio_mutex = SDL_CreateMutex();
    g_audio_cond  = SDL_CreateCond();

    SDL_AudioSpec wanted, have;
    memset(&wanted, 0, sizeof(wanted));
    wanted.freq     = 44100;
    wanted.format   = AUDIO_S16SYS;
    wanted.channels = 2;
    wanted.samples  = 1024;
    wanted.callback = sdl_audio_mixer_callback;

    g_audio_device = SDL_OpenAudioDevice(nullptr, 0, &wanted, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (g_audio_device) {
        g_output_rate     = have.freq;
        g_output_channels = have.channels;
        printf("[Audio] Opened hardware audio device: %d Hz, %d channels (buffer: %d samples)\n",
               have.freq, have.channels, have.samples);
        SDL_PauseAudioDevice(g_audio_device, 0);
    }

    g_audio_thread = SDL_CreateThread(audio_dispatch_thread, "AudioThread", nullptr);
}

extern "C" void shutdown_mock_opensles() {
    g_audio_running = 0;

    if (g_audio_cond) {
        SDL_CondBroadcast(g_audio_cond);
    }

    if (g_audio_device) {
        SDL_PauseAudioDevice(g_audio_device, 1);
        SDL_CloseAudioDevice(g_audio_device);
        g_audio_device = 0;
    }
    if (g_audio_thread) {
        SDL_WaitThread(g_audio_thread, nullptr);
        g_audio_thread = nullptr;
    }
    if (g_audio_cond) {
        SDL_DestroyCond(g_audio_cond);
        g_audio_cond = nullptr;
    }
    if (g_audio_mutex) {
        SDL_DestroyMutex(g_audio_mutex);
        g_audio_mutex = nullptr;
    }
}