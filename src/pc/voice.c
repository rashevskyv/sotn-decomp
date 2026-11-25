// SPDX-License-Identifier: AGPL-3.0-or-later
// Voice sets. PS1 EN plays the US XA sectors. JP and PSP EN play the clips
// voice/<set>/<ID>.pcm (raw s16le, mono, 37800 Hz) that
// tools/voicepack/extract_voice.py builds from the owner's PSP image. A
// missing clip plays the US sector. Music never comes here.
#include <game.h>
#include <psyz/cd.h>
#include <cutscene.h>
#include <sfx.h>
#include <stdio.h>
#include <stdlib.h>

int g_VoiceSet = VOICE_PS1;
const char* const g_VoiceSetNames[VOICE_COUNT] = {"ps1", "jp", "psp"};

// 1 if voice/<set>/ holds clips (358.pcm, the first voice id). PS1 is always
// there; JP and PSP EN need the owner's voice folder. A set without clips is
// not offered by the port menu and the ini cannot select it.
int Voice_SetAvailable(int v) {
    char path[32];
    FILE* f;

    if (v == VOICE_PS1) {
        return 1;
    }
    snprintf(path, sizeof(path), "voice/%s/358.pcm", g_VoiceSetNames[v]);
    f = fopen(path, "rb");
    if (f) {
        fclose(f);
    }
    return f != NULL;
}

// CdSoundCommand4, before its CdlReadN, for every XA id. Arms the clip for
// that read and returns its length in vsyncs; 0: play the US sector.
s32 Voice_Open(s32 id, s32 isVoice) {
    char path[32];
    long size;

    Psyz_CdSetPcmOverride(NULL); // drop a clip that no read took
    if (!isVoice || id < 0x358) {
        return 0;
    }
    if (g_VoiceSet == VOICE_PS1) {
        fprintf(stderr, "[voice] set=ps1 id=0x%03X file us xa: ok\n", id);
        return 0;
    }
    snprintf(path, sizeof(path), "voice/%s/%03X.pcm",
             g_VoiceSetNames[g_VoiceSet], id);
    size = Psyz_CdSetPcmOverride(path);
    fprintf(stderr, "[voice] set=%s id=0x%03X file %s: %s\n",
            g_VoiceSetNames[g_VoiceSet], id, path,
            size > 0 ? "ok" : "missing");
    // 37800 Hz x 2 bytes = 75600 bytes/s = 1260 bytes per 60 Hz vsync
    return size > 0 ? size / 1260 : 0;
}

// Typing pace of the cutscene boxes. A char waits unk17 + 1 frames (8 + 1
// at stock, SET_SPEED changes it). The PSP EN and JP-en scripts are the PSP
// game's: its engine ignores SET_SPEED and waits 2 + 1 frames, and its lines
// are longer. At our pace such a box types 2-3 times longer than its voice
// line. So while a voice line plays, each char (and each SET_WAIT before a
// char) gets its share of the time left to 90% of the clip, at most its
// stock wait. The share comes from the time left, so lag or a late voice
// start corrects itself on the next char. Boxes without voice keep the stock
// pace. SOTN_DLG_STOCK=1 keeps the stock pace always (the logs stay).
s32 Cutscene_VoiceClip(s32* start);

// stock frames from p to the last char of the box; *end: the op that ends it
static s32 BoxFrames(const u8* p, s32 speed, const u8** end) {
    static const u8 args[25] = {0, 0, 1, 1, 0, 2, 0, 2, 0, 2, 0, 0, 4,
                                0, 0, 4, 1, 1, 0, 5, 2, 0, 1, 0, 1};
    s32 t = 0, tail = 0, i;
    for (i = 0; i < 1024; i++) {
        u8 c = *p++;
        if (c >= 0x20) {
            t += tail + speed + 1;
            tail = 0;
        } else if (c == CSOP_SET_SPEED) {
            speed = *p++;
        } else if (c == CSOP_SET_WAIT) {
            tail += *p++ + 1;
        } else if (c == CSOP_END_CUTSCENE || c == CSOP_HIDE_DIALOG ||
                   c == CSOP_SET_PORTRAIT || c == CSOP_NEXT_DIALOG ||
                   c == CSOP_SET_POS || c == CSOP_CLOSE_DIALOG ||
                   c == CSOP_SCRIPT_SWITCH || c == CSOP_SCRIPT_UNKNOWN_15 ||
                   c >= LEN(args)) {
            break;
        } else {
            p += args[c];
        }
    }
    *end = p - 1;
    return t;
}

static const u8 *box_first, *box_end; // the box now typing
static s32 box_voice = -1, last_voice = -1; // vsync the voice line began
static s32 box_due, box_frames, box_t0, box_v0, stock_pace = -1;
// dialogue_skip (sotn.ini): Start types the rest of the box at once, then
// ends the wait and the voice line of the finished box
static s32 skip_ff, skip_press, skip_hook, skip_box; // vsync stamps

