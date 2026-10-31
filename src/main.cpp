#include <ctime>
#include <string>
#include "common.hpp"
#include "model.hpp"
#include "audio.hpp"
#include "weapons.hpp"
#include "settings.hpp"
#include "map.hpp"
#include "match.hpp"
#include "ui.hpp"

enum Scene { SC_HOME, SC_MAPSEL, SC_MODESEL, SC_DIFFSEL, SC_LOADING, SC_PLAY, SC_PAUSE, SC_SETTINGS, SC_END };

static const char* TIPS[] = {
    "Headshots deal 2.5 times the normal damage.",
    "Reload with R before the magazine runs dry.",
    "Hold right mouse to aim down sights or scope in.",
    "Press TAB at any time to open the scoreboard.",
    "Lower the render scale in Settings for a smoother framerate.",
    "Team deathmatch ends the moment one team reaches the point target.",
    "Single mode ends when the clock runs out. The most kills wins.",
};
static const int TIP_COUNT = 7;

static std::string findAssets() {
    const char* c[] = {"assets", "../assets", "../../assets"};
    for (auto s : c) if (FileExists((std::string(s) + "/models/vader.mdl").c_str())) return s;
    return std::string(GetApplicationDirectory()) + "assets";
}

static Camera3D mapCam(const Map& m, float t, float e) {
    Camera3D c{};
    float u = Clamp(e, 0.f, 1.f);
    float R = m.radius * (1.25f - 0.5f * u);
    float H = 58.f - 44.f * u + 3.f * sinf(t * 0.3f);
    float ang = t * 0.16f;
    c.position = V(m.mid.x + cosf(ang) * R, H, m.mid.z + sinf(ang) * R);
    c.target = V(m.mid.x, 2.f + 4.f * (1.f - u), m.mid.z);
    c.up = V(0, 1, 0);
    c.fovy = 55.f;
    c.projection = CAMERA_PERSPECTIVE;
    return c;
}

struct Game {
    std::string root, cfgPath;
    Settings st;
    Map map;
    Match match;
    Sounds snd;
    Model vader{};
    Model wm[4]{};
    Texture2D title{};
    RenderTexture2D sceneRT{}, vmRT{};
    int rtW = 0, rtH = 0;
    Scene scene = SC_HOME, retScene = SC_HOME;
    int selMap = 0, previewMap = -1, selMode = MODE_TDM5, selDiff = 1, selTgt = 2, selTime = 1;
    int sHome = 0, sMap = 0, sMode = 0, sDiff = 1, sPause = 0, sSet = 0, sEnd = 0;
    float bgT = 0, bgCycle = 14.f, mapT = 0;
    float loadT = 0, loadShown = 0;
    bool loadStarted = false;
    float msgT = 0;
    std::string msg;
    float perfT = 0, sinceStart = 0, cooldown = 0;
    int perfN = 0;
    bool quit = false;

    void setMsg(const std::string& m, float t = 2.5f) { msg = m; msgT = t; }

    void applySettings() {
        SetMasterVolume(st.volume / 100.f);
        SetTargetFPS(FPS_OPTS[st.fpsIdx]);
        if (map.ready) map.applyFilter(st.quality <= Q_MEDIUM);
    }

    void makeRT(bool force) {
        int W = GetScreenWidth(), H = GetScreenHeight();
        int rw = std::max(320, W * st.scalePct / 100), rh = std::max(240, H * st.scalePct / 100);
        if (!force && rw == rtW && rh == rtH) return;
        if (rtW) { UnloadRenderTexture(sceneRT); UnloadRenderTexture(vmRT); }
        sceneRT = LoadRenderTexture(rw, rh);
        vmRT = LoadRenderTexture(rw, rh);
        SetTextureFilter(sceneRT.texture, TEXTURE_FILTER_BILINEAR);
        rtW = rw; rtH = rh;
    }

    bool ensureMap(int idx) {
        if (map.id == idx && map.ready) return true;
        if (!map.beginLoad(idx, root)) { setMsg("MAP FILES COULD NOT BE FOUND"); return false; }
        while (!map.ready && !map.failed) map.step();
        if (map.failed) { setMsg("MAP FAILED TO LOAD"); return false; }
        map.applyFilter(st.quality <= Q_MEDIUM);
        return true;
    }

    void init() {
        BeginDrawing();
        ClearBackground(BLACK);
        DrawText("Loading...", 40, 40, 30, WHITE);
        EndDrawing();
        root = findAssets();
        cfgPath = std::string(GetApplicationDirectory()) + "settings.cfg";
        if (!loadSettings(st, cfgPath.c_str())) {
            st.quality = detectQuality();
            st.scalePct = QSCALE[st.quality];
        }
        clampSettings(st);
        title = LoadTexture((root + "/ui/title.png").c_str());
        vader = LoadMDL(root + "/models", "vader");
        for (int i = 0; i < 4; i++) wm[i] = LoadMDL(root + "/models", WDEFS[i].file);
        snd.shot[0] = MakeSfx(0.14f, 120, 0.8f, 18, 0.7f);
        snd.shot[1] = MakeSfx(0.35f, 70, 0.85f, 9, 0.9f);
        snd.shot[2] = MakeSfx(0.5f, 55, 0.7f, 7, 1.0f);
        snd.shot[3] = MakeSfx(0.09f, 160, 0.85f, 25, 0.6f);
        snd.hit = MakeSfx(0.05f, 900, 0.2f, 30, 0.4f);
        snd.head = MakeSfx(0.08f, 1400, 0.1f, 20, 0.5f);
        snd.hurt = MakeSfx(0.2f, 90, 0.5f, 12, 0.7f);
        snd.kill = MakeSfx(0.2f, 300, 0.3f, 12, 0.5f);
        snd.click = MakeSfx(0.05f, 600, 0.5f, 30, 0.4f);
        match.snd = &snd;
        makeRT(true);
        applySettings();
        ensureMap(0);
    }

