#ifndef NIGHTSHIFT_AUDIO_H
#define NIGHTSHIFT_AUDIO_H

#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#elif __has_include(<SDL.h>)
#include <SDL.h>
#else
#error "SDL2 headers are required for audio."
#endif

#include <stdbool.h>

typedef enum SoundCue {
    SOUND_DOOR,
    SOUND_LIGHT,
    SOUND_CAMERA,
    SOUND_LURE,
    SOUND_START,
    SOUND_WIN,
    SOUND_LOSS,
} SoundCue;

typedef struct Audio {
    SDL_AudioDeviceID device;
    SDL_AudioSpec spec;
    bool enabled;
    bool muted;
} Audio;

bool audio_init(Audio *audio);
void audio_set_muted(Audio *audio, bool muted);
void audio_play(Audio *audio, SoundCue cue);
void audio_shutdown(Audio *audio);

#endif
