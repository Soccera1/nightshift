#define SDL_MAIN_HANDLED
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#elif __has_include(<SDL.h>)
#include <SDL.h>
#else
#error "SDL2 headers are required. Install SDL2 development files or pass SDL_CFLAGS/SDL_PREFIX to make."
#endif
#ifdef main
#undef main
#endif

#include "game.h"
#include "audio.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NIGHTSHIFT_VERSION
#define NIGHTSHIFT_VERSION "dev"
#endif

enum {
    WINDOW_W = 960,
    WINDOW_H = 540,
};

typedef enum SimMode {
    SIM_NONE,
    SIM_DEFENDED,
    SIM_IDLE,
} SimMode;

typedef enum ScreenshotScene {
    SHOT_TITLE,
    SHOT_TITLE_CLEARED,
    SHOT_EXTRAS,
    SHOT_OFFICE,
    SHOT_CAMERA,
    SHOT_CAMERA_STAGE,
    SHOT_CAMERA_DINING,
    SHOT_CAMERA_HALL,
    SHOT_CAMERA_BACKSTAGE,
    SHOT_WIN,
    SHOT_LOSS_RUST,
    SHOT_LOSS_VOLT,
    SHOT_LOSS_SKITR,
    SHOT_LOSS_ECHO,
    SHOT_BLACKOUT,
} ScreenshotScene;

typedef struct RunConfig {
    const char *save_path;
    const char *settings_path;
    float night_seconds;
    int story_night_override;
    int scale_override;
    bool custom_night;
    int custom_ai[THREAT_COUNT];
    bool fullscreen_override;
    bool mute_override;
    bool reset_save;
    bool show_help;
    bool show_version;
    bool render_test;
    bool audio_test;
    const char *screenshot_path;
    ScreenshotScene screenshot_scene;
    bool input_test;
    bool settings_test;
    bool arg_error;
    const char *arg_error_message;
    const char *arg_error_value;
    SimMode sim_mode;
} RunConfig;

typedef struct ArtAssets {
    SDL_Texture *title;
    SDL_Texture *office;
    SDL_Texture *camera_stage;
    SDL_Texture *camera_dining;
    SDL_Texture *camera_hall;
    SDL_Texture *camera_backstage;
    SDL_Texture *camera_vent;
    SDL_Texture *characters;
} ArtAssets;

typedef struct TextureDraw {
    bool ok;
    int w;
    int h;
} TextureDraw;

static ArtAssets g_art;