    void lowerQuality() {
        st.quality--;
        st.scalePct = QSCALE[st.quality];
        cooldown = 8.f;
        map.applyFilter(st.quality <= Q_MEDIUM);
        setMsg(std::string("PERFORMANCE MODE  ") + QNAMES[st.quality]);
    }

    Camera3D camFor(const Actor& p) const {
        const WeaponDef& wd = WDEFS[p.weapon];
        Vector3 eye = eyeOf(p);
        if (st.bob && p.grounded) eye.y += fabsf(sinf(p.bob)) * 0.03f;
        Camera3D cam{};
        cam.position = eye;
        cam.target = eye + fwdOf(p.yaw, p.pitch);
        cam.up = V(0, 1, 0);
        cam.fovy = Lerp((float)st.fov, st.fov * wd.adsFov / 70.f, p.ads);
        cam.projection = CAMERA_PERSPECTIVE;
        return cam;
    }

    void drawActors(const Camera3D& cam, Vector3 f, float dist) {
        Color mine = C(150, 195, 255), theirs = C(255, 150, 150);
        for (size_t i = 1; i < match.A.size(); i++) {
            const Actor& a = match.A[i];
            if (!a.alive && a.deadT > 1.2f) continue;
            Vector3 d = a.pos - cam.position;
            float L = Vector3Length(d);
            if (L > dist) continue;
            if (L > 3.f && Vector3DotProduct(d, f) < -3.f) continue;
            float fall = a.alive ? 0.f : Clamp(a.deadT / 0.5f, 0.f, 1.f) * (PI / 2);
            vader.transform = MatrixMultiply(MatrixRotateX(fall), MatrixRotateY(atan2f(-sinf(a.yaw), cosf(a.yaw))));
            Color tint = (a.team == match.A[0].team) ? mine : theirs;
            DrawModel(vader, a.pos, 1.f, tint);
        }
        for (auto& t : match.trc) DrawLine3D(t.a, t.b, C(255, 230, 150));
        for (auto& q : match.parts) DrawCube(q.p, q.size, q.size, q.size, q.c);
    }

    bool drawViewmodel() {
        const Actor& p = match.A[0];
        const WeaponDef& wd = WDEFS[p.weapon];
        if (p.weapon == 2 && p.ads > 0.85f) return false;
        BeginTextureMode(vmRT);
        ClearBackground(BLANK);
        Camera3D vc{};
        vc.position = V(0, 0, 0);
        vc.target = V(0, 0, -1);
        vc.up = V(0, 1, 0);
        vc.fovy = 60;
        vc.projection = CAMERA_PERSPECTIVE;
        BeginMode3D(vc);
        Vector3 pos = Vector3Lerp(wd.hip, wd.ads, p.ads);
        float inv = 1.f - p.ads, ra = 0.f;
        float bobAmt = st.bob ? 1.f : 0.f;
        if (p.reloadT > 0.f) ra = sinf((1.f - p.reloadT / wd.reload) * PI);
        pos.x += sinf(p.bob) * 0.006f * inv * bobAmt;
        pos.y += fabsf(cosf(p.bob)) * 0.006f * inv * bobAmt - 0.12f * ra;
        pos.z += p.kick * 0.06f;
        DrawModelEx(wm[p.weapon], pos, V(1, 0, 0), p.kick * 3.f + ra * 25.f, V(1, 1, 1), WHITE);
        if (p.flash > 0.f) {
            Vector3 m = V(pos.x, pos.y + 0.01f, pos.z - wd.len / 2 - 0.04f);
            DrawSphere(m, 0.035f * frange(0.8f, 1.4f), C(255, 200, 90));
            DrawSphere(m, 0.02f, WHITE);
        }
        EndMode3D();
        EndTextureMode();
        return true;
    }

    void show3D(const Camera3D& cam, float dist, bool gameplay) {
        int W = GetScreenWidth(), H = GetScreenHeight();
        Vector3 f = Vector3Normalize(cam.target - cam.position);
        BeginTextureMode(sceneRT);
        DrawRectangleGradientV(0, 0, rtW, rtH, C(105, 155, 215), C(225, 235, 242));
        BeginMode3D(cam);
        if (map.ready) map.draw(cam.position, f, dist);
        if (gameplay) drawActors(cam, f, dist);
        EndMode3D();
        EndTextureMode();
        DrawTexturePro(sceneRT.texture, RR(0, 0, (float)rtW, -(float)rtH), RR(0, 0, (float)W, (float)H), V2(0, 0), 0.f, WHITE);
        if (gameplay && match.A[0].alive && drawViewmodel())
            DrawTexturePro(vmRT.texture, RR(0, 0, (float)rtW, -(float)rtH), RR(0, 0, (float)W, (float)H), V2(0, 0), 0.f, WHITE);
    }

