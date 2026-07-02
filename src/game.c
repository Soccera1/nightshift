#include "game.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static int clamp_night(int night)
{
    if (night < MIN_NIGHT) {
        return MIN_NIGHT;
    }
    if (night > MAX_NIGHT) {
        return MAX_NIGHT;
    }
    return night;
}

static int clamp_ai_level(int ai_level)
{
    if (ai_level < MIN_AI_LEVEL) {
        return MIN_AI_LEVEL;
    }
    if (ai_level > MAX_AI_LEVEL) {
        return MAX_AI_LEVEL;
    }
    return ai_level;
}

static int clamp_window_scale(int scale)
{
    if (scale < 1) {
        return 1;
    }
    if (scale > 4) {
        return 4;
    }
    return scale;
}

static bool parse_bool_value(int value, bool *out)
{
    if (value != 0 && value != 1) {
        return false;
    }
    *out = value != 0;
    return true;
}

typedef bool (*WriteFileCallback)(FILE *file, const void *data);

static char *temporary_path_for(const char *path)
{
    const char *suffix = ".tmp";
    size_t path_len = strlen(path);
    size_t suffix_len = strlen(suffix);
    char *temporary = malloc(path_len + suffix_len + 1);
    if (temporary == NULL) {
        return NULL;
    }
    memcpy(temporary, path, path_len);
    memcpy(temporary + path_len, suffix, suffix_len + 1);
    return temporary;
}

static bool replace_file(const char *temporary, const char *path)
{
#ifdef _WIN32
    return MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(temporary, path) == 0;
#endif
}

static bool write_file_atomic(const char *path, WriteFileCallback write_callback, const void *data)
{
    char *temporary = temporary_path_for(path);
    if (temporary == NULL) {
        return false;
    }

    FILE *file = fopen(temporary, "w");
    if (file == NULL) {
        free(temporary);
        return false;
    }

    bool ok = write_callback(file, data);
    if (fclose(file) != 0) {
        ok = false;
    }
    if (ok && !replace_file(temporary, path)) {
        ok = false;
    }
    if (!ok) {
        remove(temporary);
    }
    free(temporary);
    return ok;
}

static bool parse_key_value_line(const char *line, char *key, size_t key_size, int *value)
{
    const char *separator = strchr(line, '=');
    if (separator == NULL) {
        return false;
    }

    size_t key_len = (size_t)(separator - line);
    if (key_len == 0 || key_len >= key_size) {
        return false;
    }
    memcpy(key, line, key_len);
    key[key_len] = '\0';

    const char *value_text = separator + 1;
    char *end = NULL;
    errno = 0;
    long parsed = strtol(value_text, &end, 10);
    if (value_text == end || errno != 0 || parsed < INT_MIN || parsed > INT_MAX) {
        return false;
    }
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') {
        end++;
    }
    if (*end != '\0') {
        return false;
    }

    *value = (int)parsed;
    return true;
}

static void normalize_best_power_records(const Game *game, int best_night, int best_power[MAX_NIGHT])
{
    bool zero_initialized_unset = best_night == 0;
    if (zero_initialized_unset) {
        for (int i = 0; i < MAX_NIGHT; i++) {
            if (game->best_power[i] != 0) {
                zero_initialized_unset = false;
                break;
            }
        }
    }

    for (int i = 0; i < MAX_NIGHT; i++) {
        if (zero_initialized_unset) {
            best_power[i] = -1;
        } else {
            best_power[i] = game->best_power[i] >= 0 && game->best_power[i] <= 100 ? game->best_power[i] : -1;
        }
    }
}

const char *scene_name(Scene scene)
{
    switch (scene) {
    case SCENE_STAGE: return "SHOW STAGE";
    case SCENE_DINING: return "DINING AREA";
    case SCENE_LEFT_HALL: return "LEFT HALL";
    case SCENE_RIGHT_HALL: return "RIGHT HALL";
    case SCENE_BACKSTAGE: return "BACKSTAGE";
    case SCENE_VENT: return "SERVICE VENT";
    case SCENE_OFFICE: return "OFFICE";
    }
    return "UNKNOWN";
}