static float clampf(float value, float min, float max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static void set_color(SDL_Renderer *renderer, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

static void fill_rect(SDL_Renderer *renderer, int x, int y, int w, int h, SDL_Color color)
{
    SDL_Rect rect = { x, y, w, h };
    set_color(renderer, color);
    SDL_RenderFillRect(renderer, &rect);
}

static SDL_Texture *load_bmp_texture(SDL_Renderer *renderer, const char *path, bool color_key)
{
    SDL_Surface *surface = SDL_LoadBMP(path);
    if (surface == NULL) {
        char *base = SDL_GetBasePath();
        if (base != NULL) {
            size_t len = strlen(base) + strlen(path) + 1;
            char *absolute = malloc(len);
            if (absolute != NULL) {
                snprintf(absolute, len, "%s%s", base, path);
                surface = SDL_LoadBMP(absolute);
                free(absolute);
            }
            SDL_free(base);
        }
    }
    if (surface == NULL) {
        return NULL;
    }
    if (color_key) {
        SDL_Surface *converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(surface);
        surface = converted;
        if (surface == NULL) {
            return NULL;
        }
        uint8_t *pixels = (uint8_t *)surface->pixels;
        for (int y = 0; y < surface->h; y++) {
            uint32_t *row = (uint32_t *)(void *)(pixels + (y * surface->pitch));
            for (int x = 0; x < surface->w; x++) {
                uint8_t r;
                uint8_t g;
                uint8_t b;
                uint8_t a;
                SDL_GetRGBA(row[x], surface->format, &r, &g, &b, &a);
                if (r > 235 && g < 35 && b > 235) {
                    row[x] = SDL_MapRGBA(surface->format, r, g, b, 0);
                }
            }
        }
    }
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (texture != NULL) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    }
    return texture;
}

static void load_art_assets(SDL_Renderer *renderer)
{
    g_art.title = load_bmp_texture(renderer, "assets/title.dat", false);
    g_art.office = load_bmp_texture(renderer, "assets/office.dat", false);
    g_art.camera_stage = load_bmp_texture(renderer, "assets/camera-stage.dat", false);
    g_art.camera_dining = load_bmp_texture(renderer, "assets/camera-dining.dat", false);
    g_art.camera_hall = load_bmp_texture(renderer, "assets/camera-hall.dat", false);
    g_art.camera_backstage = load_bmp_texture(renderer, "assets/camera-backstage.dat", false);
    g_art.camera_vent = load_bmp_texture(renderer, "assets/camera-vent.dat", false);
    g_art.characters = load_bmp_texture(renderer, "assets/characters.dat", true);
}

static void destroy_texture(SDL_Texture **texture)
{
    if (*texture != NULL) {
        SDL_DestroyTexture(*texture);
        *texture = NULL;
    }
}

static void unload_art_assets(void)
{
    destroy_texture(&g_art.title);
    destroy_texture(&g_art.office);
    destroy_texture(&g_art.camera_stage);
    destroy_texture(&g_art.camera_dining);
    destroy_texture(&g_art.camera_hall);
    destroy_texture(&g_art.camera_backstage);
    destroy_texture(&g_art.camera_vent);
    destroy_texture(&g_art.characters);
}

static bool draw_texture(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *src, const SDL_Rect *dst, uint8_t alpha)
{
    if (texture == NULL) {
        return false;
    }
    SDL_SetTextureAlphaMod(texture, alpha);
    return SDL_RenderCopy(renderer, texture, src, dst) == 0;
}

static void draw_rect(SDL_Renderer *renderer, int x, int y, int w, int h, SDL_Color color)
{
    SDL_Rect rect = { x, y, w, h };
    set_color(renderer, color);
    SDL_RenderDrawRect(renderer, &rect);
}

static void draw_panel(SDL_Renderer *renderer, int x, int y, int w, int h, SDL_Color fill, SDL_Color edge);
static void draw_noise(SDL_Renderer *renderer, int seed, int x, int y, int w, int h, SDL_Color color, int count);

static SDL_Color mix_color(SDL_Color a, SDL_Color b, float t)
{
    t = clampf(t, 0.0f, 1.0f);
    return (SDL_Color){
        (uint8_t)roundf((float)a.r + (((float)b.r - (float)a.r) * t)),
        (uint8_t)roundf((float)a.g + (((float)b.g - (float)a.g) * t)),
        (uint8_t)roundf((float)a.b + (((float)b.b - (float)a.b) * t)),
        (uint8_t)roundf((float)a.a + (((float)b.a - (float)a.a) * t)),
    };
}

static void draw_vertical_gradient(SDL_Renderer *renderer, int x, int y, int w, int h, SDL_Color top, SDL_Color bottom)
{
    if (h <= 0) {
        return;
    }
    for (int row = 0; row < h; row++) {
        float t = h == 1 ? 0.0f : (float)row / (float)(h - 1);
        fill_rect(renderer, x, y + row, w, 1, mix_color(top, bottom, t));
    }
}

static void draw_horizontal_gradient(SDL_Renderer *renderer, int x, int y, int w, int h, SDL_Color left, SDL_Color right)
{
    if (w <= 0) {
        return;
    }
    for (int col = 0; col < w; col++) {
        float t = w == 1 ? 0.0f : (float)col / (float)(w - 1);
        fill_rect(renderer, x + col, y, 1, h, mix_color(left, right, t));
    }
}

static void draw_line(SDL_Renderer *renderer, int x1, int y1, int x2, int y2, SDL_Color color)
{
    set_color(renderer, color);
    SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
}

static void draw_filled_ellipse(SDL_Renderer *renderer, int cx, int cy, int rx, int ry, SDL_Color color)
{
    if (rx <= 0 || ry <= 0) {
        return;
    }
    set_color(renderer, color);
    for (int y = -ry; y <= ry; y++) {
        float yy = (float)(y * y) / (float)(ry * ry);
        if (yy > 1.0f) {
            continue;
        }
        int half = (int)roundf((float)rx * sqrtf(1.0f - yy));
        SDL_RenderDrawLine(renderer, cx - half, cy + y, cx + half, cy + y);
    }
}

static void draw_ellipse_outline(SDL_Renderer *renderer, int cx, int cy, int rx, int ry, SDL_Color color)
{
    if (rx <= 0 || ry <= 0) {
        return;
    }
    set_color(renderer, color);
    for (int i = 0; i < 96; i++) {
        float a0 = ((float)i / 96.0f) * 6.2831853f;
        float a1 = ((float)(i + 1) / 96.0f) * 6.2831853f;
        int x0 = cx + (int)roundf(cosf(a0) * (float)rx);
        int y0 = cy + (int)roundf(sinf(a0) * (float)ry);
        int x1 = cx + (int)roundf(cosf(a1) * (float)rx);
        int y1 = cy + (int)roundf(sinf(a1) * (float)ry);
        SDL_RenderDrawLine(renderer, x0, y0, x1, y1);
    }
}

static void draw_soft_ellipse(SDL_Renderer *renderer, int cx, int cy, int rx, int ry, SDL_Color color)
{
    for (int i = 5; i >= 0; i--) {
        float t = (float)i / 5.0f;
        SDL_Color layer = color;
        layer.a = (uint8_t)roundf((float)color.a * (0.10f + ((1.0f - t) * 0.24f)));
        draw_filled_ellipse(renderer, cx, cy, rx + (i * 5), ry + (i * 3), layer);
    }
}

static void draw_capsule(SDL_Renderer *renderer, int x, int y, int w, int h, SDL_Color color)
{
    int r = h / 2;
    if (w < h) {
        draw_filled_ellipse(renderer, x + (w / 2), y + (h / 2), w / 2, h / 2, color);
        return;
    }
    fill_rect(renderer, x + r, y, w - (r * 2), h, color);
    draw_filled_ellipse(renderer, x + r, y + r, r, r, color);
    draw_filled_ellipse(renderer, x + w - r, y + r, r, r, color);
}

static void draw_hanging_cable(SDL_Renderer *renderer, int x, int y, int length, SDL_Color color)
{
    for (int i = 0; i < length; i += 4) {
        int wobble = (int)roundf(sinf((float)i * 0.13f) * 5.0f);
        draw_line(renderer, x + wobble, y + i, x + wobble + 1, y + i + 3, color);
    }
}

static void draw_security_fan(SDL_Renderer *renderer, int cx, int cy, int scale)
{
    SDL_Color shell = { 51, 58, 58, 220 };
    SDL_Color wire = { 120, 135, 130, 165 };
    draw_filled_ellipse(renderer, cx, cy, 33 * scale, 25 * scale, (SDL_Color){ 0, 0, 0, 120 });
    draw_ellipse_outline(renderer, cx, cy, 29 * scale, 21 * scale, shell);
    draw_ellipse_outline(renderer, cx, cy, 23 * scale, 16 * scale, wire);
    for (int i = 0; i < 8; i++) {
        float a = ((float)i / 8.0f) * 6.2831853f;
        draw_line(renderer, cx, cy, cx + (int)roundf(cosf(a) * 28.0f * (float)scale),
            cy + (int)roundf(sinf(a) * 20.0f * (float)scale), wire);
    }
    draw_filled_ellipse(renderer, cx, cy, 5 * scale, 5 * scale, (SDL_Color){ 13, 15, 16, 255 });
    draw_capsule(renderer, cx - (5 * scale), cy + (23 * scale), 10 * scale, 35 * scale, (SDL_Color){ 29, 33, 34, 255 });
    fill_rect(renderer, cx - (28 * scale), cy + (55 * scale), 56 * scale, 8 * scale, (SDL_Color){ 28, 31, 31, 255 });
}

static void draw_security_monitor(SDL_Renderer *renderer, int x, int y, int w, int h, SDL_Color glow)
{
    draw_panel(renderer, x, y, w, h, (SDL_Color){ 12, 16, 17, 245 }, (SDL_Color){ 55, 67, 67, 255 });
    fill_rect(renderer, x + 8, y + 8, w - 16, h - 18, (SDL_Color){ 4, 8, 8, 255 });
    draw_soft_ellipse(renderer, x + (w / 2), y + (h / 2), w / 3, h / 3, (SDL_Color){ glow.r, glow.g, glow.b, 42 });
    for (int i = 0; i < 5; i++) {
        int sy = y + 14 + (i * ((h - 28) / 5));
        draw_line(renderer, x + 12, sy, x + w - 14, sy + ((i % 2) * 4), (SDL_Color){ glow.r, glow.g, glow.b, 48 });
    }
    fill_rect(renderer, x + 12, y + h - 8, 9, 3, glow);
    fill_rect(renderer, x + w - 24, y + h - 8, 12, 3, (SDL_Color){ 92, 98, 96, 255 });
}

static void draw_wall_grime(SDL_Renderer *renderer, int seed, int x, int y, int w, int h)
{
    draw_noise(renderer, seed, x, y, w, h, (SDL_Color){ 180, 165, 112, 28 }, 90);
    draw_noise(renderer, seed + 13, x, y, w, h, (SDL_Color){ 0, 0, 0, 45 }, 120);
    for (int i = 0; i < 8; i++) {
        int px = x + 14 + ((i * 47 + seed) % (w - 30));
        draw_hanging_cable(renderer, px, y + 8 + ((i * 19) % 40), 42 + ((i * 11) % 70), (SDL_Color){ 21, 24, 24, 170 });
    }
}

static void draw_panel(SDL_Renderer *renderer, int x, int y, int w, int h, SDL_Color fill, SDL_Color edge)
{
    fill_rect(renderer, x + 4, y + 5, w, h, (SDL_Color){ 0, 0, 0, 110 });
    fill_rect(renderer, x, y, w, h, fill);
    draw_rect(renderer, x, y, w, h, edge);
    draw_line(renderer, x + 2, y + 2, x + w - 3, y + 2, (SDL_Color){ edge.r, edge.g, edge.b, 85 });
}

static void draw_scanlines(SDL_Renderer *renderer, int y_offset, SDL_Color color)
{
    for (int y = y_offset % 6; y < WINDOW_H; y += 6) {
        fill_rect(renderer, 0, y, WINDOW_W, 1, color);
    }
}

static void draw_noise(SDL_Renderer *renderer, int seed, int x, int y, int w, int h, SDL_Color color, int count)
{
    uint32_t state = (uint32_t)seed * 1103515245u + 12345u;
    for (int i = 0; i < count; i++) {
        state = (state * 1664525u) + 1013904223u;
        int px = x + (int)(state % (uint32_t)w);
        state = (state * 1664525u) + 1013904223u;
        int py = y + (int)(state % (uint32_t)h);
        state = (state * 1664525u) + 1013904223u;
        int len = 1 + (int)(state % 9u);
        fill_rect(renderer, px, py, len, 1, color);
    }
}

static void draw_vignette(SDL_Renderer *renderer)
{
    for (int i = 0; i < 9; i++) {
        uint8_t a = (uint8_t)(22 + (i * 9));
        draw_rect(renderer, i * 5, i * 4, WINDOW_W - (i * 10), WINDOW_H - (i * 8), (SDL_Color){ 0, 0, 0, a });
    }
    fill_rect(renderer, 0, 0, WINDOW_W, 34, (SDL_Color){ 0, 0, 0, 70 });
    fill_rect(renderer, 0, WINDOW_H - 46, WINDOW_W, 46, (SDL_Color){ 0, 0, 0, 80 });
}

static void draw_bar(SDL_Renderer *renderer, int x, int y, int w, int h, float frac, SDL_Color fill)
{
    fill_rect(renderer, x, y, w, h, (SDL_Color){ 18, 19, 20, 255 });
    draw_rect(renderer, x, y, w, h, (SDL_Color){ 95, 101, 104, 255 });
    int filled = (int)roundf((float)(w - 4) * clampf(frac, 0.0f, 1.0f));
    fill_rect(renderer, x + 2, y + 2, filled, h - 4, fill);
}

static void draw_segment(SDL_Renderer *renderer, int x, int y, int len, int thick, bool horizontal, SDL_Color color)
{
    if (horizontal) {
        fill_rect(renderer, x, y, len, thick, color);
    } else {
        fill_rect(renderer, x, y, thick, len, color);
    }
}

static void draw_digit(SDL_Renderer *renderer, int digit, int x, int y, int scale, SDL_Color color)
{
    static const bool segments[10][7] = {
        { true, true, true, true, true, true, false },
        { false, true, true, false, false, false, false },
        { true, true, false, true, true, false, true },
        { true, true, true, true, false, false, true },
        { false, true, true, false, false, true, true },
        { true, false, true, true, false, true, true },
        { true, false, true, true, true, true, true },
        { true, true, true, false, false, false, false },
        { true, true, true, true, true, true, true },
        { true, true, true, true, false, true, true },
    };
    int len = 10 * scale;
    int thick = 2 * scale;
    int h = 11 * scale;

    if (digit < 0 || digit > 9) {
        return;
    }
    if (segments[digit][0]) draw_segment(renderer, x + thick, y, len, thick, true, color);
    if (segments[digit][1]) draw_segment(renderer, x + len + thick, y + thick, h, thick, false, color);
    if (segments[digit][2]) draw_segment(renderer, x + len + thick, y + h + (2 * thick), h, thick, false, color);
    if (segments[digit][3]) draw_segment(renderer, x + thick, y + (2 * h) + (2 * thick), len, thick, true, color);
    if (segments[digit][4]) draw_segment(renderer, x, y + h + (2 * thick), h, thick, false, color);
    if (segments[digit][5]) draw_segment(renderer, x, y + thick, h, thick, false, color);
    if (segments[digit][6]) draw_segment(renderer, x + thick, y + h + thick, len, thick, true, color);
}

static void draw_number(SDL_Renderer *renderer, int value, int x, int y, int scale, SDL_Color color)
{
    if (value < 0) {
        value = 0;
    }

    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    for (int i = 0; buf[i] != '\0'; i++) {
        draw_digit(renderer, buf[i] - '0', x + (i * 16 * scale), y, scale, color);
    }
}

static void draw_word_power(SDL_Renderer *renderer, int x, int y, SDL_Color color)
{
    draw_rect(renderer, x, y, 9, 15, color);
    fill_rect(renderer, x + 2, y + 2, 7, 4, color);
    draw_rect(renderer, x + 16, y, 12, 15, color);
    draw_rect(renderer, x + 33, y, 14, 15, color);
    fill_rect(renderer, x + 37, y + 4, 6, 11, (SDL_Color){ 9, 10, 11, 255 });
    draw_rect(renderer, x + 52, y, 12, 15, color);
    fill_rect(renderer, x + 55, y + 6, 7, 3, color);
    draw_rect(renderer, x + 70, y, 11, 15, color);
    fill_rect(renderer, x + 78, y + 8, 5, 7, color);
}

static uint8_t glyph_bits(char ch, int row)
{
    static const uint8_t digits[10][7] = {
        { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e },
        { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },
        { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f },
        { 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e },
        { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },
        { 0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e },
        { 0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e },
        { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
        { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e },
        { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e },
    };
    static const uint8_t letters[26][7] = {
        { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
        { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e },
        { 0x0f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0f },
        { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e },
        { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f },
        { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 },
        { 0x0f, 0x10, 0x10, 0x13, 0x11, 0x11, 0x0f },
        { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
        { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e },
        { 0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e },
        { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
        { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f },
        { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 },
        { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
        { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
        { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 },
        { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d },
        { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 },
        { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e },
        { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
        { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
        { 0x11, 0x11, 0x11, 0x0a, 0x0a, 0x04, 0x04 },
        { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11 },
        { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 },
        { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 },
        { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f },
    };

    if (row < 0 || row >= 7) {
        return 0;
    }
    if (ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 'A');
    }
    if (ch >= 'A' && ch <= 'Z') {
        return letters[ch - 'A'][row];
    }
    if (ch >= '0' && ch <= '9') {
        return digits[ch - '0'][row];
    }

    switch (ch) {
    case ':': return row == 2 || row == 4 ? 0x04 : 0x00;
    case '-': return row == 3 ? 0x0e : 0x00;
    case '/': return (uint8_t)(0x01 << (6 - row > 4 ? 4 : 6 - row));
    case '.': return row == 6 ? 0x04 : 0x00;
    case '%': return row < 2 ? 0x11 : (row == 3 ? 0x04 : (row > 4 ? 0x11 : 0x02));
    case '[': return row == 0 || row == 6 ? 0x0e : 0x08;
    case ']': return row == 0 || row == 6 ? 0x0e : 0x02;
    default: return 0x00;
    }
}

static void draw_text(SDL_Renderer *renderer, const char *text, int x, int y, int scale, SDL_Color color)
{
    int cursor = x;
    for (const char *at = text; *at != '\0'; at++) {
        if (*at == '\n') {
            y += 9 * scale;
            cursor = x;
            continue;
        }
        if (*at != ' ') {
            for (int row = 0; row < 7; row++) {
                uint8_t bits = glyph_bits(*at, row);
                for (int col = 0; col < 5; col++) {
                    if ((bits & (uint8_t)(1u << (4 - col))) != 0) {
                        fill_rect(renderer, cursor + (col * scale), y + (row * scale), scale, scale, color);
                    }
                }
            }
        }
        cursor += 6 * scale;
    }
}

static bool scene_has_threat(const Game *game, Scene scene)
{
    for (int i = 0; i < THREAT_COUNT; i++) {
        if (game->threats[i].active && game->threats[i].scene == scene) {
            return true;
        }
    }
    return false;
}

static void draw_camera_map(SDL_Renderer *renderer, const Game *game)
{
    static const int node_x[CAMERA_COUNT] = { 802, 842, 790, 888, 828, 874 };
    static const int node_y[CAMERA_COUNT] = { 330, 375, 420, 420, 465, 465 };

    draw_text(renderer, "MAP", 800, 292, 2, (SDL_Color){ 134, 193, 173, 255 });
    draw_rect(renderer, 770, 315, 150, 178, (SDL_Color){ 58, 85, 80, 255 });
    draw_rect(renderer, 790, 330, 98, 135, (SDL_Color){ 35, 55, 52, 255 });
    fill_rect(renderer, 836, 458, 12, 22, (SDL_Color){ 35, 55, 52, 255 });

    for (int i = 0; i < CAMERA_COUNT; i++) {
        Scene scene = camera_scene(i);
        bool selected = i == game->selected_camera;
        bool occupied = scene_has_threat(game, scene);
        SDL_Color color = selected ? (SDL_Color){ 86, 185, 145, 255 } :
            occupied ? (SDL_Color){ 211, 70, 54, 255 } : (SDL_Color){ 84, 99, 96, 255 };
        fill_rect(renderer, node_x[i], node_y[i], 28, 22, color);
        draw_number(renderer, i + 1, node_x[i] + 8, node_y[i] + 5, 1,
            selected ? (SDL_Color){ 7, 10, 10, 255 } : (SDL_Color){ 220, 230, 214, 255 });
    }
}

static SDL_Color threat_accent_color(int threat_index)
{
    switch (threat_index) {
    case 0: return (SDL_Color){ 190, 91, 63, 255 };
    case 1: return (SDL_Color){ 82, 148, 224, 255 };
    case 2: return (SDL_Color){ 207, 172, 74, 255 };
    case 3: return (SDL_Color){ 158, 100, 212, 255 };
    default: return (SDL_Color){ 212, 55, 48, 255 };
    }
}

static SDL_Rect threat_atlas_rect(int threat_index)
{
    switch (threat_index) {
    case 0: return (SDL_Rect){ 0, 0, 520, 916 };
    case 1: return (SDL_Rect){ 548, 0, 380, 916 };
    case 2: return (SDL_Rect){ 936, 476, 420, 440 };
    case 3: return (SDL_Rect){ 1360, 0, 357, 916 };
    default: return (SDL_Rect){ 0, 0, 520, 916 };
    }
}

static bool draw_art_threat_sprite(SDL_Renderer *renderer, int threat_index, int x, int y, int height, uint8_t alpha)
{
    if (g_art.characters == NULL || height <= 0) {
        return false;
    }

    SDL_Rect src = threat_atlas_rect(threat_index);
    int width = (int)roundf(((float)src.w / (float)src.h) * (float)height);
    SDL_Rect dst = { x, y, width, height };
    draw_soft_ellipse(renderer, x + (width / 2), y + height - 12, width / 3, height / 10,
        (SDL_Color){ 0, 0, 0, (uint8_t)(alpha / 3) });
    return draw_texture(renderer, g_art.characters, &src, &dst, alpha);
}

static void draw_threat_sprite(SDL_Renderer *renderer, int threat_index, int x, int y, int scale, bool lit)
{
    int art_height = lit ? 170 * scale : 155 * scale;
    if (draw_art_threat_sprite(renderer, threat_index, x, y, art_height, lit ? 245 : 205)) {
        SDL_Color accent = threat_accent_color(threat_index);
        draw_soft_ellipse(renderer, x + (38 * scale), y + (62 * scale), 13 * scale, 9 * scale,
            (SDL_Color){ accent.r, accent.g, accent.b, lit ? 95 : 52 });
        return;
    }

    SDL_Color accent = threat_accent_color(threat_index);
    SDL_Color body = lit ? (SDL_Color){ 47, 53, 52, 255 } : (SDL_Color){ 16, 19, 20, 255 };
    SDL_Color face = lit ? (SDL_Color){ 75, 83, 80, 255 } : (SDL_Color){ 29, 34, 34, 255 };
    SDL_Color rim = mix_color(accent, (SDL_Color){ 255, 245, 220, 255 }, lit ? 0.20f : 0.08f);
    SDL_Color eye = lit ? (SDL_Color){ 245, 54, 45, 255 } : (SDL_Color){ 166, 28, 26, 255 };
    int s = scale;
    int cx = x + (38 * s);

    draw_soft_ellipse(renderer, cx, y + (73 * s), 28 * s, 54 * s, (SDL_Color){ accent.r, accent.g, accent.b, lit ? 92 : 42 });
    draw_filled_ellipse(renderer, cx, y + (94 * s), 28 * s, 46 * s, body);
    draw_filled_ellipse(renderer, cx, y + (70 * s), 22 * s, 24 * s, face);
    draw_ellipse_outline(renderer, cx, y + (70 * s), 22 * s, 24 * s, (SDL_Color){ rim.r, rim.g, rim.b, 170 });
    draw_capsule(renderer, x + (10 * s), y + (83 * s), 12 * s, 48 * s, mix_color(body, accent, 0.18f));
    draw_capsule(renderer, x + (54 * s), y + (83 * s), 12 * s, 48 * s, mix_color(body, accent, 0.18f));
    draw_capsule(renderer, x + (22 * s), y + (127 * s), 10 * s, 35 * s, (SDL_Color){ 17, 19, 20, 255 });
    draw_capsule(renderer, x + (44 * s), y + (127 * s), 10 * s, 35 * s, (SDL_Color){ 17, 19, 20, 255 });

    if (threat_index == 0) {
        draw_filled_ellipse(renderer, x + (20 * s), y + (36 * s), 10 * s, 18 * s, accent);
        draw_filled_ellipse(renderer, x + (56 * s), y + (36 * s), 10 * s, 18 * s, accent);
        draw_ellipse_outline(renderer, x + (20 * s), y + (36 * s), 10 * s, 18 * s, rim);
        draw_ellipse_outline(renderer, x + (56 * s), y + (36 * s), 10 * s, 18 * s, rim);
        draw_soft_ellipse(renderer, x + (29 * s), y + (66 * s), 8 * s, 6 * s, (SDL_Color){ eye.r, eye.g, eye.b, 180 });
        draw_soft_ellipse(renderer, x + (47 * s), y + (66 * s), 8 * s, 6 * s, (SDL_Color){ eye.r, eye.g, eye.b, 180 });
        draw_filled_ellipse(renderer, x + (29 * s), y + (66 * s), 4 * s, 4 * s, eye);
        draw_filled_ellipse(renderer, x + (47 * s), y + (66 * s), 4 * s, 4 * s, eye);
        draw_capsule(renderer, x + (30 * s), y + (80 * s), 18 * s, 5 * s, (SDL_Color){ 4, 4, 4, 255 });
    } else if (threat_index == 1) {
        draw_capsule(renderer, x + (18 * s), y + (12 * s), 11 * s, 48 * s, accent);
        draw_capsule(renderer, x + (48 * s), y + (12 * s), 11 * s, 48 * s, accent);
        draw_line(renderer, x + (23 * s), y + (12 * s), x + (23 * s), y + (58 * s), rim);
        draw_line(renderer, x + (53 * s), y + (12 * s), x + (53 * s), y + (58 * s), rim);
        draw_filled_ellipse(renderer, x + (30 * s), y + (66 * s), 4 * s, 8 * s, eye);
        draw_filled_ellipse(renderer, x + (46 * s), y + (66 * s), 4 * s, 8 * s, eye);
        draw_capsule(renderer, x + (30 * s), y + (82 * s), 18 * s, 4 * s, (SDL_Color){ 5, 5, 6, 255 });
    } else if (threat_index == 2) {
        draw_capsule(renderer, x + (27 * s), y + (24 * s), 22 * s, 15 * s, accent);
        draw_filled_ellipse(renderer, x + (17 * s), y + (68 * s), 7 * s, 7 * s, accent);
        draw_filled_ellipse(renderer, x + (59 * s), y + (68 * s), 7 * s, 7 * s, accent);
        draw_filled_ellipse(renderer, x + (30 * s), y + (66 * s), 3 * s, 4 * s, eye);
        draw_filled_ellipse(renderer, x + (46 * s), y + (66 * s), 3 * s, 4 * s, eye);
        for (int i = 0; i < 4; i++) {
            draw_line(renderer, x + ((17 + (i * 12)) * s), y + (112 * s), x + ((3 + (i * 17)) * s), y + (154 * s), accent);
            draw_line(renderer, x + ((18 + (i * 12)) * s), y + (112 * s), x + ((4 + (i * 17)) * s), y + (154 * s), (SDL_Color){ 70, 63, 42, 170 });
        }
    } else {
        draw_capsule(renderer, x + (15 * s), y + (46 * s), 48 * s, 22 * s, accent);
        draw_line(renderer, x + (8 * s), y + (63 * s), x + (68 * s), y + (63 * s), (SDL_Color){ 218, 196, 245, 95 });
        draw_filled_ellipse(renderer, x + (29 * s), y + (66 * s), 4 * s, 4 * s, eye);
        draw_filled_ellipse(renderer, x + (47 * s), y + (66 * s), 4 * s, 4 * s, eye);
        draw_capsule(renderer, x + (30 * s), y + (82 * s), 18 * s, 4 * s, (SDL_Color){ 5, 5, 6, 255 });
    }

    draw_capsule(renderer, x + (21 * s), y + (99 * s), 34 * s, 6 * s, mix_color(accent, (SDL_Color){ 255, 255, 255, 255 }, 0.18f));
    draw_capsule(renderer, x + (17 * s), y + (157 * s), 18 * s, 7 * s, (SDL_Color){ 5, 6, 7, 255 });
    draw_capsule(renderer, x + (41 * s), y + (157 * s), 18 * s, 7 * s, (SDL_Color){ 5, 6, 7, 255 });
}

static void apply_window_settings(SDL_Window *window, const SettingsData *settings)
{
    if (window == NULL) {
        return;
    }
    if (settings->fullscreen) {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
        SDL_SetWindowFullscreen(window, 0);
        SDL_SetWindowSize(window, WINDOW_W * settings->window_scale, WINDOW_H * settings->window_scale);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
}

static void fill_surface_rect(SDL_Surface *surface, int x, int y, int w, int h, SDL_Color color)
{
    SDL_Rect rect = { x, y, w, h };
    uint32_t pixel = SDL_MapRGBA(surface->format, color.r, color.g, color.b, color.a);
    SDL_FillRect(surface, &rect, pixel);
}

static void apply_window_icon(SDL_Window *window)
{
    if (window == NULL) {
        return;
    }

    SDL_Surface *icon = SDL_CreateRGBSurfaceWithFormat(0, 32, 32, 32, SDL_PIXELFORMAT_RGBA32);
    if (icon == NULL) {
        return;
    }

    fill_surface_rect(icon, 0, 0, 32, 32, (SDL_Color){ 7, 9, 10, 255 });
    fill_surface_rect(icon, 3, 4, 26, 24, (SDL_Color){ 24, 29, 30, 255 });
    fill_surface_rect(icon, 5, 6, 22, 20, (SDL_Color){ 52, 58, 58, 255 });
    fill_surface_rect(icon, 8, 9, 6, 6, (SDL_Color){ 217, 70, 55, 255 });
    fill_surface_rect(icon, 18, 9, 6, 6, (SDL_Color){ 217, 70, 55, 255 });
    fill_surface_rect(icon, 9, 20, 14, 3, (SDL_Color){ 219, 226, 211, 255 });
    fill_surface_rect(icon, 11, 25, 10, 2, (SDL_Color){ 211, 175, 92, 255 });
    fill_surface_rect(icon, 5, 2, 3, 4, (SDL_Color){ 115, 128, 119, 255 });
    fill_surface_rect(icon, 24, 2, 3, 4, (SDL_Color){ 115, 128, 119, 255 });
    SDL_SetWindowIcon(window, icon);
    SDL_FreeSurface(icon);
}

static bool hit_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void window_size_to_logical(int window_w, int window_h, int window_x, int window_y, int *logical_x, int *logical_y)
{
    if (window_w <= 0 || window_h <= 0) {
        *logical_x = window_x;
        *logical_y = window_y;
        return;
    }

    float scale_x = (float)window_w / (float)WINDOW_W;
    float scale_y = (float)window_h / (float)WINDOW_H;
    float scale = scale_x < scale_y ? scale_x : scale_y;
    if (scale <= 0.0f) {
        *logical_x = window_x;
        *logical_y = window_y;
        return;
    }

    int viewport_w = (int)roundf((float)WINDOW_W * scale);
    int viewport_h = (int)roundf((float)WINDOW_H * scale);
    int viewport_x = (window_w - viewport_w) / 2;
    int viewport_y = (window_h - viewport_h) / 2;
    *logical_x = (int)floorf(((float)window_x - (float)viewport_x) / scale);
    *logical_y = (int)floorf(((float)window_y - (float)viewport_y) / scale);
}

static void window_to_logical(SDL_Window *window, int window_x, int window_y, int *logical_x, int *logical_y)
{
    int window_w = WINDOW_W;
    int window_h = WINDOW_H;
    SDL_GetWindowSize(window, &window_w, &window_h);
    window_size_to_logical(window_w, window_h, window_x, window_y, logical_x, logical_y);
}

static void select_camera(Game *game, Audio *audio, int camera)
{
    if (camera < 0 || camera >= CAMERA_COUNT) {
        return;
    }
    game->selected_camera = camera;
    game->monitor = true;
    game->static_timer = 0.0f;
    audio_play(audio, SOUND_CAMERA);
}

static void play_audio_lure(Game *game, Audio *audio)
{
    if (trigger_audio_lure(game)) {
        audio_play(audio, SOUND_LURE);
    }
}

static SDL_GameController *open_first_controller(void)
{
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            SDL_GameController *controller = SDL_GameControllerOpen(i);
            if (controller != NULL) {
                const char *name = SDL_GameControllerName(controller);
                printf("controller=%s\n", name != NULL ? name : "unknown");
                return controller;
            }
        }
    }
    return NULL;
}

static void close_controller(SDL_GameController **controller)
{
    if (*controller != NULL) {
        SDL_GameControllerClose(*controller);
        *controller = NULL;
    }
}

static void maybe_close_removed_controller(SDL_GameController **controller, SDL_JoystickID instance_id)
{
    if (*controller == NULL) {
        return;
    }

    SDL_Joystick *joystick = SDL_GameControllerGetJoystick(*controller);
    if (joystick == NULL || SDL_JoystickInstanceID(joystick) == instance_id) {
        close_controller(controller);
    }
}

static void select_relative_camera(Game *game, Audio *audio, int delta)
{
    int camera = game->selected_camera + delta;
    if (camera < 0) {
        camera = CAMERA_COUNT - 1;
    } else if (camera >= CAMERA_COUNT) {
        camera = 0;
    }
    select_camera(game, audio, camera);
}

static void select_relative_night(Game *game, Audio *audio, int delta)
{
    int night = game->night + delta;
    if (night < MIN_NIGHT) {
        night = MIN_NIGHT;
    } else if (night > game->unlocked_night) {
        night = game->unlocked_night;
    }

    if (night != game->night) {
        game->night = night;
        audio_play(audio, SOUND_LIGHT);
    }
}

static void toggle_custom_night(Game *game, Audio *audio)
{
    if (game_is_custom_night(game)) {
        game->custom_night = false;
    } else {
        game->custom_night = true;
        if (!game->custom_ai_configured) {
            for (int i = 0; i < THREAT_COUNT; i++) {
                set_custom_ai(game, i, 5);
            }
        }
    }
    audio_play(audio, SOUND_CAMERA);
}

static void select_relative_custom_threat(Game *game, Audio *audio, int delta)
{
    int threat = game->selected_custom_threat + delta;
    if (threat < 0) {
        threat = THREAT_COUNT - 1;
    } else if (threat >= THREAT_COUNT) {
        threat = 0;
    }
    if (threat != game->selected_custom_threat) {
        game->selected_custom_threat = threat;
        audio_play(audio, SOUND_LIGHT);
    }
}

static void adjust_custom_ai(Game *game, Audio *audio, int delta)
{
    int threat = game->selected_custom_threat;
    if (threat < 0 || threat >= THREAT_COUNT) {
        threat = 0;
        game->selected_custom_threat = 0;
    }

    int previous = game->custom_ai[threat];
    set_custom_ai(game, threat, previous + delta);
    if (game->custom_ai[threat] != previous) {
        audio_play(audio, SOUND_LIGHT);
    }
}

static void start_next_unlocked_night(Game *game, Audio *audio)
{
    if (game_is_custom_night(game)) {
        start_night(game);
        audio_play(audio, SOUND_START);
        return;
    }

    int next_night = game->night + 1;
    if (next_night > game->unlocked_night) {
        next_night = game->unlocked_night;
    }
    if (next_night > MAX_NIGHT) {
        next_night = MAX_NIGHT;
    }

    game->night = next_night;
    start_night(game);
    audio_play(audio, SOUND_START);
}

static void return_to_title(Game *game, Audio *audio)
{
    int night = game->night;
    int unlocked_night = game->unlocked_night;
    int best_night = game->best_night;
    float night_seconds = game->night_seconds;
    int selected_custom_threat = game->selected_custom_threat;
    bool custom_night = game->custom_night;
    bool custom_ai_configured = game->custom_ai_configured;
    bool story_cleared = game->story_cleared;
    bool custom_challenge_cleared = game->custom_challenge_cleared;
    int best_power[MAX_NIGHT];
    for (int i = 0; i < MAX_NIGHT; i++) {
        best_power[i] = game->best_power[i];
    }
    int custom_ai[THREAT_COUNT];
    for (int i = 0; i < THREAT_COUNT; i++) {
        custom_ai[i] = game->custom_ai[i];
    }

    init_game(game);
    game->night_seconds = night_seconds;
    game->unlocked_night = unlocked_night;
    game->best_night = best_night;
    game->night = night;
    game->selected_custom_threat = selected_custom_threat;
    game->custom_night = custom_night;
    game->custom_ai_configured = custom_ai_configured;
    game->story_cleared = story_cleared;
    game->custom_challenge_cleared = custom_challenge_cleared;
    for (int i = 0; i < MAX_NIGHT; i++) {
        game->best_power[i] = best_power[i];
    }
    for (int i = 0; i < THREAT_COUNT; i++) {
        game->custom_ai[i] = custom_ai[i];
    }
    audio_play(audio, SOUND_CAMERA);
}

static bool extras_unlocked(const Game *game)
{
    return game->story_cleared || game->custom_challenge_cleared;
}

static void handle_controller_button(Game *game, Audio *audio, bool *show_help, SDL_GameControllerButton button)
{
    if (button == SDL_CONTROLLER_BUTTON_GUIDE || button == SDL_CONTROLLER_BUTTON_BACK) {
        *show_help = !*show_help;
        audio_play(audio, SOUND_LIGHT);
        return;
    }

    if (*show_help) {
        if (button == SDL_CONTROLLER_BUTTON_A || button == SDL_CONTROLLER_BUTTON_B ||
            button == SDL_CONTROLLER_BUTTON_START) {
            *show_help = false;
            audio_play(audio, SOUND_LIGHT);
        }
        return;
    }

    if (game->mode == MODE_TITLE) {
        if (button == SDL_CONTROLLER_BUTTON_A || button == SDL_CONTROLLER_BUTTON_START) {
            start_night(game);
            audio_play(audio, SOUND_START);
        } else if (extras_unlocked(game) && button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
            game->mode = MODE_EXTRAS;
            audio_play(audio, SOUND_CAMERA);
        } else if (button == SDL_CONTROLLER_BUTTON_X) {
            toggle_custom_night(game, audio);
        } else if (game_is_custom_night(game) && button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
            select_relative_custom_threat(game, audio, -1);
        } else if (game_is_custom_night(game) && button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
            select_relative_custom_threat(game, audio, 1);
        } else if (game_is_custom_night(game) && button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
            adjust_custom_ai(game, audio, -1);
        } else if (game_is_custom_night(game) && button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            adjust_custom_ai(game, audio, 1);
        } else if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
            select_relative_night(game, audio, -1);
        } else if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            select_relative_night(game, audio, 1);
        } else if (button == SDL_CONTROLLER_BUTTON_B) {
            game->running = false;
        }
        return;
    }

    if (game->mode == MODE_EXTRAS) {
        if (button == SDL_CONTROLLER_BUTTON_B || button == SDL_CONTROLLER_BUTTON_A ||
            button == SDL_CONTROLLER_BUTTON_START) {
            game->mode = MODE_TITLE;
            audio_play(audio, SOUND_CAMERA);
        }
        return;
    }

    if (game->mode == MODE_PAUSED) {
        if (button == SDL_CONTROLLER_BUTTON_A || button == SDL_CONTROLLER_BUTTON_START) {
            game->mode = MODE_PLAYING;
            audio_play(audio, SOUND_START);
        } else if (button == SDL_CONTROLLER_BUTTON_Y) {
            start_night(game);
            audio_play(audio, SOUND_START);
        } else if (button == SDL_CONTROLLER_BUTTON_B) {
            return_to_title(game, audio);
        }
        return;
    }

    if (game->mode == MODE_WIN) {
        if (button == SDL_CONTROLLER_BUTTON_A || button == SDL_CONTROLLER_BUTTON_START) {
            start_next_unlocked_night(game, audio);
        } else if (button == SDL_CONTROLLER_BUTTON_Y) {
            start_night(game);
            audio_play(audio, SOUND_START);
        } else if (button == SDL_CONTROLLER_BUTTON_B) {
            game->running = false;
        }
        return;
    }

    if (game->mode != MODE_PLAYING) {
        if (button == SDL_CONTROLLER_BUTTON_A || button == SDL_CONTROLLER_BUTTON_Y) {
            start_night(game);
            audio_play(audio, SOUND_START);
        } else if (button == SDL_CONTROLLER_BUTTON_B || button == SDL_CONTROLLER_BUTTON_START) {
            game->running = false;
        }
        return;
    }

    if (game->power_out) {
        if (button == SDL_CONTROLLER_BUTTON_START || button == SDL_CONTROLLER_BUTTON_B) {
            game->mode = MODE_PAUSED;
        }
        return;
    }

    switch (button) {
    case SDL_CONTROLLER_BUTTON_A:
        game->monitor = !game->monitor;
        audio_play(audio, SOUND_CAMERA);
        break;
    case SDL_CONTROLLER_BUTTON_B:
        if (game->monitor) {
            game->monitor = false;
            audio_play(audio, SOUND_CAMERA);
        } else {
            game->mode = MODE_PAUSED;
        }
        break;
    case SDL_CONTROLLER_BUTTON_X:
        game->left_door = !game->left_door;
        audio_play(audio, SOUND_DOOR);
        break;
    case SDL_CONTROLLER_BUTTON_Y:
        game->right_door = !game->right_door;
        audio_play(audio, SOUND_DOOR);
        break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        game->left_light = !game->left_light;
        audio_play(audio, SOUND_LIGHT);
        break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        game->right_light = !game->right_light;
        audio_play(audio, SOUND_LIGHT);
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        select_relative_camera(game, audio, -1);
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        select_relative_camera(game, audio, 1);
        break;
    case SDL_CONTROLLER_BUTTON_START:
        game->mode = MODE_PAUSED;
        break;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:
        if (game->monitor) {
            play_audio_lure(game, audio);
        } else if (game_phone_call_active(game)) {
            game->call_muted = true;
            audio_play(audio, SOUND_LIGHT);
        }
        break;
    default:
        break;
    }
}

static void handle_key(Game *game, Audio *audio, SDL_Window *window, SettingsData *settings, bool *show_help, SDL_Keycode key)
{
    if (key == SDLK_F1) {
        *show_help = !*show_help;
        audio_play(audio, SOUND_LIGHT);
        return;
    }
    if (key == SDLK_F11) {
        settings->fullscreen = !settings->fullscreen;
        apply_window_settings(window, settings);
        audio_play(audio, SOUND_CAMERA);
        return;
    }
    if (key == SDLK_m) {
        settings->muted = !settings->muted;
        audio_set_muted(audio, settings->muted);
        if (!settings->muted) {
            audio_play(audio, SOUND_LIGHT);
        }
        return;
    }
    if (!settings->fullscreen && (key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS)) {
        if (settings->window_scale < 4) {
            settings->window_scale++;
            apply_window_settings(window, settings);
            audio_play(audio, SOUND_LIGHT);
        }
        return;
    }
    if (!settings->fullscreen && (key == SDLK_MINUS || key == SDLK_KP_MINUS)) {
        if (settings->window_scale > 1) {
            settings->window_scale--;
            apply_window_settings(window, settings);
            audio_play(audio, SOUND_LIGHT);
        }
        return;
    }

    if (*show_help) {
        *show_help = false;
        audio_play(audio, SOUND_LIGHT);
        return;
    }

    if (key == SDLK_ESCAPE) {
        if (game->mode == MODE_PLAYING) {
            game->mode = MODE_PAUSED;
        } else if (game->mode == MODE_PAUSED) {
            game->mode = MODE_PLAYING;
        } else if (game->mode == MODE_EXTRAS) {
            game->mode = MODE_TITLE;
        } else {
            game->running = false;
        }
        return;
    }

    if (game->mode == MODE_TITLE) {
        if (key == SDLK_RETURN || key == SDLK_SPACE) {
            start_night(game);
            audio_play(audio, SOUND_START);
        } else if (extras_unlocked(game) && key == SDLK_e) {
            game->mode = MODE_EXTRAS;
            audio_play(audio, SOUND_CAMERA);
        } else if (key == SDLK_c) {
            toggle_custom_night(game, audio);
        } else if (game_is_custom_night(game) && (key == SDLK_UP || key == SDLK_TAB)) {
            select_relative_custom_threat(game, audio, key == SDLK_UP ? -1 : 1);
        } else if (game_is_custom_night(game) && key == SDLK_DOWN) {
            select_relative_custom_threat(game, audio, 1);
        } else if (game_is_custom_night(game) && (key == SDLK_LEFT || key == SDLK_a)) {
            adjust_custom_ai(game, audio, -1);
        } else if (game_is_custom_night(game) && (key == SDLK_RIGHT || key == SDLK_d)) {
            adjust_custom_ai(game, audio, 1);
        } else if (game_is_custom_night(game) && key >= SDLK_1 && key <= SDLK_1 + THREAT_COUNT - 1) {
            game->selected_custom_threat = (int)(key - SDLK_1);
            audio_play(audio, SOUND_LIGHT);
        } else if (key == SDLK_LEFT || key == SDLK_a) {
            if (game->night > MIN_NIGHT) {
                game->night--;
                audio_play(audio, SOUND_LIGHT);
            }
        } else if (key == SDLK_RIGHT || key == SDLK_d) {
            if (game->night < game->unlocked_night) {
                game->night++;
                audio_play(audio, SOUND_LIGHT);
            }
        } else if (key >= SDLK_1 && key <= SDLK_6) {
            int night = (int)(key - SDLK_1) + 1;
            if (night <= game->unlocked_night) {
                game->night = night;
                audio_play(audio, SOUND_LIGHT);
            }
        }
        return;
    }

    if (game->mode == MODE_EXTRAS) {
        if (key == SDLK_RETURN || key == SDLK_SPACE || key == SDLK_e || key == SDLK_t) {
            game->mode = MODE_TITLE;
            audio_play(audio, SOUND_CAMERA);
        }
        return;
    }

    if (game->mode == MODE_PAUSED) {
        if (key == SDLK_RETURN || key == SDLK_p) {
            game->mode = MODE_PLAYING;
            audio_play(audio, SOUND_START);
        } else if (key == SDLK_r) {
            start_night(game);
            audio_play(audio, SOUND_START);
        } else if (key == SDLK_t) {
            return_to_title(game, audio);
        }
        return;
    }

    if (game->mode == MODE_WIN) {
        if (key == SDLK_RETURN || key == SDLK_SPACE) {
            start_next_unlocked_night(game, audio);
        } else if (key == SDLK_r) {
            start_night(game);
            audio_play(audio, SOUND_START);
        }
        return;
    }

    if (game->mode != MODE_PLAYING) {
        if (key == SDLK_r) {
            start_night(game);
            audio_play(audio, SOUND_START);
        }
        return;
    }

    if (game->power_out) {
        if (key == SDLK_p) {
            game->mode = MODE_PAUSED;
        }
        return;
    }

    switch (key) {
    case SDLK_c:
        if (game_phone_call_active(game)) {
            game->call_muted = true;
            audio_play(audio, SOUND_LIGHT);
        }
        break;
    case SDLK_a:
        game->left_door = !game->left_door;
        audio_play(audio, SOUND_DOOR);
        break;
    case SDLK_d:
        game->right_door = !game->right_door;
        audio_play(audio, SOUND_DOOR);
        break;
    case SDLK_q:
        game->left_light = !game->left_light;
        audio_play(audio, SOUND_LIGHT);
        break;
    case SDLK_e:
        game->right_light = !game->right_light;
        audio_play(audio, SOUND_LIGHT);
        break;
    case SDLK_SPACE:
        game->monitor = !game->monitor;
        audio_play(audio, SOUND_CAMERA);
        break;
    case SDLK_l:
        play_audio_lure(game, audio);
        break;
    case SDLK_p:
        game->mode = MODE_PAUSED;
        break;
    case SDLK_1:
    case SDLK_2:
    case SDLK_3:
    case SDLK_4:
    case SDLK_5:
    case SDLK_6:
        select_camera(game, audio, (int)(key - SDLK_1));
        break;
    default:
        break;
    }
}

static void handle_mouse_click(Game *game, Audio *audio, bool *show_help, int x, int y)
{
    if (*show_help) {
        *show_help = false;
        audio_play(audio, SOUND_LIGHT);
        return;
    }

    if (game->mode == MODE_TITLE) {
        if (hit_rect(x, y, 300, 420, 360, 54)) {
            start_night(game);
            audio_play(audio, SOUND_START);
            return;
        }
        if (extras_unlocked(game) && hit_rect(x, y, 670, 280, 210, 34)) {
            game->mode = MODE_EXTRAS;
            audio_play(audio, SOUND_CAMERA);
            return;
        }
        int custom_button_y = game_is_custom_night(game) ? 332 : 320;
        if (hit_rect(x, y, 670, custom_button_y, 210, 34)) {
            toggle_custom_night(game, audio);
            return;
        }
        if (game_is_custom_night(game)) {
            for (int i = 0; i < THREAT_COUNT; i++) {
                int y0 = 226 + (i * 25);
                if (hit_rect(x, y, 690, y0, 130, 22)) {
                    game->selected_custom_threat = i;
                    audio_play(audio, SOUND_LIGHT);
                    return;
                }
                if (hit_rect(x, y, 812, y0, 28, 22)) {
                    game->selected_custom_threat = i;
                    adjust_custom_ai(game, audio, -1);
                    return;
                }
                if (hit_rect(x, y, 888, y0, 28, 22)) {
                    game->selected_custom_threat = i;
                    adjust_custom_ai(game, audio, 1);
                    return;
                }
            }
        }
        if (hit_rect(x, y, 320, 360, 150, 62) && game->night > MIN_NIGHT) {
            game->night--;
            audio_play(audio, SOUND_LIGHT);
            return;
        }
        if (hit_rect(x, y, 490, 360, 150, 62) && game->night < game->unlocked_night) {
            game->night++;
            audio_play(audio, SOUND_LIGHT);
            return;
        }
        return;
    }

    if (game->mode == MODE_EXTRAS) {
        if (hit_rect(x, y, 350, 456, 260, 42)) {
            game->mode = MODE_TITLE;
            audio_play(audio, SOUND_CAMERA);
        }
        return;
    }

    if (game->mode == MODE_PAUSED) {
        if (hit_rect(x, y, 320, 220, 320, 55)) {
            game->mode = MODE_PLAYING;
            audio_play(audio, SOUND_START);
            return;
        }
        if (hit_rect(x, y, 350, 280, 260, 55)) {
            start_night(game);
            audio_play(audio, SOUND_START);
            return;
        }
        if (hit_rect(x, y, 350, 340, 260, 55)) {
            return_to_title(game, audio);
            return;
        }
        return;
    }

    if (game->mode == MODE_WIN) {
        if (hit_rect(x, y, 320, 400, 320, 42)) {
            start_next_unlocked_night(game, audio);
        } else if (hit_rect(x, y, 320, 452, 320, 42)) {
            start_night(game);
            audio_play(audio, SOUND_START);
        }
        return;
    }

    if (game->mode == MODE_LOSS) {
        if (hit_rect(x, y, 320, 470, 320, 42)) {
            start_night(game);
            audio_play(audio, SOUND_START);
        }
        return;
    }

    if (game->mode != MODE_PLAYING) {
        return;
    }

    if (game->power_out) {
        return;
    }

    if (game_phone_call_active(game) && hit_rect(x, y, 610, 116, 310, 124)) {
        game->call_muted = true;
        audio_play(audio, SOUND_LIGHT);
        return;
    }

    if (game->monitor) {
        if (hit_rect(x, y, 770, 285, 150, 30)) {
            play_audio_lure(game, audio);
            return;
        }
        for (int i = 0; i < CAMERA_COUNT; i++) {
            if (hit_rect(x, y, 770, 55 + (i * 38), 130, 30)) {
                select_camera(game, audio, i);
                return;
            }
        }

        static const int node_x[CAMERA_COUNT] = { 802, 842, 790, 888, 828, 874 };
        static const int node_y[CAMERA_COUNT] = { 330, 375, 420, 420, 465, 465 };
        for (int i = 0; i < CAMERA_COUNT; i++) {
            if (hit_rect(x, y, node_x[i], node_y[i], 28, 22)) {
                select_camera(game, audio, i);
                return;
            }
        }

        if (hit_rect(x, y, 40, 456, 690, 60)) {
            game->monitor = false;
            audio_play(audio, SOUND_CAMERA);
        }
        return;
    }

    if (hit_rect(x, y, 20, 20, 140, 24)) {
        game->left_door = !game->left_door;
        audio_play(audio, SOUND_DOOR);
    } else if (hit_rect(x, y, 20, 52, 140, 24)) {
        game->left_light = !game->left_light;
        audio_play(audio, SOUND_LIGHT);
    } else if (hit_rect(x, y, 800, 452, 140, 24)) {
        game->right_door = !game->right_door;
        audio_play(audio, SOUND_DOOR);
    } else if (hit_rect(x, y, 800, 484, 140, 24)) {
        game->right_light = !game->right_light;
        audio_play(audio, SOUND_LIGHT);
    } else if (hit_rect(x, y, 416, 395, 128, 42)) {
        game->monitor = true;
        audio_play(audio, SOUND_CAMERA);
    }
}

static void draw_office(SDL_Renderer *renderer, const Game *game)
{
    if (game->power_out) {
        draw_vertical_gradient(renderer, 0, 0, WINDOW_W, WINDOW_H, (SDL_Color){ 1, 1, 2, 255 }, (SDL_Color){ 8, 4, 5, 255 });
        draw_panel(renderer, 300, 130, 360, 220, (SDL_Color){ 7, 8, 9, 245 }, (SDL_Color){ 30, 32, 33, 255 });
        fill_rect(renderer, 0, 125, 140, 320, (SDL_Color){ 5, 5, 7, 255 });
        fill_rect(renderer, 820, 125, 140, 320, (SDL_Color){ 5, 5, 7, 255 });
        draw_noise(renderer, 42, 0, 0, WINDOW_W, WINDOW_H, (SDL_Color){ 110, 30, 28, 36 }, 180);
        draw_vignette(renderer);
        draw_text(renderer, "POWER OUT", 356, 212, 4, (SDL_Color){ 180, 42, 37, 255 });
        draw_text(renderer, "SYSTEMS OFFLINE", 344, 270, 2, (SDL_Color){ 116, 123, 123, 255 });
        return;
    }

    SDL_Rect full = { 0, 0, WINDOW_W, WINDOW_H };
    if (draw_texture(renderer, g_art.office, NULL, &full, 255)) {
        fill_rect(renderer, 0, 0, WINDOW_W, WINDOW_H, (SDL_Color){ 0, 0, 0, 54 });
        draw_scanlines(renderer, 0, (SDL_Color){ 0, 0, 0, 18 });
    } else {
        draw_vertical_gradient(renderer, 0, 0, WINDOW_W, WINDOW_H, (SDL_Color){ 11, 14, 15, 255 }, (SDL_Color){ 5, 6, 7, 255 });
        draw_vertical_gradient(renderer, 0, 0, WINDOW_W, 96, (SDL_Color){ 27, 29, 29, 255 }, (SDL_Color){ 11, 12, 13, 255 });
        draw_vertical_gradient(renderer, 0, 440, WINDOW_W, 100, (SDL_Color){ 22, 18, 15, 255 }, (SDL_Color){ 7, 6, 6, 255 });
        for (int x = 0; x < WINDOW_W; x += 80) {
            draw_line(renderer, x, 440, x - 80, 540, (SDL_Color){ 47, 38, 31, 115 });
            draw_line(renderer, x, 440, x + 80, 540, (SDL_Color){ 47, 38, 31, 115 });
        }
        draw_panel(renderer, 290, 122, 380, 268, (SDL_Color){ 22, 25, 26, 245 }, (SDL_Color){ 82, 90, 91, 255 });
        fill_rect(renderer, 322, 155, 316, 190, (SDL_Color){ 12, 15, 16, 255 });
        draw_wall_grime(renderer, 31, 322, 155, 316, 190);
        for (int i = 0; i < 7; i++) {
            draw_line(renderer, 336 + (i * 45), 160, 336 + (i * 45), 340, (SDL_Color){ 38, 45, 45, 130 });
        }
        draw_line(renderer, 322, 250, 638, 250, (SDL_Color){ 50, 58, 58, 150 });
        draw_security_monitor(renderer, 348, 214, 82, 58, (SDL_Color){ 86, 185, 145, 255 });
        draw_security_monitor(renderer, 530, 204, 82, 58, (SDL_Color){ 148, 118, 72, 255 });
        draw_hanging_cable(renderer, 456, 122, 95, (SDL_Color){ 22, 25, 25, 210 });
        draw_hanging_cable(renderer, 506, 122, 116, (SDL_Color){ 22, 25, 25, 210 });
    }

    draw_horizontal_gradient(renderer, 0, 115, 140, 330,
        game->left_door ? (SDL_Color){ 92, 88, 79, 255 } : (SDL_Color){ 19, 22, 25, 255 },
        (SDL_Color){ 8, 10, 11, 255 });
    draw_horizontal_gradient(renderer, 820, 115, 140, 330,
        (SDL_Color){ 8, 10, 11, 255 },
        game->right_door ? (SDL_Color){ 92, 88, 79, 255 } : (SDL_Color){ 19, 22, 25, 255 });
    draw_rect(renderer, 12, 128, 116, 305, (SDL_Color){ 106, 101, 91, 255 });
    draw_rect(renderer, 832, 128, 116, 305, (SDL_Color){ 106, 101, 91, 255 });
    for (int i = 0; i < 5; i++) {
        draw_line(renderer, 22, 150 + (i * 48), 120, 150 + (i * 48), (SDL_Color){ 128, 121, 104, 90 });
        draw_line(renderer, 840, 150 + (i * 48), 940, 150 + (i * 48), (SDL_Color){ 128, 121, 104, 90 });
    }

    if (game->left_light) {
        draw_horizontal_gradient(renderer, 130, 136, 230, 215, (SDL_Color){ 189, 170, 102, 125 }, (SDL_Color){ 10, 10, 8, 0 });
        draw_noise(renderer, 7, 142, 150, 190, 180, (SDL_Color){ 247, 224, 143, 45 }, 70);
    }
    if (game->right_light) {
        draw_horizontal_gradient(renderer, 600, 136, 230, 215, (SDL_Color){ 10, 10, 8, 0 }, (SDL_Color){ 189, 170, 102, 125 });
        draw_noise(renderer, 11, 630, 150, 190, 180, (SDL_Color){ 247, 224, 143, 45 }, 70);
    }

    for (int i = 0; i < THREAT_COUNT; i++) {
        const Threat *threat = &game->threats[i];
        if (!threat->active) {
            continue;
        }
        bool visible_left = game->left_light && threat->scene == SCENE_LEFT_HALL;
        bool visible_right = game->right_light && threat->scene == SCENE_RIGHT_HALL && !threat->audio_lure;
        if (visible_left || visible_right) {
            int x = visible_left ? 164 : 690;
            draw_threat_sprite(renderer, i, x, 174, 1, true);
        }
    }

    if (game_vent_danger(game)) {
        fill_rect(renderer, 434, 118, 92, 18, (SDL_Color){ 88, 58, 49, 240 });
        draw_text(renderer, "VENT", 454, 121, 1, (SDL_Color){ 229, 171, 116, 255 });
        draw_line(renderer, 430, 116, 530, 116, (SDL_Color){ 229, 171, 116, 120 });
    }

    draw_panel(renderer, 360, 382, 240, 68, (SDL_Color){ 18, 16, 14, 245 }, (SDL_Color){ 63, 57, 50, 255 });
    draw_security_fan(renderer, 482, 382, 1);
    draw_panel(renderer, 416, 395, 128, 42, (SDL_Color){ 28, 33, 34, 245 }, (SDL_Color){ 89, 99, 101, 255 });
    fill_rect(renderer, 457, 410, 46, 10, game->monitor ? (SDL_Color){ 82, 185, 145, 255 } : (SDL_Color){ 55, 59, 60, 255 });
    draw_text(renderer, "SPACE", 452, 445, 2, (SDL_Color){ 116, 123, 123, 255 });
    draw_scanlines(renderer, 0, (SDL_Color){ 0, 0, 0, 25 });
    draw_vignette(renderer);
}

static SDL_Texture *camera_scene_texture(Scene scene)
{
    switch (scene) {
    case SCENE_STAGE: return g_art.camera_stage;
    case SCENE_DINING: return g_art.camera_dining;
    case SCENE_LEFT_HALL:
    case SCENE_RIGHT_HALL: return g_art.camera_hall;
    case SCENE_BACKSTAGE: return g_art.camera_backstage;
    case SCENE_VENT: return g_art.camera_vent;
    case SCENE_OFFICE: return g_art.office;
    }
    return NULL;
}

static bool draw_camera_scene_art(SDL_Renderer *renderer, Scene scene)
{
    SDL_Rect dst = { 40, 36, 690, 420 };
    if (!draw_texture(renderer, camera_scene_texture(scene), NULL, &dst, 255)) {
        return false;
    }
    fill_rect(renderer, 40, 36, 690, 420, (SDL_Color){ 0, 18, 12, 58 });
    draw_rect(renderer, 66, 62, 638, 368, (SDL_Color){ 82, 138, 112, 118 });
    return true;
}

static void draw_camera_feed(SDL_Renderer *renderer, const Game *game)
{
    Scene scene = camera_scene(game->selected_camera);
    draw_vertical_gradient(renderer, 0, 0, WINDOW_W, WINDOW_H, (SDL_Color){ 5, 9, 9, 255 }, (SDL_Color){ 1, 3, 4, 255 });
    draw_panel(renderer, 40, 36, 690, 420, (SDL_Color){ 11, 20, 19, 245 }, (SDL_Color){ 89, 124, 114, 255 });
    draw_text(renderer, scene_name(scene), 64, 54, 2, (SDL_Color){ 134, 193, 173, 255 });
    draw_text(renderer, "REC", 648, 54, 2, (SDL_Color){ 211, 70, 54, 255 });
    fill_rect(renderer, 626, 57, 10, 10, (SDL_Color){ 211, 70, 54, 255 });

    int hash = (int)scene * 31;
    for (int i = 0; i < 28; i++) {
        int x = 65 + ((i * 79 + hash) % 620);
        int y = 70 + ((i * 47 + hash) % 340);
        fill_rect(renderer, x, y, 55 + ((i * 13) % 65), 2, (SDL_Color){ 31, 58, 53, 105 });
    }

    if (scene == SCENE_STAGE) {
        if (draw_camera_scene_art(renderer, scene)) {
            draw_text(renderer, "SHOW STAGE", 284, 392, 2, (SDL_Color){ 118, 160, 145, 255 });
        } else {
        draw_vertical_gradient(renderer, 85, 92, 600, 295, (SDL_Color){ 31, 23, 27, 255 }, (SDL_Color){ 8, 9, 10, 255 });
        draw_wall_grime(renderer, 80, 85, 92, 600, 295);
        fill_rect(renderer, 210, 104, 350, 24, (SDL_Color){ 86, 54, 61, 255 });
        draw_soft_ellipse(renderer, 294, 196, 58, 125, (SDL_Color){ 210, 90, 70, 58 });
        draw_soft_ellipse(renderer, 478, 196, 58, 125, (SDL_Color){ 90, 130, 220, 54 });
        for (int i = 0; i < 9; i++) {
            fill_rect(renderer, 114 + (i * 61), 128, 31, 205, (SDL_Color){ 37, 28, 34, 255 });
            draw_line(renderer, 114 + (i * 61), 128, 145 + (i * 61), 333, (SDL_Color){ 80, 54, 62, 80 });
        }
        fill_rect(renderer, 235, 332, 300, 28, (SDL_Color){ 35, 29, 27, 255 });
        draw_text(renderer, "SHOWTIME CURTAIN", 262, 392, 2, (SDL_Color){ 118, 96, 99, 255 });
        }
    } else if (scene == SCENE_DINING) {
        if (draw_camera_scene_art(renderer, scene)) {
            draw_text(renderer, "AUDIO REACHES ECHO", 220, 382, 2, (SDL_Color){ 134, 193, 173, 255 });
        } else {
        draw_vertical_gradient(renderer, 70, 92, 630, 295, (SDL_Color){ 28, 25, 23, 255 }, (SDL_Color){ 9, 9, 9, 255 });
        draw_wall_grime(renderer, 96, 70, 92, 630, 295);
        for (int i = 0; i < 6; i++) {
            fill_rect(renderer, 92 + (i * 100), 110, 44, 16, (SDL_Color){ 64, 50, 34, 255 });
            draw_line(renderer, 114 + (i * 100), 126, 80 + (i * 95), 238, (SDL_Color){ 80, 63, 42, 95 });
            draw_line(renderer, 114 + (i * 100), 126, 145 + (i * 95), 238, (SDL_Color){ 80, 63, 42, 95 });
        }
        for (int i = 0; i < 4; i++) {
            fill_rect(renderer, 120 + (i * 138), 236, 92, 28, (SDL_Color){ 68, 56, 43, 255 });
            fill_rect(renderer, 134 + (i * 138), 264, 12, 58, (SDL_Color){ 44, 36, 30, 255 });
            fill_rect(renderer, 186 + (i * 138), 264, 12, 58, (SDL_Color){ 44, 36, 30, 255 });
            draw_rect(renderer, 120 + (i * 138), 236, 92, 28, (SDL_Color){ 96, 75, 50, 150 });
        }
        draw_text(renderer, "AUDIO REACHES ECHO", 220, 382, 2, (SDL_Color){ 134, 193, 173, 255 });
        }
    } else if (scene == SCENE_LEFT_HALL || scene == SCENE_RIGHT_HALL) {
        if (draw_camera_scene_art(renderer, scene)) {
            draw_text(renderer, scene == SCENE_LEFT_HALL ? "WEST SERVICE HALL" : "EAST SERVICE HALL", 250, 392, 2,
                (SDL_Color){ 103, 132, 124, 255 });
        } else {
        draw_vertical_gradient(renderer, 130, 88, 500, 305, (SDL_Color){ 25, 29, 30, 255 }, (SDL_Color){ 5, 6, 7, 255 });
        draw_wall_grime(renderer, scene == SCENE_LEFT_HALL ? 121 : 122, 130, 88, 500, 305);
        draw_rect(renderer, 205, 108, 320, 262, (SDL_Color){ 63, 72, 72, 255 });
        draw_line(renderer, 205, 108, 64, 390, (SDL_Color){ 57, 66, 66, 170 });
        draw_line(renderer, 525, 108, 695, 390, (SDL_Color){ 57, 66, 66, 170 });
        for (int i = 0; i < 6; i++) {
            draw_line(renderer, 110, 365 - (i * 36), 645, 365 - (i * 20), (SDL_Color){ 40, 46, 45, 130 });
        }
        fill_rect(renderer, scene == SCENE_LEFT_HALL ? 165 : 520, 155, 30, 170, (SDL_Color){ 78, 73, 65, 255 });
        draw_text(renderer, scene == SCENE_LEFT_HALL ? "WEST SERVICE HALL" : "EAST SERVICE HALL", 250, 392, 2,
            (SDL_Color){ 103, 132, 124, 255 });
        }
    } else if (scene == SCENE_BACKSTAGE) {
        if (draw_camera_scene_art(renderer, scene)) {
            draw_text(renderer, "PARTS STORAGE", 280, 392, 2, (SDL_Color){ 130, 118, 145, 255 });
        } else {
        draw_vertical_gradient(renderer, 105, 105, 560, 282, (SDL_Color){ 26, 22, 29, 255 }, (SDL_Color){ 8, 8, 10, 255 });
        draw_wall_grime(renderer, 156, 105, 105, 560, 282);
        draw_rect(renderer, 125, 120, 520, 260, (SDL_Color){ 58, 54, 66, 255 });
        for (int i = 0; i < 5; i++) {
            int x = 145 + (i * 95);
            draw_panel(renderer, x, 154, 54, 92, (SDL_Color){ 35, 36, 39, 210 }, (SDL_Color){ 63, 64, 68, 255 });
            fill_rect(renderer, x + 13, 171, 28, 30, (SDL_Color){ 51, 52, 55, 255 });
            fill_rect(renderer, x + 18, 184, 6, 6, (SDL_Color){ 110, 50, 47, 220 });
            fill_rect(renderer, x + 31, 184, 6, 6, (SDL_Color){ 110, 50, 47, 220 });
        }
        draw_text(renderer, "PARTS STORAGE", 280, 392, 2, (SDL_Color){ 130, 118, 145, 255 });
        }
    } else if (scene == SCENE_VENT) {
        if (!draw_camera_scene_art(renderer, scene)) {
            draw_vertical_gradient(renderer, 92, 84, 590, 300, (SDL_Color){ 24, 27, 27, 255 }, (SDL_Color){ 4, 5, 5, 255 });
            draw_rect(renderer, 150, 125, 470, 210, (SDL_Color){ 80, 88, 86, 255 });
            draw_rect(renderer, 178, 145, 414, 170, (SDL_Color){ 42, 49, 48, 255 });
            for (int i = 0; i < 8; i++) {
                fill_rect(renderer, 170 + (i * 52), 130, 8, 200, (SDL_Color){ 49, 56, 55, 255 });
                draw_line(renderer, 150, 150 + (i * 24), 620, 126 + (i * 26), (SDL_Color){ 34, 42, 41, 150 });
            }
        }
        draw_text(renderer, "WATCH TO DRIVE SKITR BACK", 182, 382, 2, (SDL_Color){ 134, 193, 173, 255 });
    }

    for (int i = 0; i < THREAT_COUNT; i++) {
        const Threat *threat = &game->threats[i];
        if (threat->active && threat->scene == scene) {
            int x = 240 + (i * 88);
            draw_threat_sprite(renderer, i, x, 170, 1, false);
            draw_text(renderer, threat->name, x - 4, 315, 2, (SDL_Color){ 174, 204, 194, 255 });
        }
    }

    for (int i = 0; i < CAMERA_COUNT; i++) {
        int x = 770;
        int y = 55 + (i * 38);
        SDL_Color color = i == game->selected_camera ? (SDL_Color){ 86, 185, 145, 255 } : (SDL_Color){ 28, 37, 38, 255 };
        draw_panel(renderer, x, y, 130, 30, color, i == game->selected_camera ?
            (SDL_Color){ 160, 230, 195, 255 } : (SDL_Color){ 51, 68, 67, 255 });
        draw_text(renderer, "CAM", x + 12, y + 7, 2, (SDL_Color){ 9, 11, 12, 255 });
        draw_number(renderer, i + 1, x + 82, y + 8, 1, (SDL_Color){ 9, 11, 12, 255 });
    }
    draw_camera_map(renderer, game);
    fill_rect(renderer, 770, 285, 150, 30, game_audio_lure_active(game) ?
        (SDL_Color){ 118, 88, 44, 255 } : (SDL_Color){ 36, 43, 44, 255 });
    draw_text(renderer, game_audio_lure_active(game) ? "AUDIO ON" : "L AUDIO", 790, 294, 2,
        game_audio_lure_active(game) ? (SDL_Color){ 255, 224, 150, 255 } : (SDL_Color){ 134, 193, 173, 255 });
    if (game_audio_lure_active(game)) {
        draw_text(renderer, scene_name(game->audio_lure_scene), 66, 442, 1, (SDL_Color){ 211, 175, 92, 255 });
    }

    int static_rows = 8 + ((int)(game->static_timer * 12.0f) % 5);
    for (int i = 0; i < static_rows; i++) {
        int y = (i * 59 + (int)(game->static_timer * 75.0f)) % WINDOW_H;
        fill_rect(renderer, 0, y, WINDOW_W, 2, (SDL_Color){ 140, 166, 158, 65 });
    }
    draw_noise(renderer, 100 + (int)(game->static_timer * 19.0f), 40, 36, 690, 420, (SDL_Color){ 170, 210, 198, 34 }, 260);
    draw_scanlines(renderer, (int)(game->static_timer * 22.0f), (SDL_Color){ 0, 0, 0, 38 });
    draw_vignette(renderer);
    draw_text(renderer, "1-6 SELECT  L AUDIO  SPACE CLOSE", 66, 472, 2, (SDL_Color){ 112, 151, 139, 255 });
}

static void draw_hud(SDL_Renderer *renderer, const Game *game)
{
    int hour = game_hour(game);
    draw_text(renderer, "AM", 900, 28, 2, (SDL_Color){ 220, 230, 214, 255 });
    draw_number(renderer, hour, 820, 18, 2, (SDL_Color){ 220, 230, 214, 255 });
    if (game_is_custom_night(game)) {
        draw_text(renderer, "CUSTOM", 790, 75, 2, (SDL_Color){ 211, 175, 92, 255 });
    } else {
        draw_text(renderer, "NIGHT", 800, 75, 2, (SDL_Color){ 151, 158, 151, 255 });
        draw_number(renderer, game->night, 890, 73, 1, (SDL_Color){ 151, 158, 151, 255 });
    }

    SDL_Color power_label = game->power_out ? (SDL_Color){ 180, 42, 37, 255 } : (SDL_Color){ 210, 220, 204, 255 };
    draw_word_power(renderer, 22, 500, power_label);
    draw_bar(renderer, 120, 496, 210, 24, game->power / 100.0f,
        game->power > 25.0f ? (SDL_Color){ 82, 185, 145, 255 } : (SDL_Color){ 211, 70, 54, 255 });
    draw_number(renderer, (int)roundf(game->power), 350, 496, 1, power_label);
    draw_text(renderer, "USAGE", 430, 500, 2, (SDL_Color){ 151, 158, 151, 255 });
    int usage = power_usage_level(game);
    for (int i = 0; i < 6; i++) {
        SDL_Color color = i < usage ? (SDL_Color){ 211, 175, 92, 255 } : (SDL_Color){ 44, 49, 50, 255 };
        fill_rect(renderer, 512 + (i * 16), 498, 10, 20, color);
    }

    if (!game->monitor) {
        fill_rect(renderer, 20, 20, 140, 24, game->left_door ? (SDL_Color){ 105, 73, 65, 255 } : (SDL_Color){ 40, 46, 47, 255 });
        fill_rect(renderer, 20, 52, 140, 24, game->left_light ? (SDL_Color){ 120, 113, 76, 255 } : (SDL_Color){ 40, 46, 47, 255 });
        fill_rect(renderer, 800, 452, 140, 24, game->right_door ? (SDL_Color){ 105, 73, 65, 255 } : (SDL_Color){ 40, 46, 47, 255 });
        fill_rect(renderer, 800, 484, 140, 24, game->right_light ? (SDL_Color){ 120, 113, 76, 255 } : (SDL_Color){ 40, 46, 47, 255 });
        draw_text(renderer, "A DOOR", 30, 25, 2, (SDL_Color){ 210, 220, 204, 255 });
        draw_text(renderer, "Q LIGHT", 30, 57, 2, (SDL_Color){ 210, 220, 204, 255 });
        draw_text(renderer, "D DOOR", 810, 457, 2, (SDL_Color){ 210, 220, 204, 255 });
        draw_text(renderer, "E LIGHT", 810, 489, 2, (SDL_Color){ 210, 220, 204, 255 });
    }
}

static void draw_threat_alerts(SDL_Renderer *renderer, const Game *game)
{
    if (game->power_out) {
        return;
    }

    bool left = game_left_door_danger(game);
    bool right = game_right_door_danger(game);
    bool vent = game_vent_danger(game);
    bool audio = game_audio_lure_danger(game);
    if (!left && !right && !vent && !audio) {
        return;
    }

    fill_rect(renderer, 338, 96, 284, 82, (SDL_Color){ 24, 13, 11, 225 });
    draw_rect(renderer, 338, 96, 284, 82, (SDL_Color){ 211, 70, 54, 255 });
    draw_text(renderer, "ALERT", 440, 110, 3, (SDL_Color){ 211, 70, 54, 255 });
    int y = 144;
    if (left) {
        draw_text(renderer, "LEFT DOOR", 372, y, 2, (SDL_Color){ 219, 226, 211, 255 });
        y += 18;
    }
    if (right) {
        draw_text(renderer, "RIGHT DOOR", 372, y, 2, (SDL_Color){ 219, 226, 211, 255 });
        y += 18;
    }
    if (vent) {
        draw_text(renderer, "VENT CAM 6", 372, y, 2, (SDL_Color){ 219, 226, 211, 255 });
        y += 18;
    }
    if (audio) {
        draw_text(renderer, "AUDIO LURE", 372, y, 2, (SDL_Color){ 219, 226, 211, 255 });
    }
}

static void draw_phone_call(SDL_Renderer *renderer, const Game *game)
{
    if (!game_phone_call_active(game) || game->monitor) {
        return;
    }

    fill_rect(renderer, 610, 116, 310, 124, (SDL_Color){ 13, 16, 17, 230 });
    draw_rect(renderer, 610, 116, 310, 124, (SDL_Color){ 120, 203, 165, 255 });
    draw_text(renderer, "PHONE", 628, 132, 2, (SDL_Color){ 120, 203, 165, 255 });
    draw_text(renderer, "C MUTE", 828, 132, 2, (SDL_Color){ 151, 158, 151, 255 });
    draw_text(renderer, game_phone_message(game), 628, 164, 2, (SDL_Color){ 219, 226, 211, 255 });

    float remaining = ((float)PHONE_CALL_SECONDS - game->call_timer) / (float)PHONE_CALL_SECONDS;
    draw_bar(renderer, 628, 218, 244, 10, remaining, (SDL_Color){ 120, 203, 165, 255 });
}

static void draw_settings_badges(SDL_Renderer *renderer, const SettingsData *settings)
{
    if (settings->muted) {
        fill_rect(renderer, 18, 88, 92, 22, (SDL_Color){ 48, 36, 34, 225 });
        draw_rect(renderer, 18, 88, 92, 22, (SDL_Color){ 145, 73, 65, 255 });
        draw_text(renderer, "MUTED", 30, 94, 1, (SDL_Color){ 220, 160, 150, 255 });
    }
}

static void draw_star(SDL_Renderer *renderer, int x, int y, SDL_Color color)
{
    fill_rect(renderer, x + 14, y, 8, 32, color);
    fill_rect(renderer, x, y + 12, 36, 8, color);
    fill_rect(renderer, x + 6, y + 6, 24, 20, color);
    fill_rect(renderer, x + 10, y + 10, 16, 12, (SDL_Color){ 5, 6, 7, 255 });
    fill_rect(renderer, x + 15, y + 15, 6, 4, color);
}

static const char *loss_reason_text(LossReason reason)
{
    switch (reason) {
    case LOSS_BREACH: return "OFFICE BREACH";
    case LOSS_BLACKOUT: return "BLACKOUT";
    case LOSS_NONE: return "NONE";
    }
    return "UNKNOWN";
}

static SDL_Color threat_jumpscare_color(int threat_index)
{
    switch (threat_index) {
    case 0: return (SDL_Color){ 185, 91, 67, 255 };
    case 1: return (SDL_Color){ 94, 154, 230, 255 };
    case 2: return (SDL_Color){ 198, 170, 80, 255 };
    case 3: return (SDL_Color){ 158, 106, 210, 255 };
    default: return (SDL_Color){ 230, 58, 48, 255 };
    }
}

static void draw_loss_face(SDL_Renderer *renderer, const Game *game, SDL_Color fg)
{
    int pulse = (int)(sinf(game->jumpscare_timer * 18.0f) * 18.0f);
    int threat = game->loss_reason == LOSS_BREACH ? game->loss_threat : -1;
    SDL_Color face = threat_jumpscare_color(threat);
    fill_rect(renderer, 270 - pulse, 88 - pulse, 420 + (pulse * 2), 340 + (pulse * 2), (SDL_Color){ 3, 3, 4, 180 });
    draw_noise(renderer, 600 + threat, 250 - pulse, 80 - pulse, 460 + (pulse * 2), 360 + (pulse * 2),
        (SDL_Color){ fg.r, fg.g, fg.b, 46 }, 420);
    fill_rect(renderer, 330 - pulse, 115 - pulse, 300 + (pulse * 2), 270 + (pulse * 2), (SDL_Color){ 38, 42, 42, 255 });

    if (threat == 0) {
        draw_threat_sprite(renderer, 0, 374 - pulse, 122 - pulse, 3, true);
        fill_rect(renderer, 390 - pulse, 205, 48 + pulse, 48 + pulse, fg);
        fill_rect(renderer, 520, 205, 48 + pulse, 48 + pulse, fg);
    } else if (threat == 1) {
        draw_threat_sprite(renderer, 1, 374 - pulse, 118 - pulse, 3, true);
        fill_rect(renderer, 392 - pulse, 210, 34 + pulse, 34 + pulse, fg);
        fill_rect(renderer, 535, 210, 34 + pulse, 34 + pulse, fg);
    } else if (threat == 2) {
        draw_threat_sprite(renderer, 2, 374 - pulse, 112 - pulse, 3, true);
        draw_text(renderer, "SKITR", 440, 350, 2, face);
    } else if (threat == 3) {
        draw_threat_sprite(renderer, 3, 374 - pulse, 122 - pulse, 3, true);
        fill_rect(renderer, 408 - pulse, 192, 40 + pulse, 40 + pulse, fg);
        fill_rect(renderer, 510, 192, 40 + pulse, 40 + pulse, fg);
        draw_text(renderer, "ECHO", 446, 350, 2, face);
    } else {
        fill_rect(renderer, 390 - pulse, 195, 48 + pulse, 48 + pulse, fg);
        fill_rect(renderer, 520, 195, 48 + pulse, 48 + pulse, fg);
        fill_rect(renderer, 405, 300, 155, 36, (SDL_Color){ 8, 8, 8, 255 });
    }
}

static void draw_end_screen(SDL_Renderer *renderer, const Game *game)
{
    SDL_Color bg = game->mode == MODE_WIN ? (SDL_Color){ 12, 29, 24, 255 } : (SDL_Color){ 30, 8, 9, 255 };
    SDL_Color fg = game->mode == MODE_WIN ? (SDL_Color){ 108, 219, 162, 255 } : (SDL_Color){ 230, 58, 48, 255 };
    draw_vertical_gradient(renderer, 0, 0, WINDOW_W, WINDOW_H, bg, (SDL_Color){ 3, 4, 5, 255 });
    draw_noise(renderer, game->mode == MODE_WIN ? 720 : 721, 0, 0, WINDOW_W, WINDOW_H,
        game->mode == MODE_WIN ? (SDL_Color){ 108, 219, 162, 25 } : (SDL_Color){ 230, 58, 48, 28 }, 260);

    if (game->mode == MODE_LOSS) {
        draw_loss_face(renderer, game, fg);
        draw_text(renderer, "SHIFT FAILED", 356, 72, 4, fg);
    } else {
        draw_panel(renderer, 292, 128, 376, 250, (SDL_Color){ 8, 34, 25, 160 }, (SDL_Color){ 71, 155, 111, 255 });
        draw_number(renderer, 6, 415, 155, 5, fg);
        draw_text(renderer, "AM", 548, 186, 3, fg);
        fill_rect(renderer, 310, 335, 340, 14, fg);
        draw_text(renderer, "SHIFT COMPLETE", 305, 72, 4, fg);
        if (game_is_custom_night(game)) {
            draw_text(renderer, "CUSTOM NIGHT CLEAR", 326, 365, 3, (SDL_Color){ 219, 226, 211, 255 });
        } else if (game->night < MAX_NIGHT) {
            draw_text(renderer, "NEXT NIGHT UNLOCKED", 314, 365, 3, (SDL_Color){ 219, 226, 211, 255 });
        } else {
            draw_text(renderer, "ALL NIGHTS CLEARED", 326, 365, 3, (SDL_Color){ 219, 226, 211, 255 });
        }
    }

    draw_text(renderer, "NIGHT REPORT", 70, 282, 2, (SDL_Color){ 150, 158, 151, 255 });
    draw_text(renderer, "DOOR BLOCKS", 70, 314, 2, (SDL_Color){ 219, 226, 211, 255 });
    draw_number(renderer, game->door_blocks, 260, 312, 1, (SDL_Color){ 219, 226, 211, 255 });
    draw_text(renderer, "VENT REPELS", 70, 342, 2, (SDL_Color){ 219, 226, 211, 255 });
    draw_number(renderer, game->vent_repels, 260, 340, 1, (SDL_Color){ 219, 226, 211, 255 });
    draw_text(renderer, "AUDIO LURES", 70, 370, 2, (SDL_Color){ 219, 226, 211, 255 });
    draw_number(renderer, game->audio_lures, 260, 368, 1, (SDL_Color){ 219, 226, 211, 255 });
    draw_text(renderer, "POWER LEFT", 70, 398, 2, (SDL_Color){ 219, 226, 211, 255 });
    draw_number(renderer, (int)roundf(game->power), 260, 396, 1, (SDL_Color){ 219, 226, 211, 255 });
    draw_text(renderer, "PCT", 312, 400, 1, (SDL_Color){ 150, 158, 151, 255 });
    if (game->mode == MODE_LOSS) {
        draw_text(renderer, "SURVIVED", 70, 426, 2, (SDL_Color){ 219, 226, 211, 255 });
        draw_number(renderer, game_hour(game), 260, 424, 1, (SDL_Color){ 219, 226, 211, 255 });
        draw_text(renderer, "AM", 312, 428, 1, (SDL_Color){ 150, 158, 151, 255 });
        draw_text(renderer, "CAUSE", 620, 344, 2, (SDL_Color){ 150, 158, 151, 255 });
        draw_text(renderer, loss_reason_text(game->loss_reason), 620, 372, 2, (SDL_Color){ 219, 226, 211, 255 });
        if (game->loss_reason == LOSS_BREACH && game->loss_threat >= 0 && game->loss_threat < THREAT_COUNT) {
            draw_text(renderer, "THREAT", 620, 402, 2, (SDL_Color){ 150, 158, 151, 255 });
            draw_text(renderer, game->threats[game->loss_threat].name, 620, 430, 2, (SDL_Color){ 219, 226, 211, 255 });
        }
    }
    if (game->mode == MODE_WIN) {
        draw_panel(renderer, 320, 400, 320, 42, (SDL_Color){ 18, 40, 33, 220 }, (SDL_Color){ 150, 158, 151, 255 });
        if (game_is_custom_night(game)) {
            draw_text(renderer, "ENTER REPLAY CUSTOM", 340, 414, 2, (SDL_Color){ 150, 158, 151, 255 });
        } else if (game->night < game->unlocked_night) {
            draw_text(renderer, "ENTER NEXT NIGHT", 352, 414, 2, (SDL_Color){ 150, 158, 151, 255 });
        } else {
            draw_text(renderer, "ENTER REPLAY NIGHT", 338, 414, 2, (SDL_Color){ 150, 158, 151, 255 });
        }
        draw_panel(renderer, 320, 452, 320, 42, (SDL_Color){ 18, 22, 22, 220 }, (SDL_Color){ 105, 112, 108, 255 });
        draw_text(renderer, "R REPLAY  ESC QUIT", 350, 466, 2, (SDL_Color){ 150, 158, 151, 255 });
    } else {
        draw_panel(renderer, 320, 470, 320, 42, (SDL_Color){ 35, 12, 13, 220 }, (SDL_Color){ 150, 158, 151, 255 });
        draw_text(renderer, "R RESTART  ESC QUIT", 350, 484, 2, (SDL_Color){ 150, 158, 151, 255 });
    }
    draw_scanlines(renderer, 0, (SDL_Color){ 0, 0, 0, 28 });
    draw_vignette(renderer);
}

static void draw_title_screen(SDL_Renderer *renderer, const Game *game)
{
    SDL_Rect full = { 0, 0, WINDOW_W, WINDOW_H };
    if (draw_texture(renderer, g_art.title, NULL, &full, 255)) {
        fill_rect(renderer, 0, 0, WINDOW_W, WINDOW_H, (SDL_Color){ 0, 0, 0, 72 });
        draw_vertical_gradient(renderer, 0, 0, WINDOW_W, 150, (SDL_Color){ 0, 0, 0, 165 }, (SDL_Color){ 0, 0, 0, 0 });
        draw_vertical_gradient(renderer, 0, 330, WINDOW_W, 210, (SDL_Color){ 0, 0, 0, 18 }, (SDL_Color){ 0, 0, 0, 190 });
    } else {
        draw_vertical_gradient(renderer, 0, 0, WINDOW_W, WINDOW_H, (SDL_Color){ 5, 6, 7, 255 }, (SDL_Color){ 13, 8, 8, 255 });
        draw_vertical_gradient(renderer, 0, 420, WINDOW_W, 120, (SDL_Color){ 22, 17, 14, 255 }, (SDL_Color){ 6, 5, 5, 255 });
        for (int i = 0; i < 9; i++) {
            int x = 50 + (i * 105);
            draw_vertical_gradient(renderer, x, 72, 42, 280, (SDL_Color){ 31, 34, 34, 255 }, (SDL_Color){ 9, 10, 11, 255 });
            draw_rect(renderer, x, 72, 42, 280, (SDL_Color){ 52, 55, 56, 255 });
            draw_line(renderer, x + 8, 72, x + 35, 352, (SDL_Color){ 72, 76, 75, 85 });
        }
        fill_rect(renderer, 284, 124, 392, 218, (SDL_Color){ 0, 0, 0, 95 });
        draw_threat_sprite(renderer, 0, 360, 92, 2, true);
        draw_threat_sprite(renderer, 1, 486, 94, 2, true);
        fill_rect(renderer, 354, 260, 258, 30, (SDL_Color){ 4, 4, 5, 230 });
        draw_noise(renderer, 901, 300, 70, 360, 285, (SDL_Color){ 220, 54, 43, 30 }, 140);
    }

    draw_text(renderer, "NIGHT SHIFT", 230, 32, 6, (SDL_Color){ 219, 226, 211, 255 });
    draw_text(renderer, "V " NIGHTSHIFT_VERSION, 780, 24, 2, (SDL_Color){ 151, 158, 151, 255 });
    if (game->story_cleared) {
        draw_star(renderer, 438, 91, (SDL_Color){ 211, 175, 92, 255 });
    }
    if (game->custom_challenge_cleared) {
        draw_star(renderer, 484, 91, (SDL_Color){ 120, 203, 165, 255 });
    }
    if (game->story_cleared || game->custom_challenge_cleared) {
        draw_text(renderer, "CLEAR STARS", 398, 130, 2, (SDL_Color){ 151, 158, 151, 255 });
    }
    draw_text(renderer, "SURVIVE UNTIL 6 AM", 330, 350, 3, (SDL_Color){ 120, 203, 165, 255 });
    if (game_is_custom_night(game)) {
        draw_text(renderer, "CUSTOM NIGHT", 354, 374, 3, (SDL_Color){ 211, 175, 92, 255 });
        for (int i = 0; i < THREAT_COUNT; i++) {
            int y = 226 + (i * 25);
            SDL_Color color = i == game->selected_custom_threat ?
                (SDL_Color){ 219, 226, 211, 255 } : (SDL_Color){ 151, 158, 151, 255 };
            draw_text(renderer, i == game->selected_custom_threat ? ">" : " ", 668, y, 2, color);
            draw_text(renderer, game->threats[i].name, 690, y, 2, color);
            draw_text(renderer, "AI", 770, y, 2, color);
            draw_text(renderer, "-", 812, y, 2, (SDL_Color){ 120, 203, 165, 255 });
            draw_number(renderer, game->custom_ai[i], 846, y - 2, 1, color);
            draw_text(renderer, "+", 890, y, 2, (SDL_Color){ 120, 203, 165, 255 });
        }
        draw_panel(renderer, 670, 332, 210, 34, (SDL_Color){ 30, 27, 20, 220 }, (SDL_Color){ 105, 112, 108, 255 });
        draw_text(renderer, "C STORY MODE", 690, 342, 2, (SDL_Color){ 151, 158, 151, 255 });
    } else {
        draw_text(renderer, "SELECT NIGHT", 354, 374, 3, (SDL_Color){ 151, 158, 151, 255 });
        draw_number(renderer, game->night, 542, 372, 2, (SDL_Color){ 219, 226, 211, 255 });
        draw_text(renderer, "UNLOCKED", 386, 405, 2, (SDL_Color){ 151, 158, 151, 255 });
        draw_number(renderer, game->unlocked_night, 500, 403, 1, (SDL_Color){ 151, 158, 151, 255 });
        draw_text(renderer, "BEST", 548, 405, 2, (SDL_Color){ 151, 158, 151, 255 });
        draw_number(renderer, game->best_night, 620, 403, 1, (SDL_Color){ 151, 158, 151, 255 });
        draw_text(renderer, "ACTIVE", 690, 150, 2, (SDL_Color){ 151, 158, 151, 255 });
        int threat_y = 176;
        for (int i = 0; i < THREAT_COUNT; i++) {
            if (story_threat_active(i, game->night)) {
                draw_text(renderer, game->threats[i].name, 690, threat_y, 1, (SDL_Color){ 219, 226, 211, 255 });
                threat_y += 14;
            }
        }
        int best_power = game->best_power[game->night - 1];
        if (best_power >= 0) {
            draw_text(renderer, "BEST POWER", 786, 150, 1, (SDL_Color){ 151, 158, 151, 255 });
            draw_number(renderer, best_power, 806, 174, 1, (SDL_Color){ 211, 175, 92, 255 });
            draw_text(renderer, "PCT", 858, 178, 1, (SDL_Color){ 151, 158, 151, 255 });
        }
        if (extras_unlocked(game)) {
            draw_panel(renderer, 670, 280, 210, 34, (SDL_Color){ 18, 23, 22, 220 }, (SDL_Color){ 105, 112, 108, 255 });
            draw_text(renderer, "E EXTRAS", 716, 290, 2, (SDL_Color){ 151, 158, 151, 255 });
        }
        draw_panel(renderer, 670, 320, 210, 34, (SDL_Color){ 18, 23, 22, 220 }, (SDL_Color){ 105, 112, 108, 255 });
        draw_text(renderer, "C CUSTOM NIGHT", 686, 330, 2, (SDL_Color){ 151, 158, 151, 255 });
    }
    draw_panel(renderer, 300, 418, 360, 48, (SDL_Color){ 21, 24, 23, 220 }, (SDL_Color){ 120, 203, 165, 255 });
    draw_text(renderer, "ENTER SPACE OR A START", 306, 430, 3, (SDL_Color){ 219, 226, 211, 255 });
    draw_text(renderer, "LEFT/RIGHT SELECT  C/X CUSTOM  A/D/X/Y DOORS  SPACE/A CAMERAS", 146, 486, 1, (SDL_Color){ 151, 158, 151, 255 });
    if (game->night_seconds < (float)DEFAULT_NIGHT_SECONDS) {
        draw_text(renderer, "FAST NIGHT TEST MODE", 350, 500, 2, (SDL_Color){ 211, 175, 92, 255 });
    }
    draw_scanlines(renderer, 0, (SDL_Color){ 0, 0, 0, 30 });
    draw_vignette(renderer);
}

static void draw_extras_screen(SDL_Renderer *renderer, const Game *game)
{
    draw_vertical_gradient(renderer, 0, 0, WINDOW_W, WINDOW_H, (SDL_Color){ 6, 8, 9, 255 }, (SDL_Color){ 3, 5, 6, 255 });
    for (int i = 0; i < 12; i++) {
        int x = 40 + (i * 82);
        draw_line(renderer, x, 0, x - 120, WINDOW_H, (SDL_Color){ 24, 30, 30, 80 });
    }
    draw_text(renderer, "EXTRAS", 346, 34, 6, (SDL_Color){ 219, 226, 211, 255 });
    draw_text(renderer, "CLEAR FILE DOSSIER", 332, 100, 2, (SDL_Color){ 151, 158, 151, 255 });

    if (game->story_cleared) {
        draw_star(renderer, 80, 70, (SDL_Color){ 211, 175, 92, 255 });
        draw_text(renderer, "STORY CLEAR", 128, 82, 2, (SDL_Color){ 211, 175, 92, 255 });
    } else {
        draw_text(renderer, "STORY STAR LOCKED", 80, 82, 2, (SDL_Color){ 82, 88, 90, 255 });
    }
    if (game->custom_challenge_cleared) {
        draw_star(renderer, 700, 70, (SDL_Color){ 120, 203, 165, 255 });
        draw_text(renderer, "20 20 20 CLEAR", 748, 82, 2, (SDL_Color){ 120, 203, 165, 255 });
    } else {
        draw_text(renderer, "CUSTOM STAR LOCKED", 690, 82, 2, (SDL_Color){ 82, 88, 90, 255 });
    }

    static const char *routes[THREAT_COUNT] = {
        "LEFT ROUTE  DOOR BLOCK",
        "RIGHT ROUTE  DOOR BLOCK",
        "VENT ROUTE  CAM SIX",
        "AUDIO ROUTE  LURE BACK",
    };
    static const SDL_Color colors[THREAT_COUNT] = {
        { 185, 91, 67, 255 },
        { 94, 154, 230, 255 },
        { 198, 170, 80, 255 },
        { 158, 106, 210, 255 },
    };

    for (int i = 0; i < THREAT_COUNT; i++) {
        int x = 112 + ((i % 2) * 380);
        int y = 140 + ((i / 2) * 150);
        draw_panel(renderer, x, y, 356, 130, (SDL_Color){ 15, 18, 19, 235 }, (SDL_Color){ 82, 90, 89, 255 });
        fill_rect(renderer, x + 18, y + 20, 92, 92, (SDL_Color){ 4, 5, 6, 190 });
        int portrait_h = i == 2 ? 74 : 106;
        int portrait_x = i == 2 ? x + 22 : x + 34;
        int portrait_y = i == 2 ? y + 38 : y + 8;
        if (!draw_art_threat_sprite(renderer, i, portrait_x, portrait_y, portrait_h, 245)) {
            draw_threat_sprite(renderer, i, x + 28, y + 8, 1, true);
        }
        draw_line(renderer, x + 18, y + 114, x + 110, y + 114, colors[i]);
        if (i == 2) {
            draw_text(renderer, "VENT", x + 120, y + 92, 2, colors[i]);
        } else if (i == 3) {
            draw_text(renderer, "ECHO", x + 120, y + 92, 2, colors[i]);
        }
        draw_text(renderer, game->threats[i].name, x + 120, y + 24, 3, (SDL_Color){ 219, 226, 211, 255 });
        draw_text(renderer, routes[i], x + 120, y + 68, 1, (SDL_Color){ 151, 158, 151, 255 });
    }

    draw_panel(renderer, 350, 456, 260, 42, (SDL_Color){ 17, 20, 20, 230 }, (SDL_Color){ 105, 112, 108, 255 });
    draw_text(renderer, "E ESC OR B BACK", 382, 470, 2, (SDL_Color){ 151, 158, 151, 255 });
    draw_scanlines(renderer, 0, (SDL_Color){ 0, 0, 0, 25 });
    draw_vignette(renderer);
}

static void draw_pause_overlay(SDL_Renderer *renderer)
{
    fill_rect(renderer, 0, 0, WINDOW_W, WINDOW_H, (SDL_Color){ 0, 0, 0, 150 });
    fill_rect(renderer, 250, 105, 460, 320, (SDL_Color){ 17, 19, 20, 245 });
    draw_rect(renderer, 250, 105, 460, 320, (SDL_Color){ 115, 123, 118, 255 });
    draw_text(renderer, "PAUSED", 376, 154, 5, (SDL_Color){ 219, 226, 211, 255 });
    draw_text(renderer, "ENTER OR P RESUME", 330, 238, 3, (SDL_Color){ 120, 203, 165, 255 });
    draw_text(renderer, "R RESTART", 390, 292, 3, (SDL_Color){ 219, 226, 211, 255 });
    draw_text(renderer, "T TITLE", 410, 350, 3, (SDL_Color){ 219, 226, 211, 255 });
    draw_text(renderer, "ESC RESUME", 376, 397, 2, (SDL_Color){ 151, 158, 151, 255 });
}

static void draw_help_overlay(SDL_Renderer *renderer)
{
    fill_rect(renderer, 0, 0, WINDOW_W, WINDOW_H, (SDL_Color){ 0, 0, 0, 175 });
    fill_rect(renderer, 110, 70, 740, 400, (SDL_Color){ 15, 17, 18, 245 });
    draw_rect(renderer, 110, 70, 740, 400, (SDL_Color){ 115, 123, 118, 255 });
    draw_text(renderer, "HELP", 402, 98, 5, (SDL_Color){ 219, 226, 211, 255 });
    draw_text(renderer, "A/D OR X/Y DOORS     Q/E OR LB/RB LIGHTS", 166, 170, 2, (SDL_Color){ 219, 226, 211, 255 });
    draw_text(renderer, "SPACE OR A CAMERA     1-6 OR DPAD SELECT", 166, 205, 2, (SDL_Color){ 219, 226, 211, 255 });
    draw_text(renderer, "CAM 6 REPELS SKITR IN THE SERVICE VENT", 166, 240, 2, (SDL_Color){ 120, 203, 165, 255 });
    draw_text(renderer, "L OR LEFT STICK PLAYS CAMERA AUDIO FOR ECHO", 166, 260, 2, (SDL_Color){ 158, 106, 210, 255 });
    draw_text(renderer, "AT 0 PERCENT POWER THE OFFICE BLACKS OUT", 166, 280, 2, (SDL_Color){ 211, 175, 92, 255 });
    draw_text(renderer, "F11 FULLSCREEN     +/- SCALE WINDOW", 166, 310, 2, (SDL_Color){ 219, 226, 211, 255 });
    draw_text(renderer, "C OR LEFT STICK MUTES PHONE WHEN CAMERAS CLOSED", 166, 335, 2, (SDL_Color){ 219, 226, 211, 255 });
    draw_text(renderer, "CLICK CONTROLS OR USE KEYBOARD/GAMEPAD", 166, 360, 2, (SDL_Color){ 219, 226, 211, 255 });
    draw_text(renderer, "P ESC OR START PAUSE     T/B TITLE", 166, 385, 2, (SDL_Color){ 219, 226, 211, 255 });
    draw_text(renderer, "R OR Y RESTART AFTER NIGHT", 166, 410, 2, (SDL_Color){ 219, 226, 211, 255 });
    draw_text(renderer, "F1 BACK OR GUIDE CLOSE HELP", 286, 435, 3, (SDL_Color){ 211, 175, 92, 255 });
}

static void render_game(SDL_Renderer *renderer, const Game *game, const SettingsData *settings, bool show_help)
{
    if (game->mode == MODE_TITLE) {
        draw_title_screen(renderer, game);
    } else if (game->mode == MODE_EXTRAS) {
        draw_extras_screen(renderer, game);
    } else if (game->mode == MODE_WIN || game->mode == MODE_LOSS) {
        draw_end_screen(renderer, game);
    } else {
        if (game->monitor) {
            draw_camera_feed(renderer, game);
        } else {
            draw_office(renderer, game);
        }
        draw_hud(renderer, game);
        draw_threat_alerts(renderer, game);
        draw_phone_call(renderer, game);
        if (game->mode == MODE_PAUSED) {
            draw_pause_overlay(renderer);
        }
    }
    draw_settings_badges(renderer, settings);
    if (show_help) {
        draw_help_overlay(renderer);
    }
}

static int run_render_test(SDL_Renderer *renderer, const Game *game)
{
    uint32_t *pixels = malloc((size_t)WINDOW_W * (size_t)WINDOW_H * sizeof(*pixels));
    if (pixels == NULL) {
        fprintf(stderr, "render_test=fail reason=alloc\n");
        return EXIT_FAILURE;
    }

    SettingsData settings;
    init_settings(&settings);
    render_game(renderer, game, &settings, true);
    SDL_RenderPresent(renderer);
    if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, pixels, WINDOW_W * (int)sizeof(*pixels)) != 0) {
        fprintf(stderr, "render_test=fail reason=read_pixels error=%s\n", SDL_GetError());
        free(pixels);
        return EXIT_FAILURE;
    }

    int colored = 0;
    for (int i = 0; i < WINDOW_W * WINDOW_H; i++) {
        if ((pixels[i] & 0x00ffffffu) != 0) {
            colored++;
        }
    }
    free(pixels);

    if (colored < 1000) {
        fprintf(stderr, "render_test=fail reason=blank colored_pixels=%d\n", colored);
        return EXIT_FAILURE;
    }

    printf("render_test=pass colored_pixels=%d\n", colored);
    return EXIT_SUCCESS;
}

static int run_audio_test(void)
{
    Audio audio;
    if (!audio_init(&audio)) {
        fprintf(stderr, "audio_test=fail reason=init error=%s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    const SoundCue cues[] = {
        SOUND_DOOR,
        SOUND_LIGHT,
        SOUND_CAMERA,
        SOUND_LURE,
        SOUND_START,
        SOUND_WIN,
        SOUND_LOSS,
    };

    uint32_t queued_before = SDL_GetQueuedAudioSize(audio.device);
    for (size_t i = 0; i < sizeof(cues) / sizeof(cues[0]); i++) {
        audio_play(&audio, cues[i]);
    }
    uint32_t queued_after = SDL_GetQueuedAudioSize(audio.device);
    if (queued_after <= queued_before) {
        fprintf(stderr, "audio_test=fail reason=no_queued_audio before=%u after=%u\n", queued_before, queued_after);
        audio_shutdown(&audio);
        return EXIT_FAILURE;
    }

    audio_set_muted(&audio, true);
    audio_play(&audio, SOUND_DOOR);
    uint32_t queued_muted = SDL_GetQueuedAudioSize(audio.device);
    if (queued_muted != queued_after) {
        fprintf(stderr, "audio_test=fail reason=mute_queued_audio before=%u after=%u\n", queued_after, queued_muted);
        audio_shutdown(&audio);
        return EXIT_FAILURE;
    }

    audio_shutdown(&audio);
    printf("audio_test=pass queued_bytes=%u\n", queued_after);
    return EXIT_SUCCESS;
}

static int save_screenshot(SDL_Renderer *renderer, const Game *game, const char *path)
{
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, WINDOW_W, WINDOW_H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface == NULL) {
        fprintf(stderr, "screenshot=fail reason=create_surface error=%s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    SettingsData settings;
    init_settings(&settings);
    render_game(renderer, game, &settings, false);
    SDL_RenderPresent(renderer);
    if (SDL_RenderReadPixels(renderer, NULL, surface->format->format, surface->pixels, surface->pitch) != 0) {
        fprintf(stderr, "screenshot=fail reason=read_pixels error=%s\n", SDL_GetError());
        SDL_FreeSurface(surface);
        return EXIT_FAILURE;
    }

    int colored = 0;
    const int pitch_pixels = surface->pitch / (int)sizeof(uint32_t);
    const uint32_t *pixels = (const uint32_t *)surface->pixels;
    for (int y = 0; y < WINDOW_H; y++) {
        for (int x = 0; x < WINDOW_W; x++) {
            if ((pixels[(y * pitch_pixels) + x] & 0x00ffffffu) != 0) {
                colored++;
            }
        }
    }
    if (colored < 1000) {
        fprintf(stderr, "screenshot=fail reason=blank colored_pixels=%d path=%s\n", colored, path);
        SDL_FreeSurface(surface);
        return EXIT_FAILURE;
    }

    if (SDL_SaveBMP(surface, path) != 0) {
        fprintf(stderr, "screenshot=fail reason=save_bmp path=%s error=%s\n", path, SDL_GetError());
        SDL_FreeSurface(surface);
        return EXIT_FAILURE;
    }

    SDL_FreeSurface(surface);
    printf("screenshot=pass path=%s colored_pixels=%d\n", path, colored);
    return EXIT_SUCCESS;
}

static bool parse_screenshot_scene(const char *value, ScreenshotScene *scene)
{
    if (strcmp(value, "title") == 0) {
        *scene = SHOT_TITLE;
    } else if (strcmp(value, "title-cleared") == 0) {
        *scene = SHOT_TITLE_CLEARED;
    } else if (strcmp(value, "extras") == 0) {
        *scene = SHOT_EXTRAS;
    } else if (strcmp(value, "office") == 0) {
        *scene = SHOT_OFFICE;
    } else if (strcmp(value, "camera") == 0) {
        *scene = SHOT_CAMERA;
    } else if (strcmp(value, "camera-stage") == 0) {
        *scene = SHOT_CAMERA_STAGE;
    } else if (strcmp(value, "camera-dining") == 0) {
        *scene = SHOT_CAMERA_DINING;
    } else if (strcmp(value, "camera-hall") == 0) {
        *scene = SHOT_CAMERA_HALL;
    } else if (strcmp(value, "camera-backstage") == 0) {
        *scene = SHOT_CAMERA_BACKSTAGE;
    } else if (strcmp(value, "win") == 0) {
        *scene = SHOT_WIN;
    } else if (strcmp(value, "loss-rust") == 0) {
        *scene = SHOT_LOSS_RUST;
    } else if (strcmp(value, "loss-volt") == 0) {
        *scene = SHOT_LOSS_VOLT;
    } else if (strcmp(value, "loss-skitr") == 0) {
        *scene = SHOT_LOSS_SKITR;
    } else if (strcmp(value, "loss-echo") == 0) {
        *scene = SHOT_LOSS_ECHO;
    } else if (strcmp(value, "blackout") == 0) {
        *scene = SHOT_BLACKOUT;
    } else {
        return false;
    }
    return true;
}

static Game screenshot_game(ScreenshotScene scene, float night_seconds)
{
    Game game = { 0 };
    game.night_seconds = night_seconds;
    game.unlocked_night = 3;
    game.best_night = 2;
    game.night = 2;
    for (int i = 0; i < MAX_NIGHT; i++) {
        game.best_power[i] = -1;
    }
    init_game(&game);

    switch (scene) {
    case SHOT_TITLE:
        break;
    case SHOT_TITLE_CLEARED:
        game.unlocked_night = MAX_NIGHT;
        game.best_night = MAX_NIGHT;
        game.night = MAX_NIGHT;
        game.story_cleared = true;
        game.custom_challenge_cleared = true;
        init_game(&game);
        break;
    case SHOT_EXTRAS:
        game.unlocked_night = MAX_NIGHT;
        game.best_night = MAX_NIGHT;
        game.night = MAX_NIGHT;
        game.story_cleared = true;
        game.custom_challenge_cleared = true;
        init_game(&game);
        game.mode = MODE_EXTRAS;
        break;
    case SHOT_OFFICE:
        start_night(&game);
        game.survived = game.night_seconds * 0.35f;
        game.left_light = true;
        game.threats[0].scene = SCENE_LEFT_HALL;
        game.threats[0].route_pos = 3;
        break;
    case SHOT_CAMERA:
        start_night(&game);
        game.survived = game.night_seconds * 0.45f;
        game.monitor = true;
        game.selected_camera = 5;
        game.threats[2].active = true;
        game.threats[2].scene = SCENE_VENT;
        game.threats[2].route_pos = 2;
        break;
    case SHOT_CAMERA_STAGE:
    case SHOT_CAMERA_DINING:
    case SHOT_CAMERA_HALL:
    case SHOT_CAMERA_BACKSTAGE:
        start_night(&game);
        game.survived = game.night_seconds * 0.45f;
        game.monitor = true;
        game.selected_camera = scene == SHOT_CAMERA_STAGE ? 0 :
            scene == SHOT_CAMERA_DINING ? 1 :
            scene == SHOT_CAMERA_HALL ? 2 : 4;
        break;
    case SHOT_WIN:
        start_night(&game);
        game.mode = MODE_WIN;
        game.survived = game.night_seconds;
        game.power = 42.0f;
        game.door_blocks = 3;
        game.vent_repels = 1;
        break;
    case SHOT_LOSS_RUST:
    case SHOT_LOSS_VOLT:
    case SHOT_LOSS_SKITR:
    case SHOT_LOSS_ECHO: {
        int threat = scene == SHOT_LOSS_RUST ? 0 : scene == SHOT_LOSS_VOLT ? 1 :
            scene == SHOT_LOSS_SKITR ? 2 : 3;
        start_night(&game);
        game.mode = MODE_LOSS;
        game.loss_reason = LOSS_BREACH;
        game.loss_threat = threat;
        game.survived = game.night_seconds * (0.22f + ((float)threat * 0.16f));
        game.threats[threat].active = true;
        game.threats[threat].scene = SCENE_OFFICE;
        game.threats[threat].route_pos = game.threats[threat].route_len - 1;
        game.jumpscare_timer = 0.18f;
        game.power = 31.0f;
        game.door_blocks = 1;
        game.vent_repels = threat == 2 ? 0 : 1;
        game.audio_lures = threat == 3 ? 0 : 1;
        break;
    }
    case SHOT_BLACKOUT:
        start_night(&game);
        game.mode = MODE_LOSS;
        game.loss_reason = LOSS_BLACKOUT;
        game.loss_threat = -1;
        game.power_out = true;
        game.survived = game.night_seconds * 0.72f;
        game.power = 0.0f;
        game.jumpscare_timer = 0.18f;
        break;
    }
    return game;
}

static bool input_expect(bool condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "input_test=fail check=%s\n", name);
        return false;
    }
    return true;
}

static int run_input_test(void)
{
    Audio audio = { 0 };
    SettingsData settings;
    init_settings(&settings);
    int logical_x = 0;
    int logical_y = 0;

    window_size_to_logical(1200, WINDOW_H, 120, 270, &logical_x, &logical_y);
    if (!input_expect(logical_x == 0 && logical_y == 270, "logical_mouse_horizontal_letterbox_left_edge")) return EXIT_FAILURE;
    window_size_to_logical(1200, WINDOW_H, 600, 270, &logical_x, &logical_y);
    if (!input_expect(logical_x == 480 && logical_y == 270, "logical_mouse_horizontal_letterbox_center")) return EXIT_FAILURE;
    window_size_to_logical(WINDOW_W, 720, 480, 90, &logical_x, &logical_y);
    if (!input_expect(logical_x == 480 && logical_y == 0, "logical_mouse_vertical_letterbox_top_edge")) return EXIT_FAILURE;
    window_size_to_logical(WINDOW_W, 720, 480, 630, &logical_x, &logical_y);
    if (!input_expect(logical_x == 480 && logical_y == 540, "logical_mouse_vertical_letterbox_bottom_edge")) return EXIT_FAILURE;

    Game game = {
        .night = 2,
        .unlocked_night = 3,
        .best_night = 2,
        .night_seconds = 20.0f,
    };
    for (int i = 0; i < MAX_NIGHT; i++) {
        game.best_power[i] = -1;
    }
    init_game(&game);
    if (!input_expect(game.best_night == 2, "title_best_night_loaded")) return EXIT_FAILURE;
    if (!input_expect(game.best_power[0] == -1, "title_best_power_unset")) return EXIT_FAILURE;

    bool show_help = false;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_F1);
    if (!input_expect(show_help, "keyboard_help_open")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_RETURN);
    if (!input_expect(!show_help && game.mode == MODE_TITLE, "keyboard_help_consumes_start")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_LEFT);
    if (!input_expect(game.night == 1, "keyboard_title_left")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_RIGHT);
    if (!input_expect(game.night == 2, "keyboard_title_right")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_3);
    if (!input_expect(game.night == 3, "keyboard_title_number")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_4);
    if (!input_expect(game.night == 3, "keyboard_locked_night")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_e);
    if (!input_expect(game.mode == MODE_TITLE, "keyboard_extras_locked")) return EXIT_FAILURE;
    game.story_cleared = true;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_e);
    if (!input_expect(game.mode == MODE_EXTRAS, "keyboard_extras_open")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_ESCAPE);
    if (!input_expect(game.mode == MODE_TITLE, "keyboard_extras_escape")) return EXIT_FAILURE;
    game.story_cleared = false;
    game.unlocked_night = 6;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_6);
    if (!input_expect(game.night == 6, "keyboard_title_night_six")) return EXIT_FAILURE;
    game.unlocked_night = 3;
    game.night = 3;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_c);
    if (!input_expect(game_is_custom_night(&game) && game.custom_ai[0] == 5, "keyboard_custom_toggle_on")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_RIGHT);
    if (!input_expect(game.custom_ai[0] == 6, "keyboard_custom_ai_increase")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_DOWN);
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_LEFT);
    if (!input_expect(game.selected_custom_threat == 1 && game.custom_ai[1] == 4, "keyboard_custom_select_adjust")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_c);
    if (!input_expect(!game_is_custom_night(&game), "keyboard_custom_toggle_off")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_c);
    if (!input_expect(game_is_custom_night(&game) && game.custom_ai[0] == 6 && game.custom_ai[1] == 4, "keyboard_custom_toggle_preserves_ai")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_c);
    if (!input_expect(!game_is_custom_night(&game), "keyboard_custom_toggle_off_after_preserve")) return EXIT_FAILURE;
    for (int i = 0; i < THREAT_COUNT; i++) {
        set_custom_ai(&game, i, 0);
    }
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_c);
    if (!input_expect(game_is_custom_night(&game) && game.custom_ai[0] == 0 && game.custom_ai[1] == 0, "keyboard_custom_toggle_preserves_all_zero_ai")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_c);
    if (!input_expect(!game_is_custom_night(&game), "keyboard_custom_toggle_off_after_all_zero")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_RETURN);
    if (!input_expect(game.mode == MODE_PLAYING, "keyboard_start")) return EXIT_FAILURE;
    if (!input_expect(game.best_night == 2, "best_night_survives_start")) return EXIT_FAILURE;
    if (!input_expect(game_phone_call_active(&game), "keyboard_phone_call_active")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_c);
    if (!input_expect(game.call_muted, "keyboard_phone_mute")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_m);
    if (!input_expect(settings.muted && audio.muted, "keyboard_mute_on")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_m);
    if (!input_expect(!settings.muted && !audio.muted, "keyboard_mute_off")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_EQUALS);
    if (!input_expect(settings.window_scale == 2, "keyboard_scale_increase")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_MINUS);
    if (!input_expect(settings.window_scale == 1, "keyboard_scale_decrease")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_F11);
    if (!input_expect(settings.fullscreen, "keyboard_fullscreen_on")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_EQUALS);
    if (!input_expect(settings.window_scale == 1, "keyboard_scale_ignored_fullscreen")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_F11);
    if (!input_expect(!settings.fullscreen, "keyboard_fullscreen_off")) return EXIT_FAILURE;

    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_a);
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_d);
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_q);
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_e);
    if (!input_expect(game.left_door && game.right_door && game.left_light && game.right_light, "keyboard_office_controls")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_SPACE);
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_6);
    if (!input_expect(game.monitor && game.selected_camera == 5, "keyboard_camera_controls")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_l);
    if (!input_expect(game_audio_lure_active(&game), "keyboard_audio_lure")) return EXIT_FAILURE;
    game.audio_lure_timer = 0.0f;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_p);
    if (!input_expect(game.mode == MODE_PAUSED, "keyboard_pause")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_t);
    if (!input_expect(game.mode == MODE_TITLE && game.night == 3 && game.unlocked_night == 3, "keyboard_pause_title")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_RETURN);
    if (!input_expect(game.mode == MODE_PLAYING, "keyboard_restart_after_title_return")) return EXIT_FAILURE;

    handle_mouse_click(&game, &audio, &show_help, 330, 230);
    if (!input_expect(game.mode == MODE_PLAYING, "mouse_pause_resume")) return EXIT_FAILURE;
    game.mode = MODE_PAUSED;
    handle_mouse_click(&game, &audio, &show_help, 360, 350);
    if (!input_expect(game.mode == MODE_TITLE, "mouse_pause_title")) return EXIT_FAILURE;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_RETURN);
    if (!input_expect(game.mode == MODE_PLAYING, "keyboard_restart_after_mouse_title_return")) return EXIT_FAILURE;
    game.call_muted = false;
    handle_mouse_click(&game, &audio, &show_help, 620, 125);
    if (!input_expect(game.call_muted, "mouse_phone_mute")) return EXIT_FAILURE;
    game.monitor = false;
    handle_mouse_click(&game, &audio, &show_help, 420, 400);
    if (!input_expect(game.monitor, "mouse_monitor_open")) return EXIT_FAILURE;
    handle_mouse_click(&game, &audio, &show_help, 772, 55 + (4 * 38));
    if (!input_expect(game.selected_camera == 4, "mouse_camera_select")) return EXIT_FAILURE;
    handle_mouse_click(&game, &audio, &show_help, 780, 295);
    if (!input_expect(game_audio_lure_active(&game), "mouse_audio_lure")) return EXIT_FAILURE;
    game.audio_lure_timer = 0.0f;
    handle_mouse_click(&game, &audio, &show_help, 42, 458);
    if (!input_expect(!game.monitor, "mouse_camera_close")) return EXIT_FAILURE;
    game.power_out = true;
    game.left_door = false;
    game.right_door = false;
    game.left_light = false;
    game.right_light = false;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_a);
    handle_mouse_click(&game, &audio, &show_help, 420, 400);
    if (!input_expect(!game.left_door && !game.monitor, "blackout_keyboard_mouse_inert")) return EXIT_FAILURE;
    game.mode = MODE_WIN;
    game.night = 1;
    game.unlocked_night = 2;
    handle_key(&game, &audio, NULL, &settings, &show_help, SDLK_RETURN);
    if (!input_expect(game.mode == MODE_PLAYING && game.night == 2, "keyboard_win_next_night")) return EXIT_FAILURE;
    game.mode = MODE_WIN;
    game.night = 2;
    game.unlocked_night = 3;
    handle_mouse_click(&game, &audio, &show_help, 330, 410);
    if (!input_expect(game.mode == MODE_PLAYING && game.night == 3, "mouse_win_next_night")) return EXIT_FAILURE;

    game.mode = MODE_TITLE;
    game.story_cleared = true;
    handle_mouse_click(&game, &audio, &show_help, 700, 292);
    if (!input_expect(game.mode == MODE_EXTRAS, "mouse_extras_open")) return EXIT_FAILURE;
    handle_mouse_click(&game, &audio, &show_help, 360, 470);
    if (!input_expect(game.mode == MODE_TITLE, "mouse_extras_back")) return EXIT_FAILURE;

    game = (Game){
        .night = 1,
        .unlocked_night = 3,
        .night_seconds = 20.0f,
    };
    init_game(&game);
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    if (!input_expect(game.night == 2, "controller_title_dpad")) return EXIT_FAILURE;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    if (!input_expect(game.mode == MODE_TITLE, "controller_extras_locked")) return EXIT_FAILURE;
    game.custom_challenge_cleared = true;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    if (!input_expect(game.mode == MODE_EXTRAS, "controller_extras_open")) return EXIT_FAILURE;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_B);
    if (!input_expect(game.mode == MODE_TITLE, "controller_extras_back")) return EXIT_FAILURE;
    game.custom_challenge_cleared = false;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_X);
    if (!input_expect(game_is_custom_night(&game), "controller_custom_toggle_on")) return EXIT_FAILURE;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    if (!input_expect(game.custom_ai[0] == 6, "controller_custom_ai_increase")) return EXIT_FAILURE;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    if (!input_expect(game.selected_custom_threat == 1, "controller_custom_select")) return EXIT_FAILURE;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_X);
    if (!input_expect(!game_is_custom_night(&game), "controller_custom_toggle_off")) return EXIT_FAILURE;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_X);
    if (!input_expect(game_is_custom_night(&game) && game.custom_ai[0] == 6, "controller_custom_toggle_preserves_ai")) return EXIT_FAILURE;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_X);
    if (!input_expect(!game_is_custom_night(&game), "controller_custom_toggle_off_after_preserve")) return EXIT_FAILURE;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_A);
    if (!input_expect(game.mode == MODE_PLAYING, "controller_start")) return EXIT_FAILURE;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_LEFTSTICK);
    if (!input_expect(game.call_muted, "controller_phone_mute")) return EXIT_FAILURE;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_X);
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_Y);
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    if (!input_expect(game.left_door && game.right_door && game.left_light && game.right_light, "controller_office_controls")) return EXIT_FAILURE;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_A);
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    if (!input_expect(game.monitor && game.selected_camera == 1, "controller_camera_select")) return EXIT_FAILURE;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_LEFTSTICK);
    if (!input_expect(game_audio_lure_active(&game), "controller_audio_lure")) return EXIT_FAILURE;
    game.audio_lure_timer = 0.0f;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_B);
    if (!input_expect(!game.monitor, "controller_camera_close")) return EXIT_FAILURE;
    game.power_out = true;
    game.left_door = false;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_X);
    if (!input_expect(!game.left_door, "blackout_controller_inert")) return EXIT_FAILURE;
    game.power_out = false;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_START);
    if (!input_expect(game.mode == MODE_PAUSED, "controller_pause")) return EXIT_FAILURE;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_B);
    if (!input_expect(game.mode == MODE_TITLE, "controller_pause_title")) return EXIT_FAILURE;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_A);
    if (!input_expect(game.mode == MODE_PLAYING, "controller_restart_after_title_return")) return EXIT_FAILURE;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_START);
    if (!input_expect(game.mode == MODE_PAUSED, "controller_pause_after_title_return")) return EXIT_FAILURE;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_A);
    if (!input_expect(game.mode == MODE_PLAYING, "controller_resume")) return EXIT_FAILURE;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_BACK);
    if (!input_expect(show_help, "controller_help_open")) return EXIT_FAILURE;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_A);
    if (!input_expect(!show_help, "controller_help_close")) return EXIT_FAILURE;
    game.mode = MODE_WIN;
    game.night = 2;
    game.unlocked_night = 3;
    handle_controller_button(&game, &audio, &show_help, SDL_CONTROLLER_BUTTON_A);
    if (!input_expect(game.mode == MODE_PLAYING && game.night == 3, "controller_win_next_night")) return EXIT_FAILURE;

    printf("input_test=pass\n");
    return EXIT_SUCCESS;
}