    void dimAll(int a) {
        int W = GetScreenWidth(), H = GetScreenHeight();
        DrawRectangle(0, 0, W, H, C(0, 0, 0, a));
    }

    void drawHUD(const Camera3D& cam) {
        int W = GetScreenWidth(), H = GetScreenHeight();
        const Actor& p = match.A[0];
        const WeaponDef& wd = WDEFS[p.weapon];
        int cx = W / 2, cy = H / 2;
        if (p.weapon == 2 && p.ads > 0.85f && p.alive) {
            DrawRing(V2((float)cx, (float)cy), H * 0.45f, H * 4.f, 0, 360, 64, BLACK);
            DrawLine(cx - (int)(H * 0.45f), cy, cx + (int)(H * 0.45f), cy, BLACK);
            DrawLine(cx, cy - (int)(H * 0.45f), cx, cy + (int)(H * 0.45f), BLACK);
            DrawCircle(cx, cy, 2, RED);
        } else if (p.alive) {
            int gap = S(6) + (int)(Lerp(wd.spread, wd.adsSpread, p.ads) * 400.f * gUi);
            Color c = C(255, 255, 255, 220);
            DrawRectangle(cx - gap - S(9), cy - 1, S(9), 2, c);
            DrawRectangle(cx + gap, cy - 1, S(9), 2, c);
            DrawRectangle(cx - 1, cy - gap - S(9), 2, S(9), c);
            DrawRectangle(cx - 1, cy + gap, 2, S(9), c);
        }
        if (p.hitmark > 0.f) {
            Color c = p.headmark > 0.f ? C(255, 60, 60) : WHITE;
            int d = S(10);
            DrawLine(cx - d, cy - d, cx - d / 2, cy - d / 2, c);
            DrawLine(cx + d, cy - d, cx + d / 2, cy - d / 2, c);
            DrawLine(cx - d, cy + d, cx - d / 2, cy + d / 2, c);
            DrawLine(cx + d, cy + d, cx + d / 2, cy + d / 2, c);
        }
        Vector3 fwd = Vector3Normalize(cam.target - cam.position);
        for (size_t i = 1; i < match.A.size(); i++) {
            const Actor& a = match.A[i];
            if (!a.alive) continue;
            Vector3 top = V(a.pos.x, a.pos.y + 2.25f, a.pos.z);
            Vector3 to = top - cam.position;
            float dl = Vector3Length(to), t = 0.f;
            if (dl > 60.f || Vector3DotProduct(to, fwd) < 0.f) continue;
            if (match.mp->raycast(cam.position, to * (1.f / dl), dl, t)) continue;
            Vector2 s = GetWorldToScreen(top, cam);
            int bw = S(60);
            bool ally = a.team == p.team;
            DrawRectangle((int)s.x - bw / 2, (int)s.y, bw, S(6), C(0, 0, 0, 160));
            DrawRectangle((int)s.x - bw / 2, (int)s.y, (int)(bw * a.hp / 100.f), S(6), ally ? C(90, 160, 255) : C(230, 60, 60));
            const char* nm = TextFormat("BOT %d", (int)i);
            int tw = MeasureText(nm, S(16));
            DrawText(nm, (int)s.x - tw / 2, (int)s.y - S(18), S(16), ally ? C(170, 210, 255) : C(255, 170, 170));
        }
        if (p.hurt > 0.f) DrawRectangle(0, 0, W, H, C(200, 0, 0, (int)(Clamp(p.hurt / 0.45f, 0.f, 1.f) * 110)));

        DrawRectangle(S(24), H - S(54), S(260), S(26), C(0, 0, 0, 140));
        bar(RR((float)S(26), (float)(H - S(52)), (float)S(256), (float)S(22)), p.hp / 100.f,
            p.hp > 30 ? C(60, 200, 80) : C(220, 50, 50), C(0, 0, 0, 0));
        DrawText(TextFormat("%d", (int)ceilf(p.hp)), S(34), H - S(50), S(20), WHITE);
        DrawText(TextFormat("%d", p.mag[p.weapon]), W - S(190), H - S(80), S(52), WHITE);
        DrawText(TextFormat("/ %d", p.res[p.weapon]), W - S(105), H - S(62), S(30), C(200, 200, 200));
        DrawText(wd.name, W - S(190), H - S(100), S(20), C(240, 200, 80));
        if (p.reloadT > 0.f) DrawText("RELOADING", cx - S(60), cy + S(70), S(22), C(255, 220, 90));
        for (int i = 0; i < 4; i++)
            DrawText(TextFormat("%d %s", i + 1, WDEFS[i].name), cx - S(190) + i * S(100), H - S(30), S(18),
                     i == p.weapon ? C(255, 220, 90) : C(180, 180, 180));

        if (!match.solo()) {
            DrawRectangle(cx - S(270), S(10), S(540), S(46), C(0, 0, 0, 150));
            DrawText(TextFormat("BLUE  %d", match.teamScore[0]), cx - S(255), S(20), S(28), C(120, 180, 255));
            DrawText(TextFormat("RED  %d", match.teamScore[1]), cx + S(255) - MeasureText(TextFormat("RED  %d", match.teamScore[1]), S(28)), S(20), S(28), C(255, 120, 120));
            const char* tg = TextFormat("FIRST TO %d", match.target);
            DrawText(tg, cx - MeasureText(tg, S(20)) / 2, S(24), S(20), WHITE);
        } else {
            int secs = (int)ceilf(match.timeLeft);
            DrawRectangle(cx - S(200), S(10), S(400), S(46), C(0, 0, 0, 150));
            const char* tm = TextFormat("TIME  %02d:%02d", secs / 60, secs % 60);
            DrawText(tm, cx - MeasureText(tm, S(28)) / 2, S(20), S(28), WHITE);
            DrawText(TextFormat("KILLS  %d", p.kills), cx - S(190), S(20), S(24), C(255, 220, 90));
        }

        for (size_t i = 0; i < match.feed.size() && i < 6; i++) {
            const KillMsg& m = match.feed[i];
            float al = Clamp(m.t / 1.5f, 0.f, 1.f);
            const char* t = m.text.c_str();
            int tw = MeasureText(t, S(20));
            DrawText(t, W - tw - S(16), S(66) + (int)i * S(26), S(20), m.head ? C(255, 200, 120, (int)(255 * al)) : C(255, 255, 255, (int)(255 * al)));
        }

        if (st.showFps) DrawText(TextFormat("%d FPS  %s", GetFPS(), QNAMES[st.quality]), S(20), S(64), S(18), C(200, 255, 200));
        if (msgT > 0.f) centerText(msg.c_str(), H / 4, S(26), C(255, 220, 90), W);

        if (!p.alive) {
            dimAll(120);
            centerText("ELIMINATED", H / 3, S(60), WHITE, W);
            int secs = std::max(1, (int)ceilf(p.respawnT));
            centerText(TextFormat("RESPAWNING IN %d", secs), H / 3 + S(80), S(30), C(255, 220, 90), W);
        }
    }

