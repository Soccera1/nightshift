#include "../src/game.h"

#include <stdio.h>
#include <stdlib.h>

static void require(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "test failed: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static void require_missing_file(const char *path, const char *message)
{
    FILE *file = fopen(path, "r");
    if (file != NULL) {
        fclose(file);
        require(0, message);
    }
}

static void tick(Game *game, float seconds)
{
    const float dt = 0.25f;
    float elapsed = 0.0f;
    while (elapsed < seconds && game->mode == MODE_PLAYING) {
        update_game(game, dt);
        elapsed += dt;
    }
}

static Game new_game(float night_seconds)
{
    Game game = { 0 };
    game.night_seconds = night_seconds;
    init_game(&game);
    return game;
}

static void title_does_not_advance_night(void)
{
    Game game = new_game(20.0f);
    update_game(&game, 50.0f);

    require(game.mode == MODE_TITLE, "title mode remains title during updates");
    require(game.loss_threat == -1, "fresh game has no loss threat");
    require(game.survived == 0.0f, "title mode does not advance survived time");
    require(game.power == 100.0f, "title mode does not drain power");
}

static void extras_does_not_advance_night(void)
{
    Game game = new_game(20.0f);
    game.mode = MODE_EXTRAS;
    update_game(&game, 50.0f);

    require(game.mode == MODE_EXTRAS, "extras mode remains extras during updates");
    require(game.survived == 0.0f, "extras mode does not advance survived time");
    require(game.power == 100.0f, "extras mode does not drain power");
    require(game.static_timer > 0.0f, "extras mode keeps ambience timer moving");
}

static void defended_short_night_can_be_won(void)
{
    Game game = new_game(20.0f);
    start_night(&game);
    game.left_door = true;
    game.right_door = true;

    tick(&game, 30.0f);

    require(game.mode == MODE_WIN, "closed doors can survive a short night");
    require(game.power > 0.0f, "winning short night leaves power");
    require(game_hour(&game) == 6, "winning night reports 6 AM");
}

static void phone_call_tracks_night_start_and_mute(void)
{
    Game game = new_game(120.0f);
    start_night(&game);

    require(game_phone_call_active(&game), "phone call starts active");
    require(game_phone_message(&game) != NULL, "phone call has briefing text");
    update_game(&game, 2.0f);
    require(game.call_timer >= 2.0f, "phone call timer advances during play");
    game.call_muted = true;
    require(!game_phone_call_active(&game), "muted phone call becomes inactive");

    start_night(&game);
    require(game.call_timer == 0.0f, "phone call timer resets on night restart");
    require(game.call_muted == false, "phone call mute resets on night restart");

    update_game(&game, (float)PHONE_CALL_SECONDS + 1.0f);
    require(!game_phone_call_active(&game), "phone call expires after briefing duration");
}

static void custom_night_has_custom_phone_message(void)
{
    Game game = new_game(120.0f);
    game.custom_night = true;
    set_custom_ai(&game, 0, 20);
    start_night(&game);

    require(game_phone_call_active(&game), "custom night phone call starts active");
    require(game_phone_message(&game)[0] == 'C', "custom night uses custom briefing");
}

static void later_nights_are_harder(void)
{
    Game night_one = new_game(20.0f);
    Game night_five = { 0 };
    night_five.night_seconds = 20.0f;
    night_five.unlocked_night = 5;
    night_five.night = 5;
    init_game(&night_five);

    require(night_five.threats[0].move_interval < night_one.threats[0].move_interval,
        "later nights move threats faster");
    require(night_five.threats[2].move_interval < night_one.threats[2].move_interval,
        "later nights move vent threat faster");
    require(night_power_drain_multiplier(5) > night_power_drain_multiplier(1),
        "later nights drain power faster");
}

static void story_threat_rules_match_night_progression(void)
{
    require(story_threat_active(0, 1), "rust is active from night one");
    require(story_threat_active(1, 1), "volt is active from night one");
    require(!story_threat_active(2, 2), "skitr is inactive before night three");
    require(story_threat_active(2, 3), "skitr activates on night three");
    require(!story_threat_active(3, 3), "echo is inactive before night four");
    require(story_threat_active(3, 4), "echo activates on night four");
    require(!story_threat_active(THREAT_COUNT, 6), "unknown story threat is inactive");
}

static void story_nights_introduce_threats_progressively(void)
{
    Game game = { 0 };
    game.night_seconds = 20.0f;
    game.unlocked_night = MAX_NIGHT;

    game.night = 1;
    init_game(&game);
    start_night(&game);
    require(game.threats[0].active, "night one keeps rust active");
    require(game.threats[1].active, "night one keeps volt active");
    require(!game.threats[2].active, "night one keeps skitr inactive");
    require(!game.threats[3].active, "night one keeps echo inactive");

    game.night = 3;
    start_night(&game);
    require(game.threats[2].active, "night three activates skitr");
    require(!game.threats[3].active, "night three keeps echo inactive");

    game.night = 4;
    start_night(&game);
    require(game.threats[3].active, "night four activates echo");
}

static void custom_night_uses_ai_levels(void)
{
    Game game = new_game(120.0f);
    game.custom_night = true;
    set_custom_ai(&game, 0, 20);
    set_custom_ai(&game, 1, 10);
    set_custom_ai(&game, 2, 0);
    set_custom_ai(&game, 3, 7);
    start_night(&game);

    require(game_is_custom_night(&game), "custom night mode is preserved across start");
    require(game.custom_ai[0] == 20, "custom AI clamps high values");
    require(game.custom_ai[2] == 0, "custom AI allows zero values");
    require(game.custom_ai[3] == 7, "custom AI includes audio lure threat");
    require(game.custom_ai_configured == true, "setting custom AI marks configuration present");
    require(game.threats[0].active, "custom threat with AI is active");
    require(!game.threats[2].active, "custom threat at AI zero is inactive");
    require(game.threats[3].active, "custom audio threat with AI is active");
    require(game.threats[0].move_interval < 15.0f / night_difficulty(5),
        "max custom AI can exceed story night five speed");
    require(game.threats[2].move_interval > 22.0f / night_difficulty(1),
        "zero custom AI slows a threat below story night one");
    require(power_usage_level(&game) == 1, "moderate custom night does not add idle usage");

    set_custom_ai(&game, 0, 20);
    set_custom_ai(&game, 1, 20);
    set_custom_ai(&game, 2, 20);
    set_custom_ai(&game, 3, 20);
    require(power_usage_level(&game) == 2, "max custom AI adds usage pressure");
}

static void custom_night_win_does_not_unlock_story_progress(void)
{
    Game game = new_game(20.0f);
    SaveData save = {
        .unlocked_night = 2,
        .best_night = 1,
    };
    game.custom_night = true;
    set_custom_ai(&game, 0, 5);
    start_night(&game);
    game.mode = MODE_WIN;

    record_night_result(&game, &save);

    require(save.unlocked_night == 2, "custom night does not unlock story nights");
    require(save.best_night == 1, "custom night does not update best story night");
    require(save.custom_challenge_cleared == false, "non-max custom night does not award custom clear star");
}

static void max_custom_night_records_custom_clear_star(void)
{
    Game game = new_game(20.0f);
    SaveData save;
    init_save(&save);
    save.unlocked_night = 2;
    save.best_night = 1;
    save.story_cleared = true;
    game.custom_night = true;
    for (int i = 0; i < THREAT_COUNT; i++) {
        set_custom_ai(&game, i, MAX_AI_LEVEL);
    }
    start_night(&game);
    game.mode = MODE_WIN;

    record_night_result(&game, &save);

    require(save.unlocked_night == 2, "max custom night still does not unlock story nights");
    require(save.best_night == 1, "max custom night still does not update best story night");
    require(save.story_cleared == true, "max custom night preserves story clear star");
    require(save.custom_challenge_cleared == true, "max custom night records custom clear star");
    require(game.custom_challenge_cleared == true, "game sees custom clear star");
}

static void best_night_is_preserved_and_clamped(void)
{
    Game game = { 0 };
    game.night_seconds = 20.0f;
    game.unlocked_night = 3;
    game.night = 3;
    game.best_night = 5;
    init_game(&game);

    require(game.best_night == 3, "best night clamps to unlocked night");
    start_night(&game);
    require(game.best_night == 3, "best night survives night start reset");
}

static void best_power_defaults_to_unset(void)
{
    SaveData save;
    init_save(&save);

    for (int i = 0; i < MAX_NIGHT; i++) {
        require(save.best_power[i] == -1, "fresh save has no best power record");
    }
}

static void zeroed_game_best_power_defaults_to_unset(void)
{
    Game game = { 0 };
    init_game(&game);

    for (int i = 0; i < MAX_NIGHT; i++) {
        require(game.best_power[i] == -1, "zeroed game has no best power record");
    }
}

static void init_game_preserves_recorded_zero_best_power(void)
{
    Game game = { 0 };
    game.unlocked_night = 2;
    game.best_night = 1;
    game.best_power[0] = 0;
    init_game(&game);

    require(game.best_power[0] == 0, "recorded zero best power is preserved");
    for (int i = 1; i < MAX_NIGHT; i++) {
        require(game.best_power[i] == 0, "progressed zero record remains explicit");
    }
}

static void usage_meter_tracks_active_systems(void)
{
    Game game = new_game(20.0f);
    start_night(&game);
    require(power_usage_level(&game) == 1, "base usage starts at one");

    game.monitor = true;
    game.left_door = true;
    game.right_light = true;
    require(power_usage_level(&game) == 4, "usage counts active monitor door and light");

    game.night = 5;
    game.right_door = true;
    game.left_light = true;
    require(power_usage_level(&game) == 6, "usage clamps at six");
}

static void threat_alerts_track_door_and_vent_danger(void)
{
    Game game = new_game(120.0f);
    start_night(&game);
    require(!game_left_door_danger(&game), "left danger starts clear");
    require(!game_right_door_danger(&game), "right danger starts clear");
    require(!game_vent_danger(&game), "vent danger starts clear");

    game.threats[0].route_pos = game.threats[0].route_len - 2;
    game.threats[0].scene = SCENE_LEFT_HALL;
    require(game_left_door_danger(&game), "left hall threat at door reports danger");
    require(!game_right_door_danger(&game), "left hall threat does not report right danger");

    game.threats[1].route_pos = game.threats[1].route_len - 2;
    game.threats[1].scene = SCENE_RIGHT_HALL;
    require(game_right_door_danger(&game), "right hall threat at door reports danger");

    game.threats[2].route_pos = game.threats[2].route_len - 2;
    game.threats[2].scene = SCENE_VENT;
    game.threats[2].active = true;
    require(game_vent_danger(&game), "vent threat at service vent reports danger");

    game.left_door = true;
    game.threats[0].move_timer = game.threats[0].move_interval;
    update_game(&game, 0.1f);
    require(!game_left_door_danger(&game), "blocked left threat clears danger after retreat");
}

static void watched_vent_threat_retreats(void)
{
    Game game = new_game(120.0f);
    start_night(&game);
    game.threats[0].move_interval = 10000.0f;
    game.threats[1].move_interval = 10000.0f;
    game.threats[2].active = true;
    game.threats[2].move_interval = 1.0f;

    update_game(&game, 1.0f);
    update_game(&game, 1.0f);
    require(game.threats[2].scene == SCENE_VENT, "vent threat reaches service vent");

    game.monitor = true;
    game.selected_camera = 5;
    update_game(&game, 1.0f);

    require(game.mode == MODE_PLAYING, "watching vent prevents loss");
    require(game.threats[2].scene == SCENE_BACKSTAGE, "watching vent drives threat back");
    require(game.vent_repels == 1, "watching vent records a repel");
}

static void unwatched_vent_threat_loses(void)
{
    Game game = new_game(120.0f);
    start_night(&game);
    game.threats[0].move_interval = 10000.0f;
    game.threats[1].move_interval = 10000.0f;
    game.threats[2].active = true;
    game.threats[2].move_interval = 1.0f;

    update_game(&game, 1.0f);
    update_game(&game, 1.0f);
    update_game(&game, 1.0f);

    require(game.mode == MODE_LOSS, "unwatched vent threat reaches office");
    require(game.loss_reason == LOSS_BREACH, "office breach records loss reason");
    require(game.loss_threat == 2, "office breach records vent threat");
}

static void audio_lure_pulls_echo_back(void)
{
    Game game = new_game(120.0f);
    start_night(&game);
    for (int i = 0; i < THREAT_COUNT; i++) {
        game.threats[i].move_interval = 10000.0f;
    }
    game.threats[3].active = true;
    game.threats[3].route_pos = 3;
    game.threats[3].scene = SCENE_RIGHT_HALL;
    game.threats[3].move_interval = 1.0f;
    game.monitor = true;
    game.selected_camera = 1;

    require(trigger_audio_lure(&game), "monitor can trigger audio lure");
    require(game_audio_lure_active(&game), "audio lure becomes active");
    require(power_usage_level(&game) == 3, "active audio lure adds power usage");
    update_game(&game, 1.0f);

    require(game.mode == MODE_PLAYING, "audio lure prevents echo office entry");
    require(game.threats[3].scene == SCENE_DINING, "audio lure pulls echo back to selected camera route");
    require(game.audio_lures == 1, "successful audio lure is counted");
    require(!game_audio_lure_active(&game), "successful audio lure consumes active lure");
}

static void echo_ignores_doors_without_audio_lure(void)
{
    Game game = new_game(120.0f);
    start_night(&game);
    for (int i = 0; i < THREAT_COUNT; i++) {
        game.threats[i].move_interval = 10000.0f;
    }
    game.threats[3].active = true;
    game.threats[3].route_pos = 3;
    game.threats[3].scene = SCENE_RIGHT_HALL;
    game.threats[3].move_interval = 1.0f;
    game.right_door = true;

    update_game(&game, 1.0f);

    require(game.mode == MODE_LOSS, "echo ignores the right door");
    require(game.loss_reason == LOSS_BREACH, "echo breach records loss reason");
    require(game.loss_threat == 3, "echo breach records threat index");
}

static void winning_records_progress(void)
{
    Game game = new_game(20.0f);
    SaveData save;
    init_save(&save);
    start_night(&game);
    game.mode = MODE_WIN;

    record_night_result(&game, &save);

    require(save.best_night == 1, "win records best night");
    require(save.unlocked_night == 2, "win unlocks next night");
    require(game.unlocked_night == 2, "game sees newly unlocked night");
    require(game.best_night == 1, "game sees best night progress");
}

static void winning_records_best_power_without_lowering_it(void)
{
    Game game = new_game(20.0f);
    SaveData save;
    init_save(&save);
    save.unlocked_night = 2;
    game.unlocked_night = 2;
    game.night = 2;
    init_game(&game);
    start_night(&game);
    game.mode = MODE_WIN;
    game.power = 42.4f;

    record_night_result(&game, &save);

    require(save.best_power[1] == 42, "win records rounded remaining power");
    require(game.best_power[1] == 42, "game sees recorded best power");

    game.mode = MODE_WIN;
    game.power = 20.0f;
    record_night_result(&game, &save);
    require(save.best_power[1] == 42, "lower remaining power does not replace best power");

    game.mode = MODE_WIN;
    game.power = 99.6f;
    record_night_result(&game, &save);
    require(save.best_power[1] == 100, "higher remaining power replaces best power and clamps high");
}

static void night_five_unlocks_overtime_night_six(void)
{
    Game game = new_game(20.0f);
    SaveData save;
    init_save(&save);
    save.unlocked_night = 5;
    save.best_night = 4;
    game.unlocked_night = 5;
    game.night = 5;
    init_game(&game);
    start_night(&game);
    game.mode = MODE_WIN;

    record_night_result(&game, &save);

    require(save.best_night == 5, "night five win records best night");
    require(save.unlocked_night == 6, "night five win unlocks overtime night six");
    require(game.unlocked_night == 6, "game sees overtime unlock");
}

static void night_six_is_story_progress_cap(void)
{
    Game game = new_game(20.0f);
    SaveData save;
    init_save(&save);
    save.unlocked_night = MAX_NIGHT;
    save.best_night = 5;
    game.unlocked_night = MAX_NIGHT;
    game.night = MAX_NIGHT;
    init_game(&game);
    start_night(&game);
    game.mode = MODE_WIN;

    record_night_result(&game, &save);

    require(save.best_night == MAX_NIGHT, "night six win records final best night");
    require(save.unlocked_night == MAX_NIGHT, "night six win does not exceed max unlock");
    require(save.story_cleared == true, "night six win records story clear star");
    require(game.unlocked_night == MAX_NIGHT, "game unlock remains capped at night six");
    require(game.story_cleared == true, "game sees story clear star");
}

static void undefended_route_loses(void)
{
    Game game = new_game(120.0f);
    start_night(&game);

    tick(&game, 80.0f);

    require(game.mode == MODE_LOSS, "undefended route reaches the office");
    require(game.monitor == false, "loss closes monitor");
    require(game.loss_reason == LOSS_BREACH, "undefended route records breach loss");
    require(game.loss_threat >= 0 && game.loss_threat < THREAT_COUNT, "undefended route records breaching threat");
}

static void blocked_door_records_defense(void)
{
    Game game = new_game(120.0f);
    start_night(&game);
    game.threats[0].move_interval = 1.0f;
    game.threats[1].move_interval = 10000.0f;
    game.threats[2].move_interval = 10000.0f;

    update_game(&game, 1.0f);
    update_game(&game, 1.0f);
    update_game(&game, 1.0f);
    require(game.threats[0].scene == SCENE_LEFT_HALL, "left threat reaches door");

    game.left_door = true;
    update_game(&game, 1.0f);

    require(game.mode == MODE_PLAYING, "closed door prevents office entry");
    require(game.threats[0].scene == SCENE_DINING, "blocked threat retreats");
    require(game.door_blocks == 1, "closed door records a block");
}

static void power_out_loses(void)
{
    Game game = new_game(300.0f);
    start_night(&game);
    game.left_door = true;
    game.right_door = true;
    game.left_light = true;
    game.right_light = true;
    game.monitor = true;
    game.power = 0.1f;
    for (int i = 0; i < THREAT_COUNT; i++) {
        game.threats[i].move_interval = 10000.0f;
    }

    tick(&game, 1.0f);

    require(game.power_out == true, "power loss enters blackout");
    require(game.mode == MODE_PLAYING, "blackout does not lose immediately");
    require(power_usage_level(&game) == 0, "blackout disables usage meter");
    require(game.left_door == false && game.right_door == false, "power loss opens doors");
    require(game.left_light == false && game.right_light == false, "power loss turns off lights");
    require(game.monitor == false, "power loss closes monitor");
    require(!game_phone_call_active(&game), "power loss cuts the phone call");
    require(!trigger_audio_lure(&game), "blackout prevents camera audio");
    tick(&game, 9.0f);

    require(game.mode == MODE_LOSS, "running all systems drains power to loss");
    require(game.loss_reason == LOSS_BLACKOUT, "blackout timeout records loss reason");
    require(game.loss_threat == -1, "blackout loss has no breaching threat");
    require(game.power == 0.0f, "power loss clamps power to zero");
}

static void six_am_can_arrive_during_blackout(void)
{
    Game game = new_game(10.0f);
    start_night(&game);
    game.survived = 9.0f;
    game.power = 0.1f;
    game.left_door = true;
    game.right_door = true;
    game.left_light = true;
    game.right_light = true;
    game.monitor = true;
    for (int i = 0; i < THREAT_COUNT; i++) {
        game.threats[i].move_interval = 10000.0f;
    }

    tick(&game, 1.0f);
    require(game.power_out == true, "near-end power loss enters blackout");
    tick(&game, 2.0f);

    require(game.mode == MODE_WIN, "6 AM can arrive during blackout");
}

static void save_round_trips_progress(void)
{
    const char *path = "build/test_save.tmp";
    const char *temporary_path = "build/test_save.tmp.tmp";
    SaveData saved;
    SaveData loaded;
    init_save(&saved);
    saved.unlocked_night = 4;
    saved.best_night = 3;
    saved.story_cleared = true;
    saved.custom_challenge_cleared = true;
    saved.best_power[0] = 87;
    saved.best_power[5] = 12;

    require(save_file(path, &saved), "save file writes");
    require(load_save_file(path, &loaded), "save file loads");
    require(loaded.unlocked_night == 4, "save preserves unlocked night");
    require(loaded.best_night == 3, "save preserves best night");
    require(loaded.story_cleared == true, "save preserves story clear star");
    require(loaded.custom_challenge_cleared == true, "save preserves custom clear star");
    require(loaded.best_power[0] == 87, "save preserves first night best power");
    require(loaded.best_power[1] == -1, "save preserves unset best power");
    require(loaded.best_power[5] == 12, "save preserves overtime best power");
    require_missing_file(temporary_path, "save write does not leave temporary file");
    remove(path);
}

static void legacy_save_with_final_best_night_gets_story_star(void)
{
    const char *path = "build/test_legacy_save.tmp";
    FILE *file = fopen(path, "w");
    require(file != NULL, "legacy save file opens");
    require(fprintf(file, "unlocked_night=6\nbest_night=6\n") > 0, "legacy save file writes");
    require(fclose(file) == 0, "legacy save file closes");

    SaveData loaded;
    require(load_save_file(path, &loaded), "legacy save file loads");
    require(loaded.unlocked_night == MAX_NIGHT, "legacy save preserves final unlock");
    require(loaded.best_night == MAX_NIGHT, "legacy save preserves final best night");
    require(loaded.story_cleared == true, "legacy final best night derives story clear star");
    require(loaded.custom_challenge_cleared == false, "legacy save does not invent custom clear star");
    require(loaded.best_power[0] == -1, "legacy save leaves best power unset");
    remove(path);
}

static void fresh_save_keeps_zero_best_night(void)
{
    const char *path = "build/test_fresh_save.tmp";
    SaveData saved;
    SaveData loaded;

    init_save(&saved);
    require(save_file(path, &saved), "fresh save file writes");
    require(load_save_file(path, &loaded), "fresh save file loads");
    require(loaded.unlocked_night == MIN_NIGHT, "fresh save unlocks first night");
    require(loaded.best_night == 0, "fresh save keeps zero best night");
    for (int i = 0; i < MAX_NIGHT; i++) {
        require(loaded.best_power[i] == -1, "fresh save keeps best power unset");
    }
    remove(path);
}

static void reset_save_restores_default_progress(void)
{
    const char *path = "build/test_reset_save.tmp";
    SaveData saved;
    SaveData loaded;
    init_save(&saved);
    saved.unlocked_night = 5;
    saved.best_night = 5;
    saved.best_power[0] = 90;

    require(save_file(path, &saved), "advanced save file writes");
    require(reset_save_file(path), "reset save file writes defaults");
    require(load_save_file(path, &loaded), "reset save file loads");
    require(loaded.unlocked_night == MIN_NIGHT, "reset save restores first unlocked night");
    require(loaded.best_night == 0, "reset save clears best night");
    require(loaded.story_cleared == false, "reset save clears story clear star");
    require(loaded.custom_challenge_cleared == false, "reset save clears custom clear star");
    for (int i = 0; i < MAX_NIGHT; i++) {
        require(loaded.best_power[i] == -1, "reset save clears best power");
    }
    remove(path);
}

static void save_load_clamps_best_power_values(void)
{
    const char *path = "build/test_power_clamp_save.tmp";
    FILE *file = fopen(path, "w");
    require(file != NULL, "power clamp save file opens");
    require(fprintf(file,
        "unlocked_night=6\nbest_night=4\nbest_power_1=120\nbest_power_2=-9\nbest_power_3=67\nbest_power_7=90\n") > 0,
        "power clamp save file writes");
    require(fclose(file) == 0, "power clamp save file closes");

    SaveData loaded;
    require(load_save_file(path, &loaded), "power clamp save file loads");
    require(loaded.best_power[0] == 100, "best power clamps above range");
    require(loaded.best_power[1] == -1, "best power clamps below unset");
    require(loaded.best_power[2] == 67, "best power preserves valid value");
    remove(path);
}

static void malformed_save_lines_do_not_stop_loading(void)
{
    const char *path = "build/test_malformed_save.tmp";
    FILE *file = fopen(path, "w");
    require(file != NULL, "malformed save file opens");
    require(fprintf(file,
        "this line is not valid\n"
        "unlocked_night=4\n"
        "bad_number=abc\n"
        "best_night=3\n"
        "best_power_2=55 trailing\n"
        "best_power_3=72\n"
        "story_cleared=0\n") > 0,
        "malformed save file writes");
    require(fclose(file) == 0, "malformed save file closes");

    SaveData loaded;
    require(load_save_file(path, &loaded), "malformed save file loads");
    require(loaded.unlocked_night == 4, "save loader reads valid lines after malformed line");
    require(loaded.best_night == 3, "save loader keeps reading after invalid number");
    require(loaded.best_power[1] == -1, "save loader rejects trailing garbage");
    require(loaded.best_power[2] == 72, "save loader reads later best power");
    remove(path);
}

static void save_rejects_partial_best_power_keys(void)
{
    const char *path = "build/test_partial_power_key_save.tmp";
    FILE *file = fopen(path, "w");
    require(file != NULL, "partial best power key save file opens");
    require(fprintf(file,
        "unlocked_night=4\n"
        "best_power_2_extra=91\n"
        "best_power_2=44\n") > 0,
        "partial best power key save file writes");
    require(fclose(file) == 0, "partial best power key save file closes");

    SaveData loaded;
    require(load_save_file(path, &loaded), "partial best power key save file loads");
    require(loaded.best_power[1] == 44, "save loader ignores partial best power key matches");
    remove(path);
}

static void save_rejects_invalid_boolean_flags(void)
{
    const char *path = "build/test_invalid_bool_save.tmp";
    FILE *file = fopen(path, "w");
    require(file != NULL, "invalid boolean save file opens");
    require(fprintf(file,
        "unlocked_night=6\n"
        "best_night=5\n"
        "story_cleared=2\n"
        "custom_challenge_cleared=-1\n"
        "custom_challenge_cleared=1\n") > 0,
        "invalid boolean save file writes");
    require(fclose(file) == 0, "invalid boolean save file closes");

    SaveData loaded;
    require(load_save_file(path, &loaded), "invalid boolean save file loads");
    require(loaded.story_cleared == false, "save loader rejects invalid story clear flag");
    require(loaded.custom_challenge_cleared == true, "save loader reads later valid custom clear flag");
    remove(path);
}

static void settings_round_trip_and_clamp(void)
{
    const char *path = "build/test_settings.tmp";
    const char *temporary_path = "build/test_settings.tmp.tmp";
    SettingsData saved = {
        .window_scale = 3,
        .custom_ai = { 4, 8, 12, 16 },
        .custom_ai_configured = true,
        .fullscreen = true,
        .muted = true,
    };
    SettingsData loaded;

    require(save_settings_file(path, &saved), "settings file writes");
    require(load_settings_file(path, &loaded), "settings file loads");
    require(loaded.window_scale == 3, "settings preserve window scale");
    require(loaded.fullscreen == true, "settings preserve fullscreen");
    require(loaded.muted == true, "settings preserve mute");
    require(loaded.custom_ai[0] == 4, "settings preserve first custom AI");
    require(loaded.custom_ai[1] == 8, "settings preserve second custom AI");
    require(loaded.custom_ai[2] == 12, "settings preserve third custom AI");
    require(loaded.custom_ai[3] == 16, "settings preserve fourth custom AI");
    require(loaded.custom_ai_configured == true, "settings preserve custom AI configured flag");
    require_missing_file(temporary_path, "settings write does not leave temporary file");
    remove(path);
}

static void malformed_settings_lines_do_not_stop_loading(void)
{
    const char *path = "build/test_malformed_settings.tmp";
    FILE *file = fopen(path, "w");
    require(file != NULL, "malformed settings file opens");
    require(fprintf(file,
        "invalid settings line\n"
        "window_scale=9\n"
        "fullscreen=yes\n"
        "muted=2\n"
        "muted=1\n"
        "custom_ai_configured=0\n"
        "custom_ai_1=25\n"
        "custom_ai_2=-5\n"
        "custom_ai_3_extra=19\n"
        "custom_ai_3=11\n") > 0,
        "malformed settings file writes");
    require(fclose(file) == 0, "malformed settings file closes");

    SettingsData loaded;
    require(load_settings_file(path, &loaded), "malformed settings file loads");
    require(loaded.window_scale == 4, "settings loader clamps valid line after malformed line");
    require(loaded.fullscreen == false, "settings loader rejects invalid boolean value");
    require(loaded.muted == true, "settings loader reads later muted value");
    require(loaded.custom_ai[0] == MAX_AI_LEVEL, "settings loader clamps high custom AI");
    require(loaded.custom_ai[1] == MIN_AI_LEVEL, "settings loader clamps low custom AI");
    require(loaded.custom_ai[2] == 11, "settings loader ignores partial custom AI key matches");
    require(loaded.custom_ai_configured == false, "settings loader honors explicit custom AI configured flag");
    remove(path);
}

static void legacy_custom_ai_settings_mark_configuration_present(void)
{
    const char *path = "build/test_legacy_custom_ai_settings.tmp";
    FILE *file = fopen(path, "w");
    require(file != NULL, "legacy custom AI settings file opens");
    require(fprintf(file,
        "window_scale=1\n"
        "custom_ai_1=0\n"
        "custom_ai_2=0\n"
        "custom_ai_3=0\n"
        "custom_ai_4=0\n") > 0,
        "legacy custom AI settings file writes");
    require(fclose(file) == 0, "legacy custom AI settings file closes");

    SettingsData loaded;
    require(load_settings_file(path, &loaded), "legacy custom AI settings file loads");
    require(loaded.custom_ai_configured == true, "legacy custom AI keys mark configuration present");
    for (int i = 0; i < THREAT_COUNT; i++) {
        require(loaded.custom_ai[i] == 0, "legacy all-zero custom AI is preserved");
    }
    remove(path);
}

int main(void)
{
    title_does_not_advance_night();
    extras_does_not_advance_night();
    defended_short_night_can_be_won();
    phone_call_tracks_night_start_and_mute();
    custom_night_has_custom_phone_message();
    later_nights_are_harder();
    story_threat_rules_match_night_progression();
    story_nights_introduce_threats_progressively();
    custom_night_uses_ai_levels();
    custom_night_win_does_not_unlock_story_progress();
    max_custom_night_records_custom_clear_star();
    best_night_is_preserved_and_clamped();
    best_power_defaults_to_unset();
    zeroed_game_best_power_defaults_to_unset();
    init_game_preserves_recorded_zero_best_power();
    usage_meter_tracks_active_systems();
    threat_alerts_track_door_and_vent_danger();
    watched_vent_threat_retreats();
    unwatched_vent_threat_loses();
    audio_lure_pulls_echo_back();
    echo_ignores_doors_without_audio_lure();
    winning_records_progress();
    winning_records_best_power_without_lowering_it();
    night_five_unlocks_overtime_night_six();
    night_six_is_story_progress_cap();
    undefended_route_loses();
    blocked_door_records_defense();
    power_out_loses();
    six_am_can_arrive_during_blackout();
    save_round_trips_progress();
    legacy_save_with_final_best_night_gets_story_star();
    fresh_save_keeps_zero_best_night();
    reset_save_restores_default_progress();
    save_load_clamps_best_power_values();
    malformed_save_lines_do_not_stop_loading();
    save_rejects_partial_best_power_keys();
    save_rejects_invalid_boolean_flags();
    settings_round_trip_and_clamp();
    malformed_settings_lines_do_not_stop_loading();
    legacy_custom_ai_settings_mark_configuration_present();
    puts("game logic tests passed");
    return EXIT_SUCCESS;
}
