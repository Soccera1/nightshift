#ifndef NIGHTSHIFT_GAME_H
#define NIGHTSHIFT_GAME_H

#include <stdbool.h>

enum {
    CAMERA_COUNT = 6,
    THREAT_COUNT = 4,
    DEFAULT_NIGHT_SECONDS = 360,
    MIN_NIGHT = 1,
    MAX_NIGHT = 6,
    MIN_AI_LEVEL = 0,
    MAX_AI_LEVEL = 20,
    PHONE_CALL_SECONDS = 22,
};

typedef enum Scene {
    SCENE_STAGE,
    SCENE_DINING,
    SCENE_LEFT_HALL,
    SCENE_RIGHT_HALL,
    SCENE_BACKSTAGE,
    SCENE_VENT,
    SCENE_OFFICE,
} Scene;

typedef enum GameMode {
    MODE_TITLE,
    MODE_EXTRAS,
    MODE_PLAYING,
    MODE_PAUSED,
    MODE_WIN,
    MODE_LOSS,
} GameMode;

typedef enum LossReason {
    LOSS_NONE,
    LOSS_BREACH,
    LOSS_BLACKOUT,
} LossReason;

typedef struct Threat {
    const char *name;
    Scene scene;
    Scene route[6];
    int route_len;
    int route_pos;
    float move_timer;
    float move_interval;
    bool left_side;
    bool camera_repel;
    bool audio_lure;
    bool active;
} Threat;

typedef struct Game {
    GameMode mode;
    bool running;
    bool monitor;
    bool left_door;
    bool right_door;
    bool left_light;
    bool right_light;
    bool power_out;
    bool custom_night;
    bool custom_ai_configured;
    bool story_cleared;
    bool custom_challenge_cleared;
    bool call_muted;
    LossReason loss_reason;
    int loss_threat;
    int selected_camera;
    int night;
    int unlocked_night;
    int best_night;
    int selected_custom_threat;
    int custom_ai[THREAT_COUNT];
    int best_power[MAX_NIGHT];
    int door_blocks;
    int vent_repels;
    int audio_lures;
    Scene audio_lure_scene;
    float night_seconds;
    float power;
    float survived;
    float static_timer;
    float jumpscare_timer;
    float power_out_timer;
    float call_timer;
    float audio_lure_timer;
    Threat threats[THREAT_COUNT];
} Game;

typedef struct SaveData {
    int unlocked_night;
    int best_night;
    int best_power[MAX_NIGHT];
    bool story_cleared;
    bool custom_challenge_cleared;
} SaveData;

typedef struct SettingsData {
    int window_scale;
    int custom_ai[THREAT_COUNT];
    bool custom_ai_configured;
    bool fullscreen;
    bool muted;
} SettingsData;

const char *scene_name(Scene scene);
Scene camera_scene(int camera);
int game_hour(const Game *game);
float night_difficulty(int night);
float ai_level_difficulty(int ai_level);
float night_power_drain_multiplier(int night);
int power_usage_level(const Game *game);
bool game_is_custom_night(const Game *game);
bool game_is_max_custom_night(const Game *game);
bool game_phone_call_active(const Game *game);
const char *game_phone_message(const Game *game);
bool game_left_door_danger(const Game *game);
bool game_right_door_danger(const Game *game);
bool game_vent_danger(const Game *game);
bool game_audio_lure_danger(const Game *game);
bool game_audio_lure_active(const Game *game);
bool story_threat_active(int threat_index, int night);
void set_custom_ai(Game *game, int threat_index, int ai_level);
bool trigger_audio_lure(Game *game);
void init_game(Game *game);
void start_night(Game *game);
void record_night_result(Game *game, SaveData *save);
void update_game(Game *game, float dt);
void init_save(SaveData *save);
bool load_save_file(const char *path, SaveData *save);
bool save_file(const char *path, const SaveData *save);
bool reset_save_file(const char *path);
void init_settings(SettingsData *settings);
bool load_settings_file(const char *path, SettingsData *settings);
bool save_settings_file(const char *path, const SettingsData *settings);

#endif
