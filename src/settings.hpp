#pragma once
#include "common.hpp"
#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

enum { Q_LOW, Q_MEDIUM, Q_HIGH, Q_ULTRA, Q_COUNT };
static const char* QNAMES[Q_COUNT] = {"LOW", "MEDIUM", "HIGH", "ULTRA"};
static const int QSCALE[Q_COUNT] = {60, 80, 100, 100};
static const float QDRAW[Q_COUNT] = {75.f, 140.f, 260.f, 520.f};
static const int FPS_OPTS[5] = {0, 30, 60, 120, 144};

struct Settings {
    float sens = 1.f;
    int fov = 70;
    int quality = Q_MEDIUM;
    int scalePct = 80;
    int fpsIdx = 0;
    int volume = 80;
    bool bob = true;
    bool showFps = false;
    bool autoQ = true;
    std::string name = "PLAYER";
};

inline long long systemRamMB() {
#if defined(__unix__) && defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
    long pages = sysconf(_SC_PHYS_PAGES);
    long psz = sysconf(_SC_PAGESIZE);
    if (pages > 0 && psz > 0) return (long long)pages * psz / (1024LL * 1024LL);
#endif
    return 0;
}

inline int detectQuality() {
#if defined(__aarch64__) || defined(__arm__) || defined(_M_ARM64)
    const bool arm = true;
#else
    const bool arm = false;
#endif
    const long long mb = systemRamMB();
    if (arm && (mb == 0 || mb <= 8192)) return Q_LOW;
    if (mb > 0 && mb <= 3072) return Q_LOW;
    if (mb > 0 && mb <= 6144) return Q_MEDIUM;
    if (mb > 6144) return Q_HIGH;
    return Q_MEDIUM;
}

inline void clampSettings(Settings& s) {
    s.sens = Clamp(s.sens, 0.2f, 3.f);
    s.fov = std::clamp(s.fov, 55, 100);
    s.quality = std::clamp(s.quality, 0, Q_COUNT - 1);
    s.scalePct = std::clamp(s.scalePct, 50, 100);
    s.fpsIdx = std::clamp(s.fpsIdx, 0, 4);
    s.volume = std::clamp(s.volume, 0, 100);
    if (s.name.empty()) s.name = "PLAYER";
    if (s.name.size() > 12) s.name.resize(12);
}

inline void saveSettings(const Settings& s, const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "sens %.3f\nfov %d\nquality %d\nscale %d\nfps %d\nvolume %d\nbob %d\nshowfps %d\nautoq %d\nname %s\n",
            s.sens, s.fov, s.quality, s.scalePct, s.fpsIdx, s.volume, s.bob ? 1 : 0, s.showFps ? 1 : 0, s.autoQ ? 1 : 0, s.name.c_str());
    fclose(f);
}

inline bool loadSettings(Settings& s, const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char line[128];
    int found = 0;
    while (fgets(line, sizeof line, f)) {
        found++;
        if (!strncmp(line, "name ", 5)) {
            std::string n(line + 5);
            while (!n.empty() && (n.back() == '\n' || n.back() == '\r')) n.pop_back();
            s.name = n;
            continue;
        }
        char k[32];
        float v = 0;
        if (sscanf(line, "%31s %f", k, &v) != 2) continue;
        if (!strcmp(k, "sens")) s.sens = v;
        else if (!strcmp(k, "fov")) s.fov = (int)v;
        else if (!strcmp(k, "quality")) s.quality = (int)v;
        else if (!strcmp(k, "scale")) s.scalePct = (int)v;
        else if (!strcmp(k, "fps")) s.fpsIdx = (int)v;
        else if (!strcmp(k, "volume")) s.volume = (int)v;
        else if (!strcmp(k, "bob")) s.bob = v != 0.f;
        else if (!strcmp(k, "showfps")) s.showFps = v != 0.f;
        else if (!strcmp(k, "autoq")) s.autoQ = v != 0.f;
    }
    fclose(f);
    clampSettings(s);
    return found > 0;
}