Scene camera_scene(int camera)
{
    switch (camera) {
    case 0: return SCENE_STAGE;
    case 1: return SCENE_DINING;
    case 2: return SCENE_LEFT_HALL;
    case 3: return SCENE_RIGHT_HALL;
    case 4: return SCENE_BACKSTAGE;
    case 5: return SCENE_VENT;
    default: return SCENE_STAGE;
    }
}

int game_hour(const Game *game)
{
    int hour = (int)floorf((game->survived / game->night_seconds) * 6.0f);
    if (hour > 6) {
        return 6;
    }
    if (hour < 0) {
        return 0;
    }
    return hour;
}

float night_difficulty(int night)
{
    return 1.0f + ((float)(clamp_night(night) - 1) * 0.22f);
}

float ai_level_difficulty(int ai_level)
{
    return 0.35f + ((float)clamp_ai_level(ai_level) * 0.14f);
}

float night_power_drain_multiplier(int night)
{
    return 1.0f + ((float)(clamp_night(night) - 1) * 0.10f);
}

bool game_is_custom_night(const Game *game)
{
    return game->custom_night;
}

bool game_is_max_custom_night(const Game *game)
{
    if (!game_is_custom_night(game)) {
        return false;
    }
    for (int i = 0; i < THREAT_COUNT; i++) {
        if (clamp_ai_level(game->custom_ai[i]) != MAX_AI_LEVEL) {
            return false;
        }
    }
    return true;
}

bool game_phone_call_active(const Game *game)
{
    return game->mode == MODE_PLAYING && !game->power_out && !game->call_muted &&
        game->call_timer < (float)PHONE_CALL_SECONDS;
}

const char *game_phone_message(const Game *game)
{
    if (game_is_custom_night(game)) {
        return "CUSTOM NIGHT ONLINE\nAI LEVELS DRIVE THE PACE\nCAM SIX STILL HOLDS SKITR";
    }

    switch (game->night) {
    case 1:
        return "WELCOME TO NIGHT SHIFT\nDOORS STOP HALL THREATS\nSAVE POWER UNTIL SIX";
    case 2:
        return "RUST FAVORS THE LEFT HALL\nVOLT FAVORS THE RIGHT HALL\nUSE LIGHTS BEFORE DOORS";
    case 3:
        return "SKITR MOVES THROUGH VENTS\nWATCH CAM SIX TO DRIVE IT BACK\nDOORS WILL NOT STOP IT";
    case 4:
        return "ECHO FOLLOWS SOUNDS\nPLAY AUDIO FROM CAMERAS\nPULL IT AWAY FROM OFFICE";
    case 5:
        return "ALL ROUTES ARE ACTIVE\nBLOCK LATE AND WATCH THE VENT\nEVERY SECOND OF POWER COUNTS";
    default:
        return "OVERTIME PROTOCOL\nEXPECT MAX PRESSURE\nEARN THE FINAL CLEAR STAR";
    }
}

bool game_left_door_danger(const Game *game)
{
    for (int i = 0; i < THREAT_COUNT; i++) {
        const Threat *threat = &game->threats[i];
        if (threat->active && !threat->camera_repel && !threat->audio_lure && threat->left_side &&
            threat->route_pos == threat->route_len - 2 &&
            threat->scene == SCENE_LEFT_HALL) {
            return true;
        }
    }
    return false;
}

bool game_right_door_danger(const Game *game)
{
    for (int i = 0; i < THREAT_COUNT; i++) {
        const Threat *threat = &game->threats[i];
        if (threat->active && !threat->camera_repel && !threat->audio_lure && !threat->left_side &&
            threat->route_pos == threat->route_len - 2 &&
            threat->scene == SCENE_RIGHT_HALL) {
            return true;
        }
    }
    return false;
}

bool game_vent_danger(const Game *game)
{
    for (int i = 0; i < THREAT_COUNT; i++) {
        const Threat *threat = &game->threats[i];
        if (threat->active && threat->camera_repel &&
            threat->route_pos == threat->route_len - 2 &&
            threat->scene == SCENE_VENT) {
            return true;
        }
    }
    return false;
}