    void drawScoreboard() {
        int W = GetScreenWidth(), H = GetScreenHeight();
        int bw = S(760), bh = S(640);
        int bx = W / 2 - bw / 2, by = H / 2 - bh / 2;
        DrawRectangle(bx, by, bw, bh, C(0, 0, 0, 215));
        DrawRectangleLines(bx, by, bw, bh, C(100, 105, 125));
        centerText("SCOREBOARD", by + S(14), S(30), WHITE, W);
        int x0 = bx + S(40), xt = bx + S(300), xk = bx + S(480), xd = bx + S(600);
        int y0 = by + S(70);
        DrawText("PLAYER", x0, y0, S(18), C(200, 200, 200));
        DrawText("TEAM", xt, y0, S(18), C(200, 200, 200));
        DrawText("KILLS", xk, y0, S(18), C(200, 200, 200));
        DrawText("DEATHS", xd, y0, S(18), C(200, 200, 200));
        std::vector<int> order = match.standings();
        int row = 0;
        for (int idx : order) {
            const Actor& a = match.A[idx];
            int y = y0 + S(34) + row * S(22);
            if (y > by + bh - S(30)) break;
            Color tc = match.solo() ? C(255, 255, 255) : (a.team == 0 ? C(120, 180, 255) : C(255, 120, 120));
            Color rc = idx == 0 ? C(255, 220, 90) : WHITE;
            DrawText(match.nameOf(idx).c_str(), x0, y, S(18), rc);
            DrawText(match.solo() ? "SOLO" : (a.team == 0 ? "BLUE" : "RED"), xt, y, S(18), tc);
            DrawText(TextFormat("%d", a.kills), xk, y, S(18), WHITE);
            DrawText(TextFormat("%d", a.deaths), xd, y, S(18), WHITE);
            row++;
        }
    }

    void drawHome(float dt) {
        int W = GetScreenWidth(), H = GetScreenHeight();
        bgT += dt;
        bgCycle -= dt;
        if (bgCycle <= 0.f) { bgCycle = 14.f; ensureMap(map.id < 0 ? 0 : (map.id + 1) % 4); }
        if (map.ready) show3D(mapCam(map, bgT, 0.25f), 1000.f, false);
        else DrawTexturePro(title, RR(0, 0, (float)title.width, (float)title.height), RR(0, 0, (float)W, (float)H), V2(0, 0), 0.f, WHITE);
        DrawRectangleGradientV(0, 0, W, H, C(0, 0, 0, 40), C(0, 0, 0, 190));
        centerText("VADER VILLAGE", H / 6, S(84), WHITE, W);
        centerText("Team deathmatch and free for all across four biomes", H / 6 + S(96), S(26), C(225, 225, 225), W);
        if (map.ready && map.id >= 0) centerText(MAPS[map.id].name, H / 6 + S(140), S(22), C(255, 220, 90), W);
        navKeys(sHome, 3);
        const char* labels[3] = {"PLAY", "SETTINGS", "QUIT"};
        for (int i = 0; i < 3; i++) {
            Rectangle r = RR((float)(W / 2 - S(180)), (float)(H / 2 + S(10) + i * S(72)), (float)S(360), (float)S(56));
            if (button(i, sHome, r, labels[i], S(26))) {
                if (i == 0) { sMap = selMap; previewMap = -1; mapT = 0; scene = SC_MAPSEL; }
                else if (i == 1) { retScene = SC_HOME; sSet = 0; scene = SC_SETTINGS; }
                else quit = true;
            }
        }
        centerText("WASD move   SHIFT sprint   SPACE jump   LMB shoot   RMB scope   R reload   1-4 weapons   TAB scoreboard   ESC pause",
                   H - S(40), S(18), C(230, 230, 230), W);
    }

