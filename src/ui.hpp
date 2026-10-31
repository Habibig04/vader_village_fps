#pragma once
#include "common.hpp"

inline float gUi = 1.f;
inline int S(float v) { return (int)(v * gUi + 0.5f); }

inline void shadowText(const char* t, int x, int y, int fs, Color c) {
    DrawText(t, x + 2, y + 2, fs, C(0, 0, 0, 170));
    DrawText(t, x, y, fs, c);
}

inline void centerText(const char* t, int y, int fs, Color c, int W) {
    shadowText(t, W / 2 - MeasureText(t, fs) / 2, y, fs, c);
}

inline bool button(int idx, int& sel, Rectangle r, const char* label, int fs) {
    bool hov = CheckCollisionPointRec(GetMousePosition(), r);
    if (hov) sel = idx;
    bool on = sel == idx;
    DrawRectangleRec(r, on ? C(255, 190, 60, 235) : C(12, 14, 22, 205));
    DrawRectangleLines((int)r.x, (int)r.y, (int)r.width, (int)r.height, on ? C(255, 240, 180) : C(100, 105, 125));
    int tw = MeasureText(label, fs);
    DrawText(label, (int)(r.x + r.width / 2) - tw / 2, (int)(r.y + r.height / 2) - fs / 2, fs, on ? C(20, 20, 20) : WHITE);
    return (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) || (on && IsKeyPressed(KEY_ENTER));
}

inline int optRow(int idx, int& sel, Rectangle r, const char* label, const char* value, int fs) {
    bool hov = CheckCollisionPointRec(GetMousePosition(), r);
    if (hov) sel = idx;
    bool on = sel == idx;
    DrawRectangleRec(r, on ? C(255, 190, 60, 225) : C(12, 14, 22, 205));
    DrawRectangleLines((int)r.x, (int)r.y, (int)r.width, (int)r.height, on ? C(255, 240, 180) : C(100, 105, 125));
    int ty = (int)(r.y + r.height / 2) - fs / 2;
    DrawText(label, (int)r.x + S(20), ty, fs, on ? C(20, 20, 20) : WHITE);
    int vw = MeasureText(value, fs);
    DrawText(value, (int)(r.x + r.width) - vw - S(20), ty, fs, on ? C(20, 20, 20) : C(255, 225, 120));
    if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return GetMousePosition().x < r.x + r.width / 2 ? -1 : 1;
    if (on && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D))) return 1;
    if (on && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A))) return -1;
    return 0;
}

inline void navKeys(int& sel, int n) {
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) sel = (sel + 1) % n;
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) sel = (sel + n - 1) % n;
}

inline void bar(Rectangle r, float frac, Color fg, Color bg) {
    DrawRectangleRec(r, bg);
    frac = Clamp(frac, 0.f, 1.f);
    DrawRectangle((int)r.x, (int)r.y, (int)(r.width * frac), (int)r.height, fg);
}
