// SPDX-License-Identifier: AGPL-3.0-or-later
// Voice sets. PS1 EN plays the US XA sectors. JP and PSP EN play the clips
// voice/<set>/<ID>.pcm (raw s16le, mono, 37800 Hz) that
// tools/voicepack/extract_voice.py builds from the owner's PSP image. A
// missing clip plays the US sector. Music never comes here.
#include <game.h>
#include <psyz/cd.h>
#include <stdio.h>

int g_VoiceSet = VOICE_PS1;
const char* const g_VoiceSetNames[VOICE_COUNT] = {"ps1", "jp", "psp"};

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
