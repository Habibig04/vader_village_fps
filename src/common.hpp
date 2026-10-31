#pragma once
#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <algorithm>

struct Box { Vector3 mn, mx; };
struct Particle { Vector3 p, v; float life, size; Color c; };

inline Vector3 V(float x, float y, float z) { return Vector3{x, y, z}; }
inline Vector2 V2(float x, float y) { return Vector2{x, y}; }
inline Rectangle RR(float x, float y, float w, float h) { return Rectangle{x, y, w, h}; }
inline Vector3 operator+(const Vector3& a, const Vector3& b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vector3 operator-(const Vector3& a, const Vector3& b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
inline Vector3 operator*(const Vector3& a, float s) { return V(a.x * s, a.y * s, a.z * s); }
inline Vector3 operator-(const Vector3& a) { return V(-a.x, -a.y, -a.z); }

inline Color C(int r, int g, int b, int a = 255) {
    auto k = [](int v) { return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v)); };
    return Color{k(r), k(g), k(b), k(a)};
}

inline float& at(Vector3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }
inline float at(const Vector3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }
inline float frand() { return (float)GetRandomValue(0, 10000) / 10000.f; }
inline float frange(float a, float b) { return a + (b - a) * frand(); }
inline float gauss() { return (frand() + frand() + frand() - 1.5f) * 0.8f; }

inline float angDiff(float a, float b) {
    float d = fmodf(b - a + PI, 2 * PI);
    if (d < 0) d += 2 * PI;
    return d - PI;
}

inline float smooth01(float x) {
    float t = Clamp(x, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

inline bool rayBox(Vector3 o, Vector3 d, const Box& b, float& tn) {
    float t0 = 0, t1 = 1e30f;
    for (int i = 0; i < 3; i++) {
        float oi = at(o, i), di = at(d, i), lo = at(b.mn, i), hi = at(b.mx, i);
        if (fabsf(di) < 1e-8f) { if (oi < lo || oi > hi) return false; continue; }
        float a = (lo - oi) / di, c = (hi - oi) / di;
        if (a > c) std::swap(a, c);
        if (a > t0) t0 = a;
        if (c < t1) t1 = c;
        if (t0 > t1) return false;
    }
    tn = t0;
    return true;
}