static bool settings_expect(bool condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "settings_test=fail check=%s\n", name);
        return false;
    }
    return true;
}

static int run_settings_test(const char *settings_path)
{
    if (settings_path == NULL || settings_path[0] == '\0') {
        fprintf(stderr, "settings_test=fail reason=missing_settings_path\n");
        return EXIT_FAILURE;
    }

    SettingsData saved;
    init_settings(&saved);
    saved.window_scale = 2;
    saved.fullscreen = true;
    saved.muted = true;
    saved.custom_ai_configured = true;
    for (int i = 0; i < THREAT_COUNT; i++) {
        saved.custom_ai[i] = 0;
    }
    if (!save_settings_file(settings_path, &saved)) {
        fprintf(stderr, "settings_test=fail reason=write_initial path=%s\n", settings_path);
        return EXIT_FAILURE;
    }

    SettingsData loaded;
    if (!load_settings_file(settings_path, &loaded)) {
        fprintf(stderr, "settings_test=fail reason=load_initial path=%s\n", settings_path);
        return EXIT_FAILURE;
    }
    if (!settings_expect(loaded.custom_ai_configured, "load_preserves_configured_flag")) return EXIT_FAILURE;
    for (int i = 0; i < THREAT_COUNT; i++) {
        if (!settings_expect(loaded.custom_ai[i] == 0, "load_preserves_all_zero_ai")) return EXIT_FAILURE;
    }

    Game game = {
        .night = 1,
        .unlocked_night = 1,
        .night_seconds = 20.0f,
        .custom_ai_configured = loaded.custom_ai_configured,
    };
    for (int i = 0; i < THREAT_COUNT; i++) {
        game.custom_ai[i] = loaded.custom_ai[i];
    }
    init_game(&game);

    Audio audio = { 0 };
    toggle_custom_night(&game, &audio);
    if (!settings_expect(game_is_custom_night(&game), "toggle_enters_custom_night")) return EXIT_FAILURE;
    for (int i = 0; i < THREAT_COUNT; i++) {
        if (!settings_expect(game.custom_ai[i] == 0, "toggle_preserves_configured_zero_ai")) return EXIT_FAILURE;
    }

    for (int i = 0; i < THREAT_COUNT; i++) {
        set_custom_ai(&game, i, (i + 1) * 4);
        saved.custom_ai[i] = game.custom_ai[i];
    }
    saved.custom_ai_configured = game.custom_ai_configured;
    saved.fullscreen = false;
    saved.muted = false;
    if (!save_settings_file(settings_path, &saved)) {
        fprintf(stderr, "settings_test=fail reason=write_updated path=%s\n", settings_path);
        return EXIT_FAILURE;
    }
    if (!load_settings_file(settings_path, &loaded)) {
        fprintf(stderr, "settings_test=fail reason=load_updated path=%s\n", settings_path);
        return EXIT_FAILURE;
    }
    if (!settings_expect(loaded.custom_ai_configured, "updated_preserves_configured_flag")) return EXIT_FAILURE;
    for (int i = 0; i < THREAT_COUNT; i++) {
        if (!settings_expect(loaded.custom_ai[i] == (i + 1) * 4, "updated_preserves_ai")) return EXIT_FAILURE;
    }
    if (!settings_expect(!loaded.fullscreen && !loaded.muted, "updated_preserves_booleans")) return EXIT_FAILURE;

    printf("settings_test=pass path=%s\n", settings_path);
    return EXIT_SUCCESS;
}