    void drawMapSel(float dt) {
        int W = GetScreenWidth(), H = GetScreenHeight();
        (void)W;
        mapT += dt;
        bgT += dt;
        navKeys(sMap, 5);
        if (sMap < 4 && sMap != previewMap) { previewMap = sMap; ensureMap(sMap); mapT = 0.f; }
        if (map.ready && map.id == previewMap) show3D(mapCam(map, bgT, smooth01(mapT / 7.f)), 1000.f, false);
        DrawRectangle(0, 0, S(560), H, C(0, 0, 0, 175));
        shadowText("CHOOSE A MAP", S(40), S(40), S(36), WHITE);
        for (int i = 0; i < 4; i++) {
            Rectangle r = RR((float)S(40), (float)(S(120) + i * S(104)), (float)S(480), (float)S(88));
            bool hov = CheckCollisionPointRec(GetMousePosition(), r);
            if (hov) sMap = i;
            bool on = sMap == i;
            DrawRectangleRec(r, on ? C(255, 190, 60, 235) : C(12, 14, 22, 205));
            DrawRectangleLines((int)r.x, (int)r.y, (int)r.width, (int)r.height, on ? C(255, 240, 180) : C(100, 105, 125));
            DrawText(MAPS[i].name, (int)r.x + S(18), (int)r.y + S(14), S(28), on ? C(20, 20, 20) : WHITE);
            DrawText(MAPS[i].sub, (int)r.x + S(18), (int)r.y + S(52), S(18), on ? C(40, 40, 40) : C(200, 200, 200));
            if ((hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) || (on && IsKeyPressed(KEY_ENTER))) {
                selMap = i;
                sMode = selMode;
                scene = SC_MODESEL;
                return;
            }
        }
        Rectangle back = RR((float)S(40), (float)(S(120) + 4 * S(104)), (float)S(480), (float)S(60));
        if (button(4, sMap, back, "BACK", S(24))) { scene = SC_HOME; sHome = 0; return; }
        if (IsKeyPressed(KEY_ESCAPE)) { scene = SC_HOME; sHome = 0; return; }
        if (map.ready && map.id == previewMap && mapT > 0.5f) {
            float a = Clamp((mapT - 0.5f) / 1.0f, 0.f, 1.f);
            int al = (int)(255 * a);
            const char* nm = MAPS[previewMap].name;
            DrawText(nm, W / 2 + S(120) - MeasureText(nm, S(48)) / 2, H - S(150), S(48), C(255, 220, 90, al));
            DrawText(MAPS[previewMap].sub, W / 2 + S(120) - MeasureText(MAPS[previewMap].sub, S(22)) / 2, H - S(96), S(22), C(240, 240, 240, al));
        }
    }

    void drawModeSel(float dt) {
        int W = GetScreenWidth(), H = GetScreenHeight();
        (void)W;
        mapT += dt;
        bgT += dt;
        if (map.ready && map.id == selMap) show3D(mapCam(map, bgT, smooth01(mapT / 7.f)), 1000.f, false);
        DrawRectangle(0, 0, S(620), H, C(0, 0, 0, 180));
        shadowText(MAPS[selMap].name, S(40), S(36), S(34), C(255, 220, 90));
        navKeys(sMode, 6);
        for (int i = 0; i < 3; i++) {
            Rectangle r = RR((float)S(40), (float)(S(110) + i * S(96)), (float)S(540), (float)S(82));
            bool hov = CheckCollisionPointRec(GetMousePosition(), r);
            if (hov) sMode = i;
            bool on = sMode == i;
            bool chosen = selMode == i;
            DrawRectangleRec(r, on ? C(255, 190, 60, 235) : C(12, 14, 22, 205));
            DrawRectangleLines((int)r.x, (int)r.y, (int)r.width, (int)r.height, chosen ? C(255, 240, 180) : C(100, 105, 125));
            DrawText(MODE_NAMES[i], (int)r.x + S(18), (int)r.y + S(12), S(24), on ? C(20, 20, 20) : WHITE);
            DrawText(MODE_DESC[i], (int)r.x + S(18), (int)r.y + S(46), S(16), on ? C(40, 40, 40) : C(200, 200, 200));
            if ((hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) || (on && IsKeyPressed(KEY_ENTER))) selMode = i;
        }
        bool solo = selMode == MODE_SOLO;
        Rectangle r3 = RR((float)S(40), (float)(S(110) + 3 * S(96)), (float)S(540), (float)S(60));
        const char* lbl = solo ? "TIME LIMIT" : "POINTS TO WIN";
        const char* val = solo ? TextFormat("< %d MIN >", TIME_OPTS[selTime]) : TextFormat("< %d >", TARGET_OPTS[selTgt]);
        int dir = optRow(3, sMode, r3, lbl, val, S(22));
        if (dir != 0) {
            if (solo) selTime = std::clamp(selTime + dir, 0, 3);
            else selTgt = std::clamp(selTgt + dir, 0, 7);
        }
        Rectangle rc = RR((float)S(40), (float)(S(110) + 4 * S(96)), (float)S(540), (float)S(60));
        if (button(4, sMode, rc, "CONTINUE", S(24))) { scene = SC_DIFFSEL; sDiff = selDiff; return; }
        Rectangle rb = RR((float)S(40), (float)(S(110) + 5 * S(96)), (float)S(540), (float)S(52));
        if (button(5, sMode, rb, "BACK", S(22))) { scene = SC_MAPSEL; return; }
        if (IsKeyPressed(KEY_ESCAPE)) { scene = SC_MAPSEL; return; }
    }

