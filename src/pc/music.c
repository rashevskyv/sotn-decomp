// SPDX-License-Identifier: AGPL-3.0-or-later
// Music files. music/<ID>.mp3 or music/<ID>.flac (44100 Hz, ID = the three
// hex digits of the start id of the XA track, 30F.mp3) plays in place of the
// XA music. tools/music/ost_map.py builds the folder and music/loops.txt.
// No file, or music_files = 0 in sotn.ini: the XA plays as on the PS1.
//
// A worker thread decodes the whole file to s16 stereo while it plays; psyz
// plays it through the CD input (volume, fades, mute and pause as on XA).
// Looping tracks jump from the loop end to the loop start with a 40 ms
// crossfade; a track without loop points loops the whole file. The game's
// timers (D_8013AE90, D_8013AEF4) still run: their loop reads keep the file
// at its position. The pause stack of CdSoundCommand12 keeps the position.
// The Saturn boss track SATURN_BOSS_MUSIC plays data/ST/STAGE15/music/535.wav
// (tools/saturn/saturn_audio.py) the same way, also with music_files = 0.
#include <game.h>
#include <psyz/audio.h>
#include <psyz/cd.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// dr_libs by David Reid, public domain or MIT-0 (see README, Credits)
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_OGG
#include "dr_flac.h"

extern int g_MusicFiles; // wide.c, sotn.ini music_files

#define RATE 44100
#define XFADE (RATE * 40 / 1000)
#define CHUNK 4096 // frames per decode step and per publish

typedef struct {
    s32 id; // 0: free
    short* pcm;
    size_t ready, frames; // decoded frames (under the audio lock), allocated
    size_t start, loop_a, loop_b; // loops.txt; loop_b 0: no loop points
    size_t a, b;                  // as played: b 0 plays once
    int level;                    // gain in 1/256 (the XA's loudness)
    drmp3 mp3;
    drflac* flac;
    int channels;
    SDL_Thread* thread;
    SDL_AtomicInt stop;
} Track;

// playing, armed, two paused (the game's pause stack holds two)
#define N_TRACKS 4
static Track tracks[N_TRACKS];
static struct {
    Track* track; // NULL: XA or nothing was playing
    size_t pos;
} saved[2];
static u8 missing_logged[0x41];