static void print_help(const char *program)
{
    printf("Night Shift %s\n", NIGHTSHIFT_VERSION);
    printf("Usage: %s [options]\n\n", program);
    printf("Options:\n");
    printf("  --help                 Show this help text.\n");
    printf("  --version              Show the game version.\n");
    printf("  --fast-night           Use a 20 second night for testing.\n");
    printf("  --night=N              Select/unlock story night 1..6 for testing.\n");
    printf("  --night-seconds=N      Set custom night length, minimum 5.\n");
    printf("  --save=PATH            Use a custom progress save file.\n");
    printf("  --reset-save           Reset progress in the selected save file before launch.\n");
    printf("  --settings=PATH        Use a custom settings file.\n");
    printf("  --scale=1..4           Start windowed at an integer scale.\n");
    printf("  --fullscreen           Start in fullscreen desktop mode.\n");
    printf("  --mute                 Start with procedural audio muted.\n");
    printf("  --custom-night=R,V,S,E Start Custom Night with Rust, Volt, Skitr, Echo AI levels 0..20.\n");
    printf("  --render-test          Draw one SDL frame, read pixels, and exit.\n");
    printf("  --audio-test           Queue every procedural sound cue using SDL audio, then exit.\n");
    printf("  --screenshot=PATH      Save one deterministic BMP frame and exit.\n");
    printf("  --screenshot-scene=ID  Use title, title-cleared, extras, office, camera, camera-stage, camera-dining, camera-hall, camera-backstage, win, loss-rust, loss-volt, loss-skitr, loss-echo, or blackout.\n");
    printf("  --input-test           Exercise keyboard, mouse, and controller handlers, then exit.\n");
    printf("  --settings-test        Exercise settings load/save through --settings=PATH, then exit.\n");
    printf("  --simulate[=defended]  Run a no-SDL defended simulation; exits 0 on win.\n");
    printf("  --simulate=idle        Run a no-SDL idle simulation; exits 0 on loss.\n\n");
    printf("Default save/settings files use SDL's per-user preference directory when available.\n");
    printf("Controls: F1 help, click UI, E extras after clear, C custom night/title or phone mute/play, L camera audio, 1-6 night/camera select, A/D doors, Q/E lights, Space cameras, T title from pause, M audio mute, F11 fullscreen.\n");
    printf("Controller: A start/cameras, RB extras after clear, X custom night on title, left stick phone mute/audio lure, B close/title/quit, X/Y doors, LB/RB lights, D-pad select, Start pause, Back/Guide help.\n");
}