    void drawDiffSel(float dt) {
        int W = GetScreenWidth(), H = GetScreenHeight();
        (void)W;
        mapT += dt;
        bgT += dt;
        if (map.ready && map.id == selMap) show3D(mapCam(map, bgT, smooth01(mapT / 7.f)), 1000.f, false);
        DrawRectangle(0, 0, S(620), H, C(0, 0, 0, 180));
        shadowText("DIFFICULTY", S(40), S(36), S(34), C(255, 220, 90));
        DrawText(TextFormat("%s  /  %s", MAPS[selMap].name, solo_label()), S(40), S(84), S(18), C(220, 220, 220));
        navKeys(sDiff, 5);
        for (int i = 0; i < 4; i++) {
            Rectangle r = RR((float)S(40), (float)(S(130) + i * S(96)), (float)S(540), (float)S(82));
            bool hov = CheckCollisionPointRec(GetMousePosition(), r);
            if (hov) sDiff = i;
            bool on = sDiff == i;
            DrawRectangleRec(r, on ? C(255, 190, 60, 235) : C(12, 14, 22, 205));
            DrawRectangleLines((int)r.x, (int)r.y, (int)r.width, (int)r.height, C(100, 105, 125));
            DrawText(DIFFS[i].name, (int)r.x + S(18), (int)r.y + S(12), S(28), on ? C(20, 20, 20) : WHITE);
            DrawText(DIFFS[i].desc, (int)r.x + S(18), (int)r.y + S(50), S(18), on ? C(40, 40, 40) : C(200, 200, 200));
            if ((hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) || (on && IsKeyPressed(KEY_ENTER))) {
                selDiff = i;
                scene = SC_LOADING;
                loadStarted = false;
                loadT = 0.f;
                loadShown = 0.f;
                return;
            }
        }
        Rectangle rb = RR((float)S(40), (float)(S(130) + 4 * S(96)), (float)S(540), (float)S(52));
        if (button(4, sDiff, rb, "BACK", S(22))) { scene = SC_MODESEL; return; }
        if (IsKeyPressed(KEY_ESCAPE)) { scene = SC_MODESEL; return; }
    }

    const char* solo_label() const {
        return selMode == MODE_SOLO ? TextFormat("SINGLE  %d MIN", TIME_OPTS[selTime]) : TextFormat("TEAM DEATHMATCH  FIRST TO %d", TARGET_OPTS[selTgt]);
    }

    void startMatch() {
        match.snd = &snd;
        match.start(&map, selMode, selDiff, TARGET_OPTS[selTgt], TIME_OPTS[selTime]);
        scene = SC_PLAY;
        sinceStart = 0.f; perfT = 0.f; perfN = 0; cooldown = 0.f;
        DisableCursor();
        GetMouseDelta();
    }

    void drawLoading(float dt) {
        int W = GetScreenWidth(), H = GetScreenHeight();
        loadT += dt;
        if (!loadStarted) {
            loadStarted = true;
            if (!(map.id == selMap && map.ready)) map.beginLoad(selMap, root);
        }
        float real = 1.f;
        if (!map.ready) {
            if (map.failed) {
                setMsg("MAP FILES COULD NOT BE LOADED");
                scene = SC_MAPSEL; previewMap = -1;
                return;
            }
            real = map.step();
        }
        if (map.failed) { setMsg("MAP FAILED TO LOAD"); scene = SC_MAPSEL; previewMap = -1; return; }
        float goal = map.ready ? 1.f : real * 0.97f;
        loadShown += (goal - loadShown) * fminf(1.f, dt * 6.f);

        DrawRectangleGradientV(0, 0, W, H, C(10, 12, 20), C(40, 46, 62));
        centerText("LOADING", H / 5, S(56), WHITE, W);
        centerText(MAPS[selMap].name, H / 5 + S(80), S(34), C(255, 220, 90), W);
        centerText(MODE_NAMES[selMode], H / 5 + S(128), S(24), C(230, 230, 230), W);
        if (selMode == MODE_SOLO) centerText(TextFormat("TIME LIMIT  %d MINUTES", TIME_OPTS[selTime]), H / 5 + S(162), S(20), C(200, 200, 200), W);
        else centerText(TextFormat("FIRST TO %d POINTS", TARGET_OPTS[selTgt]), H / 5 + S(162), S(20), C(200, 200, 200), W);
        centerText(TextFormat("DIFFICULTY  %s", DIFFS[selDiff].name), H / 2 - S(10), S(30), C(255, 120, 120), W);
        bar(RR((float)(W / 2 - S(300)), (float)(H * 0.6f), (float)S(600), (float)S(22)), loadShown, C(255, 190, 60), C(40, 40, 50));
        centerText(TextFormat("%d%%", (int)(loadShown * 100.f)), (int)(H * 0.6f) + S(30), S(20), WHITE, W);
        centerText(TIPS[(int)(loadT / 2.5f) % TIP_COUNT], H - S(90), S(22), C(200, 200, 200), W);
        if (map.ready && loadT >= 1.6f && loadShown > 0.97f) startMatch();
    }

