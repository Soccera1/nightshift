#include "audio.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

enum {
    AUDIO_RATE = 48000,
};

static float env_at(int i, int frames)
{
    int attack = frames / 16;
    int release = frames / 4;
    if (attack < 1) {
        attack = 1;
    }
    if (release < 1) {
        release = 1;
    }
    if (i < attack) {
        return (float)i / (float)attack;
    }
    if (i > frames - release) {
        return (float)(frames - i) / (float)release;
    }
    return 1.0f;
}

static float noise(uint32_t *state)
{
    *state = (*state * 1664525u) + 1013904223u;
    return ((float)((*state >> 16) & 0xffffu) / 32767.5f) - 1.0f;
}

static float tone(float hz, float t)
{
    return sinf(t * hz * 6.28318530718f);
}

bool audio_init(Audio *audio)
{
    *audio = (Audio){ 0 };

    SDL_AudioSpec want = {
        .freq = AUDIO_RATE,
        .format = AUDIO_F32SYS,
        .channels = 1,
        .samples = 1024,
    };

    audio->device = SDL_OpenAudioDevice(NULL, 0, &want, &audio->spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (audio->device == 0) {
        audio->enabled = false;
        return false;
    }

    audio->enabled = true;
    SDL_PauseAudioDevice(audio->device, 0);
    return true;
}

void audio_set_muted(Audio *audio, bool muted)
{
    audio->muted = muted;
}

void audio_play(Audio *audio, SoundCue cue)
{
    if (!audio->enabled || audio->muted) {
        return;
    }

    float seconds = 0.18f;
    float freq_a = 180.0f;
    float freq_b = 90.0f;
    float volume = 0.18f;
    bool noisy = false;

    switch (cue) {
    case SOUND_DOOR:
        seconds = 0.18f;
        freq_a = 115.0f;
        freq_b = 70.0f;
        volume = 0.22f;
        noisy = true;
        break;
    case SOUND_LIGHT:
        seconds = 0.07f;
        freq_a = 880.0f;
        freq_b = 660.0f;
        volume = 0.10f;
        break;
    case SOUND_CAMERA:
        seconds = 0.16f;
        freq_a = 250.0f;
        freq_b = 1200.0f;
        volume = 0.11f;
        noisy = true;
        break;
    case SOUND_LURE:
        seconds = 0.42f;
        freq_a = 620.0f;
        freq_b = 410.0f;
        volume = 0.13f;
        break;
    case SOUND_START:
        seconds = 0.25f;
        freq_a = 330.0f;
        freq_b = 495.0f;
        volume = 0.14f;
        break;
    case SOUND_WIN:
        seconds = 0.65f;
        freq_a = 440.0f;
        freq_b = 660.0f;
        volume = 0.16f;
        break;
    case SOUND_LOSS:
        seconds = 0.85f;
        freq_a = 95.0f;
        freq_b = 48.0f;
        volume = 0.26f;
        noisy = true;
        break;
    }

    int frames = (int)((float)audio->spec.freq * seconds);
    if (frames <= 0) {
        return;
    }

    float *samples = calloc((size_t)frames, sizeof(*samples));
    if (samples == NULL) {
        return;
    }

    uint32_t random_state = 0x12345678u ^ (uint32_t)cue;
    for (int i = 0; i < frames; i++) {
        float t = (float)i / (float)audio->spec.freq;
        float sweep = (float)i / (float)frames;
        float hz = freq_a + ((freq_b - freq_a) * sweep);
        float sample = tone(hz, t);
        if (cue == SOUND_WIN) {
            sample = (tone(freq_a, t) * 0.45f) + (tone(freq_b, t) * 0.55f);
        }
        if (noisy) {
            sample = (sample * 0.55f) + (noise(&random_state) * 0.45f);
        }
        samples[i] = sample * env_at(i, frames) * volume;
    }

    SDL_QueueAudio(audio->device, samples, (uint32_t)((size_t)frames * sizeof(*samples)));
    free(samples);
}

void audio_shutdown(Audio *audio)
{
    if (audio->device != 0) {
        SDL_ClearQueuedAudio(audio->device);
        SDL_CloseAudioDevice(audio->device);
    }
    *audio = (Audio){ 0 };
}