bool game_audio_lure_danger(const Game *game)
{
    for (int i = 0; i < THREAT_COUNT; i++) {
        const Threat *threat = &game->threats[i];
        if (threat->active && threat->audio_lure &&
            threat->route_pos == threat->route_len - 2 &&
            threat->scene != SCENE_OFFICE) {
            return true;
        }
    }
    return false;
}

bool game_audio_lure_active(const Game *game)
{
    return game->audio_lure_timer > 0.0f;
}

bool story_threat_active(int threat_index, int night)
{
    night = clamp_night(night);
    switch (threat_index) {
    case 0:
    case 1:
        return true;
    case 2:
        return night >= 3;
    case 3:
        return night >= 4;
    default:
        return false;
    }
}

static int route_scene_index(const Threat *threat, Scene scene)
{
    for (int i = 0; i < threat->route_len; i++) {
        if (threat->route[i] == scene) {
            return i;
        }
    }
    return -1;
}

static float custom_power_drain_multiplier(const Game *game)
{
    int total_ai = 0;
    for (int i = 0; i < THREAT_COUNT; i++) {
        total_ai += clamp_ai_level(game->custom_ai[i]);
    }
    float average_ai = (float)total_ai / (float)THREAT_COUNT;
    return 0.9f + ((average_ai / (float)MAX_AI_LEVEL) * 0.55f);
}

void set_custom_ai(Game *game, int threat_index, int ai_level)
{
    if (threat_index < 0 || threat_index >= THREAT_COUNT) {
        return;
    }
    game->custom_ai[threat_index] = clamp_ai_level(ai_level);
    game->custom_ai_configured = true;
}

static void apply_threat_activity(Game *game)
{
    if (game_is_custom_night(game)) {
        for (int i = 0; i < THREAT_COUNT; i++) {
            game->threats[i].active = clamp_ai_level(game->custom_ai[i]) > 0;
        }
        return;
    }

    for (int i = 0; i < THREAT_COUNT; i++) {
        game->threats[i].active = story_threat_active(i, game->night);
    }
}

bool trigger_audio_lure(Game *game)
{
    if (game->mode != MODE_PLAYING || !game->monitor || game->power_out) {
        return false;
    }

    game->audio_lure_scene = camera_scene(game->selected_camera);
    game->audio_lure_timer = 5.0f;
    return true;
}

int power_usage_level(const Game *game)
{
    if (game->power_out) {
        return 0;
    }

    int usage = 1;
    if (game->monitor) usage++;
    if (game_audio_lure_active(game)) usage++;
    if (game->left_door) usage++;
    if (game->right_door) usage++;
    if (game->left_light) usage++;
    if (game->right_light) usage++;
    if (game_is_custom_night(game)) {
        if (custom_power_drain_multiplier(game) > 1.25f) usage++;
    } else if (game->night >= 4) {
        usage++;
    }
    return usage > 6 ? 6 : usage;
}

void init_save(SaveData *save)
{
    *save = (SaveData){
        .unlocked_night = MIN_NIGHT,
        .best_night = 0,
        .story_cleared = false,
        .custom_challenge_cleared = false,
    };
    for (int i = 0; i < MAX_NIGHT; i++) {
        save->best_power[i] = -1;
    }
}

void init_settings(SettingsData *settings)
{
    *settings = (SettingsData){
        .window_scale = 1,
        .custom_ai_configured = false,
        .fullscreen = false,
        .muted = false,
    };
    for (int i = 0; i < THREAT_COUNT; i++) {
        settings->custom_ai[i] = 0;
    }
}