static void print_version(void)
{
    printf("Night Shift %s\n", NIGHTSHIFT_VERSION);
}

static char *make_pref_file_path(const char *filename)
{
    char *base = SDL_GetPrefPath("NightShift", "NightShift");
    if (base == NULL) {
        return NULL;
    }

    size_t base_len = strlen(base);
    size_t file_len = strlen(filename);
    char *path = malloc(base_len + file_len + 1);
    if (path == NULL) {
        SDL_free(base);
        return NULL;
    }

    memcpy(path, base, base_len);
    memcpy(path + base_len, filename, file_len + 1);
    SDL_free(base);
    return path;
}

static bool parse_custom_night(const char *value, int ai[THREAT_COUNT])
{
    const char *at = value;
    for (int i = 0; i < THREAT_COUNT; i++) {
        ai[i] = 0;
    }
    for (int i = 0; i < THREAT_COUNT; i++) {
        char *end = NULL;
        errno = 0;
        long parsed = strtol(at, &end, 10);
        if (at[0] == '\0' || end == at || errno != 0 || parsed < MIN_AI_LEVEL || parsed > MAX_AI_LEVEL) {
            return false;
        }
        ai[i] = (int)parsed;
        if (i == THREAT_COUNT - 1) {
            return *end == '\0';
        }
        if (*end != ',') {
            return false;
        }
        at = end + 1;
    }
    return false;
}

