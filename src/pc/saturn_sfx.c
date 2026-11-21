// SPDX-License-Identifier: AGPL-3.0-or-later
// Saturn-only sounds of the Saturn stages 15 (Underground Garden) and 16
// (Cursed Prison). The stage code plays the Saturn ids (0x8F6..0x916, past
// the PS1 table). tools/saturn/saturn_audio.py renders them from the owner's
// disc into data/ST/<STAGE>/sfx/<ID>.wav, 44100 Hz s16 stereo. A WAV plays on
// a second SDL3 audio device, mixed by SDL beside the psyz SPU stream. No
// WAV: the PS1 stand-in below plays through the PS1 sound engine.
// The boss track id SATURN_BOSS_MUSIC gets the XA configs of the stage music
// here; music.c plays its WAV.
#include <game.h>
#include <sfx.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include "pc.h"

#define FIRST 0x8F6 // the first id past the PS1 table (SFX_LAST 0x8F5)
#define LAST 0x916  // the Saturn PlaySfx accepts 0x600..0x916
#define N_IDS (LAST - FIRST + 1)
#define N_VOICES 8

extern u16 g_SfxVolumeMultiplier; // dra: the game's sfx fade, 0x7F = full
extern XaMusicConfig g_XaMusicConfigs[];

// Saturn id -> PS1 stand-in (the sound the stage played before)
static const struct {
    s16 id, ps1;
} s_StandIns[] = {
    {0x8F7, SFX_EXPLODE_FAST_B},         // boss bomb
    {0x8F8, SFX_SKELETON_DEATH_C},       // boss death
    {0x8FA, SFX_SCRAPE_B},               // tree face falls
    {0x8FB, SFX_BLOODY_ZOMBIE_SPLATTER}, // tree face bursts
    {0x8FC, SFX_BLOODY_ZOMBIE_SPLATTER}, // tree hurt
    {0x8FD, SFX_STONE_ROSE_DEATH},       // tree death
    {0x8FE, SFX_WEAPON_SWISH_C},         // gargoyle claw
    {0x8FF, SFX_WALL_DEBRIS_B},          // gargoyle debris
    {0x900, SFX_MAGIC_WEAPON_APPEAR_B},  // Specter vanishes
    {0x901, SFX_MAGIC_NOISE_SWEEP},      // Specter cry
    {0x903, SFX_FIRE_SHOT},              // Will-o'-Wisp burst
    {0x90B, SFX_VENUS_WEED_DEATH},       // Man-eater bursts
    {0x90C, SFX_SCRAPE_B},               // Man-eater opens
    {0x90D, SFX_WEAPON_STAB_B},          // Man-eater snaps
    {0x90E, SFX_SKELETON_DEATH_B},       // Gardener dies
    {0x90F, SFX_WEAPON_SWISH_A},         // Gardener shears
    {0x911, SFX_MERMAN_DEATH},           // Leaper dies
    {0x912, SFX_WATER_SPLASH_JUMP},      // Leaper jumps out
    {0x913, SFX_WATER_SPLASH_MOVE},      // Leaper falls in
};

typedef struct {
    u8 tried;  // 1: looked for the file
    Uint8* pcm; // NULL: no file
    Uint32 len;
} Wav;

static Wav s_Wavs[2][N_IDS]; // stage 15, stage 16
static SDL_AudioDeviceID s_Device;
static SDL_AudioStream* s_Voices[N_VOICES];
static bool s_DeviceFailed;

static const char* StageDir(int stage) {
    return stage ? "STAGE16" : "STAGE15";
}

// 0: stage 15, 1: stage 16, -1: not a Saturn stage with sounds
static int SaturnStage(void) {
    return (int)g_StageId == STAGE_ID_SATURN_15   ? 0
           : (int)g_StageId == STAGE_ID_SATURN_16 ? 1
                                                  : -1;
}

static s16 StandIn(s16 id) {
    int i;
    for (i = 0; i < LEN(s_StandIns); i++) {
        if (s_StandIns[i].id == id) {
            return s_StandIns[i].ps1;
        }
    }
    return 0; // no stand-in: nothing plays
}

static bool OpenDevice(void) {
    SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, 44100};
    int i;

    if (s_Device || s_DeviceFailed) {
        return s_Device != 0;
    }
    s_Device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    for (i = 0; s_Device && i < N_VOICES; i++) {
        s_Voices[i] = SDL_CreateAudioStream(&spec, NULL);
        if (!s_Voices[i] || !SDL_BindAudioStream(s_Device, s_Voices[i])) {
            SDL_CloseAudioDevice(s_Device); // also unbinds the streams
            s_Device = 0;
        }
    }
    if (!s_Device) {
        s_DeviceFailed = true;
        fprintf(stderr, "[satsfx] no audio device: %s; PS1 stand-ins play\n",
                SDL_GetError());
    }
    return s_Device != 0;
}