void init_game(Game *game)
{
    float night_seconds = game->night_seconds > 0.0f ? game->night_seconds : (float)DEFAULT_NIGHT_SECONDS;
    int unlocked_night = clamp_night(game->unlocked_night > 0 ? game->unlocked_night : MIN_NIGHT);
    int night = game->night > 0 ? clamp_night(game->night) : unlocked_night;
    int best_night = game->best_night > 0 ? clamp_night(game->best_night) : 0;
    int selected_custom_threat = game->selected_custom_threat;
    bool custom_night = game->custom_night;
    bool custom_ai_configured = game->custom_ai_configured;
    bool story_cleared = game->story_cleared;
    bool custom_challenge_cleared = game->custom_challenge_cleared;
    int best_power[MAX_NIGHT];
    normalize_best_power_records(game, best_night, best_power);
    int custom_ai[THREAT_COUNT];
    for (int i = 0; i < THREAT_COUNT; i++) {
        custom_ai[i] = clamp_ai_level(game->custom_ai[i]);
    }
    if (night > unlocked_night) {
        night = unlocked_night;
    }
    if (best_night > unlocked_night) {
        best_night = unlocked_night;
    }
    *game = (Game){
        .mode = MODE_TITLE,
        .running = true,
        .custom_night = custom_night,
        .custom_ai_configured = custom_ai_configured,
        .story_cleared = story_cleared,
        .custom_challenge_cleared = custom_challenge_cleared,
        .loss_threat = -1,
        .audio_lure_scene = SCENE_STAGE,
        .selected_camera = 0,
        .night = night,
        .unlocked_night = unlocked_night,
        .best_night = best_night,
        .selected_custom_threat = selected_custom_threat >= 0 && selected_custom_threat < THREAT_COUNT ? selected_custom_threat : 0,
        .custom_ai = { custom_ai[0], custom_ai[1], custom_ai[2], custom_ai[3] },
        .best_power = { best_power[0], best_power[1], best_power[2], best_power[3], best_power[4], best_power[5] },
        .night_seconds = night_seconds,
        .power = 100.0f,
        .threats = {
            {
                .name = "RUST",
                .scene = SCENE_STAGE,
                .route = { SCENE_STAGE, SCENE_DINING, SCENE_BACKSTAGE, SCENE_LEFT_HALL, SCENE_OFFICE },
                .route_len = 5,
                .route_pos = 0,
                .move_interval = 15.0f / night_difficulty(night),
                .left_side = true,
                .active = true,
            },
            {
                .name = "VOLT",
                .scene = SCENE_STAGE,
                .route = { SCENE_STAGE, SCENE_DINING, SCENE_RIGHT_HALL, SCENE_BACKSTAGE, SCENE_OFFICE },
                .route_len = 5,
                .route_pos = 0,
                .move_interval = 18.0f / night_difficulty(night),
                .left_side = false,
                .active = true,
            },
            {
                .name = "SKITR",
                .scene = SCENE_BACKSTAGE,
                .route = { SCENE_BACKSTAGE, SCENE_DINING, SCENE_VENT, SCENE_OFFICE },
                .route_len = 4,
                .route_pos = 0,
                .move_interval = 22.0f / night_difficulty(night),
                .camera_repel = true,
                .active = true,
            },
            {
                .name = "ECHO",
                .scene = SCENE_STAGE,
                .route = { SCENE_STAGE, SCENE_DINING, SCENE_BACKSTAGE, SCENE_RIGHT_HALL, SCENE_OFFICE },
                .route_len = 5,
                .route_pos = 0,
                .move_interval = 20.0f / night_difficulty(night),
                .audio_lure = true,
                .active = true,
            },
        },
    };
}

