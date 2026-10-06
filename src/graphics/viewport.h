#pragma once

#define GAME_CANVAS_W 1280.0f
#define GAME_CANVAS_H 720.0f
#define GAME_ASPECT   (GAME_CANVAS_W / GAME_CANVAS_H)

struct ViewportState {
    int window_w = 1280;
    int window_h = 720;
    int vp_x = 0;
    int vp_y = 0;
    int vp_w = 1280;
    int vp_h = 720;

    void update(int w, int h) {
        if (w <= 0 || h <= 0) return;
        window_w = w;
        window_h = h;

        float current_aspect = static_cast<float>(w) / static_cast<float>(h);
        if (current_aspect >= GAME_ASPECT) {
            vp_h = h;
            vp_w = static_cast<int>(h * GAME_ASPECT);
            vp_x = (w - vp_w) / 2;
            vp_y = 0;
        } else {
            vp_w = w;
            vp_h = static_cast<int>(w / GAME_ASPECT);
            vp_x = 0;
            vp_y = (h - vp_h) / 2;
        }
    }

    void windowToGame(float win_x, float win_y, float* out_gx, float* out_gy) const {
        if (!out_gx || !out_gy) return;
        float local_x = win_x - static_cast<float>(vp_x);
        float local_y = win_y - static_cast<float>(vp_y);

        float gx = (local_x / static_cast<float>(vp_w)) * GAME_CANVAS_W;
        float gy = (local_y / static_cast<float>(vp_h)) * GAME_CANVAS_H;

        if (gx < 0.0f) gx = 0.0f;
        if (gx > GAME_CANVAS_W) gx = GAME_CANVAS_W;
        if (gy < 0.0f) gy = 0.0f;
        if (gy > GAME_CANVAS_H) gy = GAME_CANVAS_H;

        *out_gx = gx;
        *out_gy = gy;
    }
};

extern ViewportState g_viewport;