static int SDLCALL Decode(void* arg) {
    Track* t = arg;
    short buf[CHUNK * 2];
    while (t->ready < t->frames && !SDL_GetAtomicInt(&t->stop)) {
        size_t want = t->frames - t->ready, got, i;
        short* dst = t->pcm + t->ready * 2;
        if (want > CHUNK) {
            want = CHUNK;
        }
        if (t->channels == 2) {
            got = t->flac ? drflac_read_pcm_frames_s16(t->flac, want, dst)
                          : drmp3_read_pcm_frames_s16(&t->mp3, want, dst);
        } else {
            got = t->flac ? drflac_read_pcm_frames_s16(t->flac, want, buf)
                          : drmp3_read_pcm_frames_s16(&t->mp3, want, buf);
            for (i = 0; i < got; i++) {
                dst[i * 2] = dst[i * 2 + 1] = buf[i];
            }
        }
        if (!got) {
            // shorter than its header said: the rest is silence
            got = t->frames - t->ready;
            memset(dst, 0, got * 4);
        }
        if (t->level != 256) {
            for (i = 0; i < got * 2; i++) {
                int v = dst[i] * t->level / 256;
                dst[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
            }
        }
        Psyz_AudioLock();
        t->ready += got;
        Psyz_AudioUnlock();
    }
    if (t->flac) {
        drflac_close(t->flac);
        t->flac = NULL;
    } else {
        drmp3_uninit(&t->mp3);
    }
    return 0;
}

static void Free(Track* t) {
    SDL_SetAtomicInt(&t->stop, 1);
    SDL_WaitThread(t->thread, NULL);
    free(t->pcm);
    memset(t, 0, sizeof(*t));
}

// frees the tracks that psyz does not read and the pause stack does not hold
static void Collect(void) {
    int i;
    for (i = 0; i < N_TRACKS; i++) {
        Track* t = &tracks[i];
        if (t->id && !Psyz_CdMusicUses(t->pcm) && saved[0].track != t &&
            saved[1].track != t) {
            Free(t);
        }
    }
}

// music/loops.txt: "ID start loop_start loop_end [level]", frames at 44100 Hz
// and the gain in 1/256 (ost_map.py matches the soundtrack to the XA level)
static void Loops(Track* t, const char* path) {
    char line[96];
    unsigned id, s, a, b, l;
    int n;
    FILE* f = fopen(path, "r");
    if (!f) {
        return;
    }
    while (fgets(line, sizeof(line), f)) {
        n = sscanf(line, "%x %u %u %u %u", &id, &s, &a, &b, &l);
        if (n >= 4 && (s32)id == t->id) {
            t->start = s;
            t->loop_a = a;
            t->loop_b = b;
            t->level = n == 5 && l > 0 && l <= 1024 ? (int)l : 256;
        }
    }
    fclose(f);
}

// the Saturn boss track (tools/saturn/saturn_audio.py): one WAV, read whole
#define SATURN_MUSIC_DIR SOTN_DATA_DIR "/ST/STAGE15/music"
static bool OpenSaturn(Track* t) {
    char path[256];
    SDL_AudioSpec spec;
    Uint8* wav;
    Uint32 len;

    snprintf(path, sizeof(path), SATURN_MUSIC_DIR "/%03X.wav", t->id);
    if (!SDL_LoadWAV(path, &spec, &wav, &len)) {
        return false;
    }
    t->pcm = spec.format == SDL_AUDIO_S16 && spec.channels == 2 &&
                     spec.freq == RATE && len >= RATE * 4
                 ? malloc(len)
                 : NULL;
    if (t->pcm) {
        memcpy(t->pcm, wav, len);
        t->frames = t->ready = len / 4;
        t->level = 256;
        Loops(t, SATURN_MUSIC_DIR "/loops.txt");
        if (t->loop_b > t->frames || t->loop_a >= t->loop_b) {
            t->loop_a = t->loop_b = 0;
        }
        fprintf(stderr, "[music] %03X: file %s, %.1f s, %s\n", t->id, path,
                (double)t->frames / RATE,
                t->loop_b ? "loop points" : "no loop points");
    } else {
        fprintf(stderr, "[music] %03X: %s: not 44100 Hz s16 stereo\n", t->id,
                path);
    }
    SDL_free(wav);
    return t->pcm != NULL;
}

static Track* Open(s32 id) {
    char path[32];
    unsigned rate = 0;
    drmp3_uint64 n = 0;
    Track* t = NULL;
    int i;
    Uint64 t0 = SDL_GetTicksNS();

    for (i = 0; i < N_TRACKS && !t; i++) {
        t = tracks[i].id ? NULL : &tracks[i];
    }
    if (!t) {
        return NULL; // all held: the XA plays
    }
    t->id = id;
    if (id == SATURN_BOSS_MUSIC) {
        if (OpenSaturn(t)) {
            return t;
        }
        memset(t, 0, sizeof(*t));
        return NULL;
    }
    snprintf(path, sizeof(path), "music/%03X.flac", id);
    t->flac = drflac_open_file(path, NULL);
    if (t->flac) {
        rate = t->flac->sampleRate;
        t->channels = t->flac->channels;
        n = t->flac->totalPCMFrameCount;
    } else {
        snprintf(path, sizeof(path), "music/%03X.mp3", id);
        if (!drmp3_init_file(&t->mp3, path, NULL)) {
            t->id = 0;
            return NULL;
        }
        rate = t->mp3.sampleRate;
        t->channels = t->mp3.channels;
        n = drmp3_get_pcm_frame_count(&t->mp3); // the LAME header: no scan
    }
    if (rate != RATE || t->channels < 1 || t->channels > 2 || n < RATE) {
        fprintf(stderr, "[music] %03X: %s: %u Hz, %d channels, %u frames: "
                        "not 44100 Hz mono or stereo, the XA plays\n",
                id, path, rate, t->channels, (unsigned)n);
        if (t->flac) {
            drflac_close(t->flac);
        } else {
            drmp3_uninit(&t->mp3);
        }
        memset(t, 0, sizeof(*t));
        return NULL;
    }
    t->frames = (size_t)n;
    t->level = 256;
    Loops(t, "music/loops.txt");
    if (t->start >= t->frames) {
        t->start = 0;
    }
    if (t->loop_b > t->frames || t->loop_a >= t->loop_b) {
        t->loop_a = t->loop_b = 0;
    }
    if (t->loop_b) {
        t->frames = t->loop_b; // psyz never reads past the loop end
    }
    t->pcm = malloc(t->frames * 4);
    t->thread = t->pcm ? SDL_CreateThread(Decode, "music", t) : NULL;
    if (!t->thread) {
        fprintf(stderr, "[music] %03X: no memory or thread, the XA plays\n",
                id);
        free(t->pcm);
        t->pcm = NULL;
        if (t->flac) {
            drflac_close(t->flac);
        } else {
            drmp3_uninit(&t->mp3);
        }
        memset(t, 0, sizeof(*t));
        return NULL;
    }
    fprintf(stderr,
            "[music] %03X: file %s, %.1f s, %s, level %d/256, opened in "
            "%.1f ms\n",
            id, path, (double)t->frames / RATE,
            t->loop_b ? "loop points" : "no loop points", t->level,
            (SDL_GetTicksNS() - t0) / 1e6);
    return t;
}

// the next read plays the XA (drops a file that no read took)
static void Disarm(void) { Psyz_CdSetMusic(NULL, NULL, 0, 0, 0, 0, 0); }

// CdSoundCommand4, before its CdlReadN. mode: the XA mode (2 plays once).
// Arms the file for that read and returns its length in vsyncs from the
// start point; 0: no file, the XA plays.
s32 Music_Start(s32 id, s32 mode) {
    Track* t = NULL;
    int i;

    Disarm();
    if (id < 0x340) {
        // CdSoundCommand4 empties the pause stack for these ids
        saved[0].track = saved[1].track = NULL;
    }
    // the Saturn boss track has no disc music to keep: music_files 0 too
    if (id != SATURN_BOSS_MUSIC &&
        (!g_MusicFiles || id < 0x300 || id > 0x340)) {
        return 0;
    }
    Collect();
    for (i = 0; i < N_TRACKS && !t; i++) {
        t = tracks[i].id == id ? &tracks[i] : NULL; // still held: no decode
    }
    if (!t) {
        t = Open(id);
    }
    if (!t) {
        if (id == SATURN_BOSS_MUSIC) {
            fprintf(stderr, "[music] %03X: no file, the XA plays\n", id);
        } else if (!missing_logged[id - 0x300]) {
            missing_logged[id - 0x300] = 1;
            fprintf(stderr, "[music] %03X: no file, the XA plays\n", id);
        }
        return 0;
    }
    t->a = t->loop_a;
    t->b = t->loop_b;
    if (mode == 2) {
        t->a = t->b = 0; // one-shot
    } else if (!t->b) {
        t->b = t->frames; // no loop points: the whole file
    }
    if (t->b && t->a <= t->start && t->b > t->start + 2 * XFADE) {
        // the loop goes back to the start point: psyz fades into the frames
        // before loop_a, so loop_a = start + XFADE fades into the start, not
        // into the lead-in silence of the file
        t->a = t->start + XFADE;
    }
    Psyz_CdSetMusic(t->pcm, &t->ready, t->frames, t->start, t->a, t->b, XFADE);
    fprintf(stderr, "[music] %03X start at audio frame %u, mode %d: %s\n", id,
            Psyz_SpuTicks(), mode, t->b ? "loops" : "plays once");
    return (s32)((t->frames - t->start + 734) / 735);
}

// wall clock for the start-latency logs (the clock of psyz's logs)
double Music_WallMs(void) { return SDL_GetTicksNS() / 1e6; }

// 1 while a music file plays (or is armed)
bool Music_Playing(void) {
    int i;
    for (i = 0; i < N_TRACKS; i++) {
        if (tracks[i].id && Psyz_CdMusicUses(tracks[i].pcm)) {
            return true;
        }
    }
    return false;
}

// CdSoundCommand6: the loop read keeps the file at its position. 1: a file
// plays, 0: the XA plays.
s32 Music_Loop(void) {
    if (!Music_Playing()) {
        Disarm();
        return 0;
    }
    Psyz_CdKeepMusic();
    return 1;
}

// CdSoundCommand12, after CdlPause: slot = the game's pause stack entry
void Music_Save(s32 slot) {
    int i;
    if (slot < 0 || slot > 1) {
        return;
    }
    saved[slot].track = NULL;
    for (i = 0; i < N_TRACKS; i++) {
        long pos = tracks[i].id ? Psyz_CdMusicPos(tracks[i].pcm) : -1;
        if (pos >= 0) {
            saved[slot].track = &tracks[i];
            saved[slot].pos = (size_t)pos;
            fprintf(stderr, "[music] %03X paused at %.3f s (slot %d)\n",
                    tracks[i].id, (double)pos / RATE, slot);
        }
    }
}

// CdSoundStartXaPlayback, before its CdlReadN: 1 = the paused file resumes
// at its position, 0 = the XA resumes by sector
s32 Music_Resume(s32 slot) {
    Track* t;
    if (slot < 0 || slot > 1 || !saved[slot].track) {
        Disarm();
        return 0;
    }
    t = saved[slot].track; // kept: the game repeats this step while busy
    Psyz_CdSetMusic(t->pcm, &t->ready, t->frames, saved[slot].pos, t->a, t->b,
                    XFADE);
    fprintf(stderr, "[music] %03X resumed at %.3f s (slot %d)\n", t->id,
            (double)saved[slot].pos / RATE, slot);
    return 1;
}