void start_night(Game *game)
{
    float night_seconds = game->night_seconds > 0.0f ? game->night_seconds : (float)DEFAULT_NIGHT_SECONDS;
    int night = game->night;
    int unlocked_night = game->unlocked_night;
    int best_night = game->best_night;
    int selected_custom_threat = game->selected_custom_threat;
    bool custom_night = game->custom_night;
    bool custom_ai_configured = game->custom_ai_configured;
    bool story_cleared = game->story_cleared;
    bool custom_challenge_cleared = game->custom_challenge_cleared;
    int best_power[MAX_NIGHT];
    normalize_best_power_records(game, best_night > 0 ? clamp_night(best_night) : 0, best_power);
    int custom_ai[THREAT_COUNT];
    for (int i = 0; i < THREAT_COUNT; i++) {
        custom_ai[i] = clamp_ai_level(game->custom_ai[i]);
    }
    init_game(game);
    game->night_seconds = night_seconds;
    game->unlocked_night = clamp_night(unlocked_night > 0 ? unlocked_night : MIN_NIGHT);
    game->best_night = best_night > 0 ? clamp_night(best_night) : 0;
    game->selected_custom_threat = selected_custom_threat >= 0 && selected_custom_threat < THREAT_COUNT ? selected_custom_threat : 0;
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
    if (game->best_night > game->unlocked_night) {
        game->best_night = game->unlocked_night;
    }
    game->night = clamp_night(night > 0 ? night : game->unlocked_night);
    if (game->night > game->unlocked_night) {
        game->night = game->unlocked_night;
    }
    if (game_is_custom_night(game)) {
        game->threats[0].move_interval = 15.0f / ai_level_difficulty(game->custom_ai[0]);
        game->threats[1].move_interval = 18.0f / ai_level_difficulty(game->custom_ai[1]);
        game->threats[2].move_interval = 22.0f / ai_level_difficulty(game->custom_ai[2]);
        game->threats[3].move_interval = 20.0f / ai_level_difficulty(game->custom_ai[3]);
    } else {
        game->threats[0].move_interval = 15.0f / night_difficulty(game->night);
        game->threats[1].move_interval = 18.0f / night_difficulty(game->night);
        game->threats[2].move_interval = 22.0f / night_difficulty(game->night);
        game->threats[3].move_interval = 20.0f / night_difficulty(game->night);
    }
    apply_threat_activity(game);
    game->mode = MODE_PLAYING;
}

void record_night_result(Game *game, SaveData *save)
{
    if (game->mode != MODE_WIN) {
        return;
    }
    if (game_is_custom_night(game)) {
        if (game_is_max_custom_night(game)) {
            save->custom_challenge_cleared = true;
            game->custom_challenge_cleared = true;
        }
        return;
    }
    if (game->night > save->best_night) {
        save->best_night = game->night;
    }
    if (game->night >= MIN_NIGHT && game->night <= MAX_NIGHT) {
        int index = game->night - 1;
        int power = (int)roundf(game->power);
        if (power < 0) power = 0;
        if (power > 100) power = 100;
        if (power > save->best_power[index]) {
            save->best_power[index] = power;
        }
    }
    if (game->night >= MAX_NIGHT) {
        save->story_cleared = true;
    }
    if (game->night >= save->unlocked_night && save->unlocked_night < MAX_NIGHT) {
        save->unlocked_night = game->night + 1;
    }
    save->unlocked_night = clamp_night(save->unlocked_night);
    game->unlocked_night = save->unlocked_night;
    game->best_night = save->best_night < 0 ? 0 : clamp_night(save->best_night);
    game->story_cleared = save->story_cleared;
    game->custom_challenge_cleared = save->custom_challenge_cleared;
    for (int i = 0; i < MAX_NIGHT; i++) {
        game->best_power[i] = save->best_power[i];
    }
}

static void move_threat(Game *game, int threat_index)
{
    Threat *threat = &game->threats[threat_index];

    if (!threat->active || threat->scene == SCENE_OFFICE) {
        return;
    }

    if (threat->audio_lure && game_audio_lure_active(game)) {
        int lure_pos = route_scene_index(threat, game->audio_lure_scene);
        if (lure_pos >= 0 && lure_pos < threat->route_pos) {
            threat->route_pos = lure_pos;
            threat->scene = threat->route[threat->route_pos];
            game->audio_lures++;
            game->audio_lure_timer = 0.0f;
            return;
        }
    }

    if (threat->route_pos == threat->route_len - 2 && threat->camera_repel) {
        bool watched = game->monitor && camera_scene(game->selected_camera) == threat->scene;
        if (watched) {
            threat->route_pos = 0;
            threat->scene = threat->route[threat->route_pos];
            game->vent_repels++;
            return;
        }
    }

    if (threat->route_pos == threat->route_len - 2 && !threat->camera_repel && !threat->audio_lure) {
        bool blocked = threat->left_side ? game->left_door : game->right_door;
        if (blocked) {
            threat->route_pos = 1;
            threat->scene = threat->route[threat->route_pos];
            game->door_blocks++;
            return;
        }
    }

    threat->route_pos++;
    if (threat->route_pos >= threat->route_len) {
        threat->route_pos = threat->route_len - 1;
    }
    threat->scene = threat->route[threat->route_pos];
    if (threat->scene == SCENE_OFFICE) {
        game->mode = MODE_LOSS;
        game->loss_reason = LOSS_BREACH;
        game->loss_threat = threat_index;
        game->jumpscare_timer = 0.0f;
        game->monitor = false;
    }
}