// frames this step (stock n) may take of the time left; n when not paced
static u8 Pace(s32 n, s32 rest) {
    s32 f;
    if (box_voice < 0 || stock_pace) {
        return n;
    }
    f = (n + 1) * (box_due - VSync(-1)) / (n + 1 + rest);
    return f > n ? n : f < 1 ? 0 : f - 1;
}

u8 Cutscene_CharTimer(u8 n, const u8* p) {
    s32 clip, start, rest, now = VSync(-1);
    const u8* end;
    if (stock_pace < 0) {
        stock_pace = getenv("SOTN_DLG_STOCK") != NULL;
    }
    if (p - 1 < box_first || p - 1 > box_end) { // the first char of a box
        if (box_voice >= 0) {
            last_voice = box_voice;
        }
        box_voice = -1;
        box_first = p - 1;
        box_frames = n + 1 + BoxFrames(p, n, &box_end);
        box_t0 = g_Timer;
        box_v0 = now;
    }
    rest = BoxFrames(p, n, &end);
    // a voice start an earlier box used is not this box's line
    if (box_voice < 0 && (clip = Cutscene_VoiceClip(&start)) > 0 &&
        start != last_voice && now - start < clip) {
        box_voice = start;
        box_due = start + clip * 9 / 10;
        fprintf(stderr,
                "[dlg] type: stock %d frames (%d ms), voice %d ms, "
                "%d ms in, typed by %d ms%s\n",
                box_frames, box_frames * 50 / 3, clip * 50 / 3,
                (now - start) * 50 / 3, clip * 15,
                stock_pace ? " (SOTN_DLG_STOCK: stock pace)" : "");
    }
    if (!rest) { // the last char
        fprintf(stderr,
                "[dlg] typed in %d frames, %d ms wall (stock %d frames); "
                "last char at voice ms %d (-1: no voice)\n",
                (s32)(g_Timer - box_t0), (now - box_v0) * 50 / 3, box_frames,
                box_voice < 0 ? -1 : (now - box_voice) * 50 / 3);
    }
    if (skip_ff && rest) {
        return 0;
    }
    return Pace(n, rest);
}

u8 Cutscene_WaitTimer(u8** pp, u8 n) {
    const u8* end;
    u8* p = *pp;
    u8 w = *(*pp)++;
    s32 rest;
    if (p <= box_first || p > box_end) { // before the box's first char
        return w;
    }
    rest = BoxFrames(p + 1, n, &end);
    if (skip_ff && rest) {
        return 0;
    }
    return rest ? Pace(w, rest) : w; // a wait after the last char stays
}

void Cutscene_SkipReset(void) {
    box_first = box_end = NULL;
    skip_ff = skip_press = skip_hook = skip_box = 0;
}

// Pads (src/dra/pads.c): with dialogue_skip on, Start is the skip button
// while a dialogue shows. It never reaches the game then (no pause).
// A dialogue shows: its script ran this or the last frames, and a box was
// open at most 2 s ago (a pause between two boxes counts).
void Cutscene_PadFilter(Pad* pad) {
    static int held, was_down; // Start taken by us; Start was down (raw)
    s32 now = VSync(-1);
    int down = (pad->pressed & PAD_START) != 0;
    int tap = down && !was_down;
    was_down = down;
    if (!g_DialogSkip || !down) {
        held = 0;
        return;
    }
    if (tap && skip_hook && now - skip_hook <= 30 && skip_box &&
        now - skip_box <= 120) {
        held = 1;
        skip_press = now;
    }
    if (held) { // until Start is up
        pad->pressed &= ~PAD_START;
        pad->tapped &= ~PAD_START;
    }
}

// Each frame of the script loop (CS_FF, include/cutscene.h), before its
// timer check. The press: text left in the box -> type it at once (the
// timers are 0 until the last char); box complete -> end the wait and stop
// the voice line, so the next op runs. Nothing outside a box is skipped.
void Cutscene_Fast(u8* timer, u8** script) {
    const u8* end;
    s32 now = VSync(-1), s;
    int inbox = box_first && *script >= box_first && *script <= box_end;
    skip_hook = now;
    if (inbox) {
        skip_box = now;
    }
    if (skip_ff && !(inbox && BoxFrames(*script, 0, &end))) {
        skip_ff = 0;
    }
    if (skip_press && now - skip_press > 3) {
        skip_press = 0;
    }
    if (skip_press && g_DialogSkip && inbox) {
        skip_press = 0;
        if (BoxFrames(*script, 0, &end)) {
            skip_ff = 1;
            fprintf(stderr, "[dlgskip] press: type the box at once\n");
        } else {
            fprintf(stderr, "[dlgskip] press: next box, timer %d, voice %s\n",
                    *timer, Cutscene_VoiceClip(&s) > 0 ? "stopped" : "none");
            *timer = 0;
            if (Cutscene_VoiceClip(&s) > 0) {
                g_api.PlaySfx(SET_STOP_MUSIC);
            }
        }
    }
    if (skip_ff) {
        *timer = 0;
    }
}