static Wav* Load(int stage, s16 id) {
    Wav* w = &s_Wavs[stage][id - FIRST];
    char path[256];
    SDL_AudioSpec spec;

    if (w->tried) {
        return w;
    }
    w->tried = 1;
    snprintf(path, sizeof(path), SOTN_DATA_DIR "/ST/%s/sfx/%03X.wav",
             StageDir(stage), id);
    if (!SDL_LoadWAV(path, &spec, &w->pcm, &w->len)) {
        w->pcm = NULL;
        fprintf(stderr, "[satsfx] %s %03X: no file %s, PS1 stand-in %03X\n",
                StageDir(stage), id, path, StandIn(id));
    } else if (spec.format != SDL_AUDIO_S16 || spec.channels != 2 ||
               spec.freq != 44100) {
        fprintf(stderr, "[satsfx] %s: not 44100 Hz s16 stereo, PS1 stand-in\n",
                path);
        SDL_free(w->pcm);
        w->pcm = NULL;
    }
    return w;
}

// one voice: a free one, else the one nearest to its end
static SDL_AudioStream* Voice(void) {
    int i, best = 0, queued, least = 0x7FFFFFFF;
    for (i = 0; i < N_VOICES; i++) {
        queued = SDL_GetAudioStreamQueued(s_Voices[i]);
        if (queued < least) {
            least = queued;
            best = i;
        }
    }
    SDL_ClearAudioStream(s_Voices[best]);
    return s_Voices[best];
}

// vol 0..0x7F; pan -8 (left) .. 8 (right), as PlaySfxVolPan
static void Play(Wav* w, s32 vol, s32 pan) {
    // ponytail: unity gain, not matched to the loudness of the PS1 SPU mix;
    // scale it here if the Saturn sounds stand out
    int gain = vol * g_SfxVolumeMultiplier; // 1/(0x7F*0x7F)
    int left = pan > 0 ? gain * (8 - pan) / 8 : gain;
    int right = pan < 0 ? gain * (8 + pan) / 8 : gain;
    Uint32 i, n = w->len / 2;
    short* src = (short*)w->pcm;
    short* buf = malloc(w->len);

    if (!buf) {
        return;
    }
    for (i = 0; i + 1 < n; i += 2) {
        buf[i] = src[i] * left / (0x7F * 0x7F);
        buf[i + 1] = src[i + 1] * right / (0x7F * 0x7F);
    }
    SDL_PutAudioStreamData(Voice(), buf, (int)w->len);
    free(buf);
}

// the boss track: its XA configs are the stage music's (the same disc file
// and volume); music.c plays the WAV in place of the XA. No WAV: the stage
// music keeps playing, as before.
static void BossMusic(s16* id) {
    char path[256];
    FILE* f;

    snprintf(path, sizeof(path), SOTN_DATA_DIR "/ST/STAGE15/music/%03X.wav",
             SATURN_BOSS_MUSIC);
    f = SaturnStage() == 0 ? fopen(path, "rb") : NULL;
    if (!f) {
        fprintf(stderr, "[satsfx] boss music %03X: no file %s, the stage "
                        "music %03X plays\n",
                *id, path, g_StageMusicId);
        *id = (s16)g_StageMusicId;
        return;
    }
    fclose(f);
    g_XaMusicConfigs[SATURN_BOSS_MUSIC - 0x300] =
        g_XaMusicConfigs[g_StageMusicId - 0x300];
    g_XaMusicConfigs[SATURN_BOSS_MUSIC + 1 - 0x300] =
        g_XaMusicConfigs[g_StageMusicId + 1 - 0x300];
}

bool SaturnSfx_Play(s16* id, s32 vol, s32 pan) {
    int stage;
    Wav* w;

    if (*id == SATURN_BOSS_MUSIC) {
        BossMusic(id);
        return false;
    }
    if (*id < FIRST || *id > LAST) {
        return false; // a PS1 id: the PS1 engine plays it
    }
    stage = SaturnStage();
    w = stage >= 0 && OpenDevice() ? Load(stage, *id) : NULL;
    if (!w || !w->pcm) {
        *id = StandIn(*id);
        return *id == 0;
    }
    Play(w, vol, pan);
    fprintf(stderr, "[satsfx] %s %03X: file ST/%s/sfx/%03X.wav, vol %d pan %d\n",
            StageDir(stage), *id, StageDir(stage), *id, vol, pan);
    return true;
}