void update_game(Game *game, float dt)
{
    if (game->mode == MODE_TITLE || game->mode == MODE_EXTRAS || game->mode == MODE_PAUSED) {
        game->static_timer += dt;
        return;
    }

    if (game->mode != MODE_PLAYING) {
        game->jumpscare_timer += dt;
        return;
    }

    game->survived += dt;
    game->static_timer += dt;
    if (game->audio_lure_timer > 0.0f) {
        game->audio_lure_timer -= dt;
        if (game->audio_lure_timer < 0.0f) {
            game->audio_lure_timer = 0.0f;
        }
    }
    if (game->call_timer < (float)PHONE_CALL_SECONDS) {
        game->call_timer += dt;
    }

    if (game->survived >= game->night_seconds) {
        game->mode = MODE_WIN;
        game->monitor = false;
        return;
    }

    if (game->power_out) {
        game->power_out_timer += dt;
        if (game->power_out_timer >= 8.0f) {
            game->mode = MODE_LOSS;
            game->loss_reason = LOSS_BLACKOUT;
            game->jumpscare_timer = 0.0f;
        }
        return;
    }

    float pressure = 1.0f + ((float)game_hour(game) * 0.18f);
    for (int i = 0; i < THREAT_COUNT; i++) {
        Threat *threat = &game->threats[i];
        if (!threat->active) {
            continue;
        }
        threat->move_timer += dt * pressure;
        if (threat->move_timer >= threat->move_interval) {
            threat->move_timer = 0.0f;
            move_threat(game, i);
        }
    }

    float drain = 0.42f;
    if (game->monitor) drain += 0.55f;
    if (game_audio_lure_active(game)) drain += 0.32f;
    if (game->left_door) drain += 0.38f;
    if (game->right_door) drain += 0.38f;
    if (game->left_light) drain += 0.24f;
    if (game->right_light) drain += 0.24f;
    drain *= game_is_custom_night(game) ? custom_power_drain_multiplier(game) : night_power_drain_multiplier(game->night);
    game->power -= drain * dt;
    if (game->power <= 0.0f) {
        game->power = 0.0f;
        game->power_out = true;
        game->power_out_timer = 0.0f;
        game->left_door = false;
        game->right_door = false;
        game->left_light = false;
        game->right_light = false;
        game->monitor = false;
    }
}

bool load_save_file(const char *path, SaveData *save)
{
    init_save(save);

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return errno == ENOENT;
    }

    char line[128];
    char key[32];
    int value;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (!parse_key_value_line(line, key, sizeof(key), &value)) {
            continue;
        }
        if (strcmp(key, "unlocked_night") == 0) {
            save->unlocked_night = clamp_night(value);
        } else if (strcmp(key, "best_night") == 0) {
            save->best_night = value < 0 ? 0 : clamp_night(value);
        } else if (strcmp(key, "story_cleared") == 0) {
            parse_bool_value(value, &save->story_cleared);
        } else if (strcmp(key, "custom_challenge_cleared") == 0) {
            parse_bool_value(value, &save->custom_challenge_cleared);
        } else {
            int power_night = 0;
            int key_end = 0;
            if (sscanf(key, "best_power_%d%n", &power_night, &key_end) == 1 &&
                key[key_end] == '\0' &&
                power_night >= MIN_NIGHT && power_night <= MAX_NIGHT) {
                if (value < 0) {
                    save->best_power[power_night - 1] = -1;
                } else if (value > 100) {
                    save->best_power[power_night - 1] = 100;
                } else {
                    save->best_power[power_night - 1] = value;
                }
            }
        }
    }

    fclose(file);
    int max_best_night = save->unlocked_night > MIN_NIGHT ? save->unlocked_night : 0;
    if (save->best_night > max_best_night) {
        save->best_night = max_best_night;
    }
    if (save->best_night >= MAX_NIGHT) {
        save->story_cleared = true;
    }
    return true;
}