    void drawPlay(float dt) {
        bool live = scene == SC_PLAY;
        if (live && IsKeyPressed(KEY_ESCAPE)) {
            scene = SC_PAUSE; sPause = 0; EnableCursor(); live = false;
        }
        if (live) {
            match.update(dt, st);
            sinceStart += dt; perfT += dt; perfN++; cooldown -= dt;
            if (perfT >= 3.f) {
                float fps = perfN / perfT;
                if (st.autoQ && cooldown <= 0.f && sinceStart > 6.f && fps < 40.f && st.quality > Q_LOW) lowerQuality();
                perfT = 0.f; perfN = 0;
            }
            if (match.over) { scene = SC_END; sEnd = 0; EnableCursor(); }
        }
        const Actor& p = match.A[0];
        Camera3D cam = camFor(p);
        show3D(cam, QDRAW[st.quality], true);
        drawHUD(cam);
        if (IsKeyDown(KEY_TAB)) drawScoreboard();
    }

    void drawPause(float dt) {
        (void)dt;
        int W = GetScreenWidth(), H = GetScreenHeight();
        show3D(camFor(match.A[0]), QDRAW[st.quality], true);
        dimAll(160);
        centerText("PAUSED", H / 4, S(60), WHITE, W);
        if (IsKeyPressed(KEY_ESCAPE)) { scene = SC_PLAY; DisableCursor(); GetMouseDelta(); return; }
        navKeys(sPause, 3);
        const char* labels[3] = {"RESUME", "SETTINGS", "QUIT TO MENU"};
        for (int i = 0; i < 3; i++) {
            Rectangle r = RR((float)(W / 2 - S(180)), (float)(H / 2 + i * S(72)), (float)S(360), (float)S(56));
            if (button(i, sPause, r, labels[i], S(26))) {
                if (i == 0) { scene = SC_PLAY; DisableCursor(); GetMouseDelta(); }
                else if (i == 1) { retScene = SC_PAUSE; sSet = 0; scene = SC_SETTINGS; }
                else { scene = SC_HOME; sHome = 0; }
                return;
            }
        }
    }

    void drawSettings(float dt) {
        int W = GetScreenWidth();
        bgT += dt;
        if (retScene == SC_PAUSE) show3D(camFor(match.A[0]), QDRAW[st.quality], true);
        else if (map.ready) show3D(mapCam(map, bgT, 0.2f), 1000.f, false);
        dimAll(170);
        centerText("SETTINGS", S(40), S(44), WHITE, W);
        navKeys(sSet, 11);
        char vbuf[32];
        const char* names[10] = {"MOUSE SENSITIVITY", "FIELD OF VIEW", "GRAPHICS PRESET", "RENDER SCALE", "FPS LIMIT",
                                 "MASTER VOLUME", "VIEW BOBBING", "SHOW FPS", "AUTO PERFORMANCE", "RESET TO DEFAULTS"};
        for (int i = 0; i < 10; i++) {
            const char* v = "";
            switch (i) {
                case 0: snprintf(vbuf, sizeof vbuf, "%.2f", st.sens); v = vbuf; break;
                case 1: snprintf(vbuf, sizeof vbuf, "%d", st.fov); v = vbuf; break;
                case 2: v = QNAMES[st.quality]; break;
                case 3: snprintf(vbuf, sizeof vbuf, "%d%%", st.scalePct); v = vbuf; break;
                case 4: if (st.fpsIdx == 0) v = "UNLIMITED"; else { snprintf(vbuf, sizeof vbuf, "%d", FPS_OPTS[st.fpsIdx]); v = vbuf; } break;
                case 5: snprintf(vbuf, sizeof vbuf, "%d%%", st.volume); v = vbuf; break;
                case 6: v = st.bob ? "ON" : "OFF"; break;
                case 7: v = st.showFps ? "ON" : "OFF"; break;
                case 8: v = st.autoQ ? "ON" : "OFF"; break;
                default: v = "PRESS ENTER"; break;
            }
            Rectangle r = RR((float)(W / 2 - S(330)), (float)(S(110) + i * S(50)), (float)S(660), (float)S(42));
            int dir = optRow(i, sSet, r, names[i], v, S(20));
            if (dir == 0) continue;
            switch (i) {
                case 0: st.sens += 0.05f * dir; break;
                case 1: st.fov += dir; break;
                case 2: st.quality = (st.quality + dir + Q_COUNT) % Q_COUNT; st.scalePct = QSCALE[st.quality]; break;
                case 3: st.scalePct += 5 * dir; break;
                case 4: st.fpsIdx = (st.fpsIdx + dir + 5) % 5; break;
                case 5: st.volume += 5 * dir; break;
                case 6: st.bob = !st.bob; break;
                case 7: st.showFps = !st.showFps; break;
                case 8: st.autoQ = !st.autoQ; break;
                default: {
                    Settings d;
                    d.quality = detectQuality();
                    d.scalePct = QSCALE[d.quality];
                    st = d;
                    setMsg("DEFAULTS RESTORED");
                    break;
                }
            }
            clampSettings(st);
            applySettings();
            makeRT(false);
        }
        Rectangle rb = RR((float)(W / 2 - S(330)), (float)(S(110) + 10 * S(50) + S(12)), (float)S(660), (float)S(50));
        bool leave = IsKeyPressed(KEY_ESCAPE);
        if (button(10, sSet, rb, "BACK", S(24)) || leave) {
            saveSettings(st, cfgPath.c_str());
            scene = retScene;
            if (scene == SC_PLAY) { DisableCursor(); GetMouseDelta(); }
            if (scene == SC_HOME) sHome = 1;
        }
    }