static RunConfig parse_args(int argc, char **argv)
{
    RunConfig config = {
        .save_path = NULL,
        .settings_path = NULL,
        .night_seconds = (float)DEFAULT_NIGHT_SECONDS,
        .story_night_override = 0,
        .scale_override = 0,
        .custom_night = false,
        .custom_ai = { 0, 0, 0, 0 },
        .fullscreen_override = false,
        .mute_override = false,
        .reset_save = false,
        .show_help = false,
        .show_version = false,
        .render_test = false,
        .audio_test = false,
        .screenshot_path = NULL,
        .screenshot_scene = SHOT_TITLE,
        .input_test = false,
        .settings_test = false,
        .arg_error = false,
        .arg_error_message = NULL,
        .arg_error_value = NULL,
        .sim_mode = SIM_NONE,
    };

    for (int i = 1; i < argc; i++) {
        if (config.arg_error) {
            break;
        }

        if (strcmp(argv[i], "--fast-night") == 0) {
            config.night_seconds = 20.0f;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            config.show_help = true;
        } else if (strcmp(argv[i], "--version") == 0) {
            config.show_version = true;
        } else if (strcmp(argv[i], "--render-test") == 0) {
            config.render_test = true;
        } else if (strcmp(argv[i], "--audio-test") == 0) {
            config.audio_test = true;
        } else if (strncmp(argv[i], "--custom-night=", 15) == 0) {
            const char *value = argv[i] + 15;
            if (parse_custom_night(value, config.custom_ai)) {
                config.custom_night = true;
            } else {
                config.arg_error = true;
                config.arg_error_message = "invalid --custom-night value, expected R,V,S,E AI levels from 0 to 20";
                config.arg_error_value = value;
            }
        } else if (strncmp(argv[i], "--screenshot=", 13) == 0) {
            config.screenshot_path = argv[i] + 13;
            if (config.screenshot_path[0] == '\0') {
                config.arg_error = true;
                config.arg_error_message = "invalid --screenshot value, expected a path";
                config.arg_error_value = argv[i];
            }
        } else if (strncmp(argv[i], "--screenshot-scene=", 19) == 0) {
            const char *value = argv[i] + 19;
            if (!parse_screenshot_scene(value, &config.screenshot_scene)) {
                config.arg_error = true;
                config.arg_error_message = "invalid --screenshot-scene value";
                config.arg_error_value = value;
            }
        } else if (strcmp(argv[i], "--input-test") == 0) {
            config.input_test = true;
        } else if (strcmp(argv[i], "--settings-test") == 0) {
            config.settings_test = true;
        } else if (strncmp(argv[i], "--night=", 8) == 0) {
            const char *value = argv[i] + 8;
            char *end = NULL;
            errno = 0;
            long parsed = strtol(value, &end, 10);
            if (value[0] != '\0' && end != value && *end == '\0' && errno == 0 &&
                parsed >= MIN_NIGHT && parsed <= MAX_NIGHT) {
                config.story_night_override = (int)parsed;
            } else {
                config.arg_error = true;
                config.arg_error_message = "invalid --night value, expected an integer from 1 to 6";
                config.arg_error_value = value;
            }
        } else if (strncmp(argv[i], "--night-seconds=", 16) == 0) {
            const char *value = argv[i] + 16;
            char *end = NULL;
            errno = 0;
            float parsed = strtof(value, &end);
            if (value[0] != '\0' && end != value && *end == '\0' && errno == 0 &&
                isfinite(parsed) && parsed >= 5.0f) {
                config.night_seconds = parsed;
            } else {
                config.arg_error = true;
                config.arg_error_message = "invalid --night-seconds value, expected a number >= 5";
                config.arg_error_value = value;
            }
        } else if (strncmp(argv[i], "--save=", 7) == 0) {
            config.save_path = argv[i] + 7;
            if (config.save_path[0] == '\0') {
                config.arg_error = true;
                config.arg_error_message = "invalid --save value, expected a path";
                config.arg_error_value = argv[i];
            }
        } else if (strcmp(argv[i], "--reset-save") == 0) {
            config.reset_save = true;
        } else if (strncmp(argv[i], "--settings=", 11) == 0) {
            config.settings_path = argv[i] + 11;
            if (config.settings_path[0] == '\0') {
                config.arg_error = true;
                config.arg_error_message = "invalid --settings value, expected a path";
                config.arg_error_value = argv[i];
            }
        } else if (strncmp(argv[i], "--scale=", 8) == 0) {
            const char *value = argv[i] + 8;
            char *end = NULL;
            errno = 0;
            long parsed = strtol(value, &end, 10);
            if (value[0] != '\0' && end != value && *end == '\0' && errno == 0 && parsed >= 1 && parsed <= 4) {
                config.scale_override = (int)parsed;
            } else {
                config.arg_error = true;
                config.arg_error_message = "invalid --scale value, expected an integer from 1 to 4";
                config.arg_error_value = value;
            }
        } else if (strcmp(argv[i], "--fullscreen") == 0) {
            config.fullscreen_override = true;
        } else if (strcmp(argv[i], "--mute") == 0) {
            config.mute_override = true;
        } else if (strcmp(argv[i], "--simulate") == 0 || strcmp(argv[i], "--simulate=defended") == 0) {
            config.sim_mode = SIM_DEFENDED;
        } else if (strcmp(argv[i], "--simulate=idle") == 0) {
            config.sim_mode = SIM_IDLE;
        } else if (strncmp(argv[i], "--simulate=", 11) == 0) {
            config.arg_error = true;
            config.arg_error_message = "invalid --simulate value, expected defended or idle";
            config.arg_error_value = argv[i] + 11;
        } else {
            config.arg_error = true;
            config.arg_error_message = "unknown option";
            config.arg_error_value = argv[i];
        }
    }
    if (!config.arg_error && config.custom_night && config.story_night_override > 0) {
        config.arg_error = true;
        config.arg_error_message = "invalid option combination, --night is only for story mode";
        config.arg_error_value = "--custom-night";
    }
    return config;
}