static bool write_save_contents(FILE *file, const void *data)
{
    const SaveData *save = data;

    int written = fprintf(file,
        "unlocked_night=%d\nbest_night=%d\nstory_cleared=%d\ncustom_challenge_cleared=%d\n",
        clamp_night(save->unlocked_night),
        save->best_night < 0 ? 0 : clamp_night(save->best_night),
        save->story_cleared ? 1 : 0,
        save->custom_challenge_cleared ? 1 : 0);
    for (int i = 0; i < MAX_NIGHT && written > 0; i++) {
        int power = save->best_power[i];
        if (power < -1) power = -1;
        if (power > 100) power = 100;
        int line = fprintf(file, "best_power_%d=%d\n", i + 1, power);
        if (line <= 0) {
            written = line;
        }
    }
    return written > 0;
}

bool save_file(const char *path, const SaveData *save)
{
    return write_file_atomic(path, write_save_contents, save);
}

bool reset_save_file(const char *path)
{
    SaveData save;
    init_save(&save);
    return save_file(path, &save);
}

bool load_settings_file(const char *path, SettingsData *settings)
{
    init_settings(settings);

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return errno == ENOENT;
    }

    char line[128];
    char key[32];
    int value;
    bool saw_custom_ai = false;
    bool saw_custom_ai_configured = false;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (!parse_key_value_line(line, key, sizeof(key), &value)) {
            continue;
        }
        if (strcmp(key, "window_scale") == 0) {
            settings->window_scale = clamp_window_scale(value);
        } else if (strcmp(key, "custom_ai_configured") == 0) {
            if (parse_bool_value(value, &settings->custom_ai_configured)) {
                saw_custom_ai_configured = true;
            }
        } else if (strcmp(key, "fullscreen") == 0) {
            parse_bool_value(value, &settings->fullscreen);
        } else if (strcmp(key, "muted") == 0) {
            parse_bool_value(value, &settings->muted);
        } else {
            int threat = 0;
            int key_end = 0;
            if (sscanf(key, "custom_ai_%d%n", &threat, &key_end) == 1 &&
                key[key_end] == '\0' &&
                threat >= 1 && threat <= THREAT_COUNT) {
                settings->custom_ai[threat - 1] = clamp_ai_level(value);
                saw_custom_ai = true;
            }
        }
    }

    fclose(file);
    if (!saw_custom_ai_configured && saw_custom_ai) {
        settings->custom_ai_configured = true;
    }
    return true;
}

static bool write_settings_contents(FILE *file, const void *data)
{
    const SettingsData *settings = data;

    int written = fprintf(file, "window_scale=%d\nfullscreen=%d\nmuted=%d\n",
        clamp_window_scale(settings->window_scale),
        settings->fullscreen ? 1 : 0,
        settings->muted ? 1 : 0);
    if (written > 0) {
        int line = fprintf(file, "custom_ai_configured=%d\n", settings->custom_ai_configured ? 1 : 0);
        if (line <= 0) {
            written = line;
        }
    }
    for (int i = 0; i < THREAT_COUNT && written > 0; i++) {
        int line = fprintf(file, "custom_ai_%d=%d\n", i + 1, clamp_ai_level(settings->custom_ai[i]));
        if (line <= 0) {
            written = line;
        }
    }
    return written > 0;
}

bool save_settings_file(const char *path, const SettingsData *settings)
{
    return write_file_atomic(path, write_settings_contents, settings);
}
