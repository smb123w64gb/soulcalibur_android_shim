#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void init_mock_opensles();
void shutdown_mock_opensles();
void set_soundplayer3_tick_queue(void* fn);

#ifdef __cplusplus
}
#endif