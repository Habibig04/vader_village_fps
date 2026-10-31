#pragma once
#include "common.hpp"

// Tiny synthesized sound effects so no audio files are needed.
struct Sfx { Sound s[3]; int i = 0; bool ok = false; };

inline Sfx MakeSfx(float dur, float f0, float noise, float decay, float vol) {
    Sfx x;
    if (!IsAudioDeviceReady()) return x;
    int n = (int)(22050 * dur);
    for (int k = 0; k < 3; k++) {
        Wave w = {};
        w.frameCount = n; w.sampleRate = 22050; w.sampleSize = 16; w.channels = 1;
        w.data = MemAlloc(n * 2);
        short* d = (short*)w.data; float ph = 0;
        for (int i = 0; i < n; i++) {
            float t = (float)i / n;
            ph += f0 * (1.f - 0.7f * t) / 22050.f;
            float s = (sinf(ph * 6.2832f) * (1 - noise) + ((rand() % 2000) / 1000.f - 1.f) * noise) * expf(-t * decay) * vol;
            d[i] = (short)(Clamp(s, -1.f, 1.f) * 30000);
        }
        x.s[k] = LoadSoundFromWave(w);
        UnloadWave(w);
    }
    x.ok = true;
    return x;
}
inline void Play(Sfx& x, float vol = 1.f) {
    if (!x.ok) return;
    SetSoundVolume(x.s[x.i], vol);
    PlaySound(x.s[x.i]);
    x.i = (x.i + 1) % 3;
}