static void apply_story_night_config(Game *game, const RunConfig *config)
{
    if (config->story_night_override <= 0 || config->custom_night) {
        return;
    }
    game->unlocked_night = config->story_night_override;
    if (game->best_night > game->unlocked_night) {
        game->best_night = game->unlocked_night;
    }
    game->night = config->story_night_override;
}

static void apply_custom_night_config(Game *game, const RunConfig *config)
{
    if (!config->custom_night) {
        return;
    }
    game->custom_night = true;
    for (int i = 0; i < THREAT_COUNT; i++) {
        set_custom_ai(game, i, config->custom_ai[i]);
    }
    game->selected_custom_threat = 0;
}

static int run_simulation(const RunConfig *config)
{
    Game game = { 0 };
    game.night_seconds = config->night_seconds;
    if (config->story_night_override > 0) {
        game.unlocked_night = config->story_night_override;
        game.night = config->story_night_override;
    }
    init_game(&game);
    apply_custom_night_config(&game, config);
    start_night(&game);

    float elapsed = 0.0f;
    const float dt = 0.25f;
    while (game.mode == MODE_PLAYING && elapsed < game.night_seconds + 120.0f) {
        if (config->sim_mode == SIM_DEFENDED) {
            game.left_door = true;
            game.right_door = true;
            if (game.threats[2].active && game.threats[2].scene == SCENE_VENT) {
                game.monitor = true;
                game.selected_camera = 5;
            } else if (game.threats[3].active && game.threats[3].route_pos >= game.threats[3].route_len - 2) {
                game.monitor = true;
                game.selected_camera = 1;
                trigger_audio_lure(&game);
            } else {
                game.monitor = false;
            }
        }
        update_game(&game, dt);
        elapsed += dt;
    }

    const char *mode = game.mode == MODE_WIN ? "win" : game.mode == MODE_LOSS ? "loss" : "timeout";
    printf("simulation=%s night=%d mode=%s survived=%.2f power=%.2f hour=%d\n",
        config->sim_mode == SIM_IDLE ? "idle" : "defended",
        game.night,
        mode,
        game.survived,
        game.power,
        game_hour(&game));

    if (config->sim_mode == SIM_DEFENDED) {
        return game.mode == MODE_WIN ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (config->sim_mode == SIM_IDLE) {
        return game.mode == MODE_LOSS ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    return EXIT_FAILURE;
}

int main(int argc, char **argv)
{
    RunConfig config = parse_args(argc, argv);
    if (config.arg_error) {
        fprintf(stderr, "error: %s", config.arg_error_message);
        if (config.arg_error_value != NULL) {
            fprintf(stderr, ": %s", config.arg_error_value);
        }
        fprintf(stderr, "\nRun '%s --help' for usage.\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (config.show_help) {
        print_help(argv[0]);
        return EXIT_SUCCESS;
    }
    if (config.show_version) {
        print_version();
        return EXIT_SUCCESS;
    }
    if (config.sim_mode != SIM_NONE) {
        return run_simulation(&config);
    }
    if (config.input_test) {
        return run_input_test();
    }
    if (config.settings_test) {
        return run_settings_test(config.settings_path);
    }

    SDL_SetMainReady();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    char *default_save_path = config.save_path == NULL ? make_pref_file_path("nightshift.save") : NULL;
    char *default_settings_path = config.settings_path == NULL ? make_pref_file_path("nightshift.cfg") : NULL;
    const char *save_path = config.save_path != NULL ? config.save_path :
        default_save_path != NULL ? default_save_path : "nightshift.save";
    const char *settings_path = config.settings_path != NULL ? config.settings_path :
        default_settings_path != NULL ? default_settings_path : "nightshift.cfg";

    SettingsData settings;
    init_settings(&settings);
    if (!load_settings_file(settings_path, &settings)) {
        fprintf(stderr, "warning: could not load settings file '%s'\n", settings_path);
    }
    if (config.scale_override > 0) {
        settings.window_scale = config.scale_override;
    }
    if (config.fullscreen_override) {
        settings.fullscreen = true;
    }
    if (config.mute_override) {
        settings.muted = true;
    }

    uint32_t window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (settings.fullscreen) {
        window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    SDL_Window *window = SDL_CreateWindow("Night Shift",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W * settings.window_scale, WINDOW_H * settings.window_scale,
        window_flags);
    if (window == NULL) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        free(default_save_path);
        free(default_settings_path);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    apply_window_icon(window);

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (renderer == NULL) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        free(default_save_path);
        free(default_settings_path);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderSetLogicalSize(renderer, WINDOW_W, WINDOW_H);
    load_art_assets(renderer);

    if (config.audio_test) {
        int result = run_audio_test();
        unload_art_assets();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        free(default_save_path);
        free(default_settings_path);
        SDL_Quit();
        return result;
    }

    if (config.render_test) {
        Game game = { 0 };
        game.night_seconds = config.night_seconds;
        apply_story_night_config(&game, &config);
        init_game(&game);
        apply_custom_night_config(&game, &config);
        int result = run_render_test(renderer, &game);
        unload_art_assets();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        free(default_save_path);
        free(default_settings_path);
        SDL_Quit();
        return result;
    }

    if (config.screenshot_path != NULL) {
        Game game = screenshot_game(config.screenshot_scene, config.night_seconds);
        apply_custom_night_config(&game, &config);
        int result = save_screenshot(renderer, &game, config.screenshot_path);
        unload_art_assets();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        free(default_save_path);
        free(default_settings_path);
        SDL_Quit();
        return result;
    }

    Audio audio;
    if (!audio_init(&audio)) {
        fprintf(stderr, "warning: audio disabled: %s\n", SDL_GetError());
    }
    audio_set_muted(&audio, settings.muted);
    SDL_GameController *controller = open_first_controller();

    Game game = { 0 };
    SaveData save;
    init_save(&save);
    game.night_seconds = config.night_seconds;
    if (config.reset_save && !reset_save_file(save_path)) {
        fprintf(stderr, "warning: could not reset save file '%s'\n", save_path);
    }
    if (!load_save_file(save_path, &save)) {
        fprintf(stderr, "warning: could not load save file '%s'\n", save_path);
    }
    game.unlocked_night = save.unlocked_night;
    game.best_night = save.best_night;
    game.story_cleared = save.story_cleared;
    game.custom_challenge_cleared = save.custom_challenge_cleared;
    for (int i = 0; i < MAX_NIGHT; i++) {
        game.best_power[i] = save.best_power[i];
    }
    for (int i = 0; i < THREAT_COUNT; i++) {
        game.custom_ai[i] = settings.custom_ai[i];
    }
    game.custom_ai_configured = settings.custom_ai_configured;
    game.night = save.unlocked_night;
    apply_story_night_config(&game, &config);
    init_game(&game);
    apply_custom_night_config(&game, &config);
    bool show_help = false;
    uint64_t last_counter = SDL_GetPerformanceCounter();

    while (game.running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                game.running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                handle_key(&game, &audio, window, &settings, &show_help, event.key.keysym.sym);
            } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                handle_controller_button(&game, &audio, &show_help, (SDL_GameControllerButton)event.cbutton.button);
            } else if (event.type == SDL_CONTROLLERDEVICEADDED) {
                if (controller == NULL) {
                    controller = open_first_controller();
                }
            } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
                maybe_close_removed_controller(&controller, event.cdevice.which);
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                int logical_x;
                int logical_y;
                window_to_logical(window, event.button.x, event.button.y, &logical_x, &logical_y);
                handle_mouse_click(&game, &audio, &show_help, logical_x, logical_y);
            }
        }

        uint64_t now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - last_counter) / (double)SDL_GetPerformanceFrequency());
        last_counter = now;
        dt = clampf(dt, 0.0f, 0.05f);

        GameMode previous_mode = game.mode;
        update_game(&game, dt);
        if (previous_mode == MODE_PLAYING && game.mode == MODE_LOSS) {
            audio_play(&audio, SOUND_LOSS);
        }
        if (previous_mode == MODE_PLAYING && game.mode == MODE_WIN) {
            audio_play(&audio, SOUND_WIN);
            record_night_result(&game, &save);
            if (!save_file(save_path, &save)) {
                fprintf(stderr, "warning: could not write save file '%s'\n", save_path);
            }
        }
        render_game(renderer, &game, &settings, show_help);
        SDL_RenderPresent(renderer);
    }

    for (int i = 0; i < THREAT_COUNT; i++) {
        settings.custom_ai[i] = game.custom_ai[i];
    }
    settings.custom_ai_configured = game.custom_ai_configured;
    if (!save_settings_file(settings_path, &settings)) {
        fprintf(stderr, "warning: could not write settings file '%s'\n", settings_path);
    }
    close_controller(&controller);
    audio_shutdown(&audio);
    unload_art_assets();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    free(default_save_path);
    free(default_settings_path);
    SDL_Quit();
    return EXIT_SUCCESS;
}