    void drawEnd(float dt) {
        int W = GetScreenWidth(), H = GetScreenHeight();
        bgT += dt;
        if (map.ready) show3D(mapCam(map, bgT, 0.6f), 1000.f, false);
        dimAll(190);
        std::string title;
        Color tc = WHITE;
        if (match.solo()) {
            title = match.winActor == 0 ? std::string("YOU WIN") : match.nameOf(match.winActor) + " WINS";
            tc = C(255, 220, 90);
        } else {
            bool mine = match.winTeam == 0;
            title = mine ? "BLUE TEAM WINS" : "RED TEAM WINS";
            tc = mine ? C(120, 180, 255) : C(255, 120, 120);
        }
        centerText(title.c_str(), S(40), S(56), tc, W);
        if (match.solo()) {
            int wk = match.winActor >= 0 ? match.A[match.winActor].kills : 0;
            centerText(TextFormat("%d KILLS  ON THE CLOCK", wk), S(112), S(24), WHITE, W);
        } else {
            centerText(TextFormat("BLUE %d   -   %d RED", match.teamScore[0], match.teamScore[1]), S(112), S(26), WHITE, W);
        }
        std::vector<int> order = match.standings();
        int x0 = W / 2 - S(360);
        int y0 = S(170);
        DrawText("PLAYER", x0, y0, S(20), C(200, 200, 200));
        DrawText("TEAM", x0 + S(260), y0, S(20), C(200, 200, 200));
        DrawText("KILLS", x0 + S(430), y0, S(20), C(200, 200, 200));
        DrawText("DEATHS", x0 + S(560), y0, S(20), C(200, 200, 200));
        for (size_t r = 0; r < order.size() && r < 10; r++) {
            const Actor& a = match.A[order[r]];
            int y = y0 + S(36) + (int)r * S(26);
            Color tcol = match.solo() ? WHITE : (a.team == 0 ? C(120, 180, 255) : C(255, 120, 120));
            DrawText(match.nameOf(order[r]).c_str(), x0, y, S(20), order[r] == 0 ? C(255, 220, 90) : WHITE);
            DrawText(match.solo() ? "SOLO" : (a.team == 0 ? "BLUE" : "RED"), x0 + S(260), y, S(20), tcol);
            DrawText(TextFormat("%d", a.kills), x0 + S(430), y, S(20), WHITE);
            DrawText(TextFormat("%d", a.deaths), x0 + S(560), y, S(20), WHITE);
        }
        navKeys(sEnd, 2);
        Rectangle r1 = RR((float)(W / 2 - S(360)), (float)(H - S(170)), (float)S(340), (float)S(56));
        Rectangle r2 = RR((float)(W / 2 + S(20)), (float)(H - S(170)), (float)S(340), (float)S(56));
        if (button(0, sEnd, r1, "PLAY AGAIN", S(24))) {
            if (ensureMap(selMap)) startMatch();
            return;
        }
        if (button(1, sEnd, r2, "MAIN MENU", S(24)) || IsKeyPressed(KEY_ESCAPE)) { scene = SC_HOME; sHome = 0; }
    }

    void frame() {
        float dt = fminf(GetFrameTime(), 0.05f);
        gUi = GetScreenHeight() / 720.f;
        makeRT(false);
        if (msgT > 0.f) msgT -= dt;
        BeginDrawing();
        ClearBackground(BLACK);
        switch (scene) {
            case SC_HOME: drawHome(dt); break;
            case SC_MAPSEL: drawMapSel(dt); break;
            case SC_MODESEL: drawModeSel(dt); break;
            case SC_DIFFSEL: drawDiffSel(dt); break;
            case SC_LOADING: drawLoading(dt); break;
            case SC_PLAY: drawPlay(dt); break;
            case SC_PAUSE: drawPause(dt); break;
            case SC_SETTINGS: drawSettings(dt); break;
            case SC_END: drawEnd(dt); break;
        }
        EndDrawing();
    }
};

int main() {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Vader Village");
    SetExitKey(KEY_NULL);
    InitAudioDevice();
    SetRandomSeed((unsigned)time(nullptr));
    Game* g = new Game();
    g->init();
    while (!WindowShouldClose() && !g->quit) g->frame();
    saveSettings(g->st, g->cfgPath.c_str());
    delete g;
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
