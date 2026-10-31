#pragma once
#include "common.hpp"
#include "audio.hpp"
#include "map.hpp"
#include "weapons.hpp"
#include "settings.hpp"

struct Diff { const char* name; const char* desc; float aimErr, reaction, rpm, dmg, spread; };
static const Diff DIFFS[4] = {
    {"EASY", "Slow reactions and loose aim", 0.16f, 1.00f, 150.f, 6.f, 0.060f},
    {"NORMAL", "Balanced opponents", 0.10f, 0.70f, 210.f, 8.f, 0.040f},
    {"HARD", "Fast reactions and tight aim", 0.05f, 0.45f, 280.f, 9.f, 0.025f},
    {"NIGHTMARE", "Relentless, precise and fast", 0.025f, 0.22f, 360.f, 10.f, 0.012f},
};

enum { MODE_TDM5, MODE_TDM10, MODE_SOLO, MODE_N };
static const char* MODE_NAMES[MODE_N] = {"TEAM DEATHMATCH  5 v 5", "TEAM DEATHMATCH  10 v 10", "SINGLE  FREE FOR ALL"};
static const char* MODE_DESC[MODE_N] = {
    "Five against five. First team to the point target wins.",
    "Ten against ten. First team to the point target wins.",
    "Everyone for themselves. Most kills when the clock runs out wins.",
};
static const int TARGET_OPTS[8] = {25, 50, 100, 150, 200, 300, 400, 500};
static const int TIME_OPTS[4] = {2, 5, 10, 15};
static const float RESPAWN_T = 3.f;

struct Sounds { Sfx shot[4], hit, head, kill, hurt, click; };

struct Actor {
    int id = 0, team = 0, kills = 0, deaths = 0, weapon = 0;
    bool bot = false, alive = true, grounded = true;
    Vector3 pos{0, 0, 0}, vel{0, 0, 0}, lastPos{0, 0, 0}, wanderDir{0, 0, 0};
    float yaw = 0, pitch = 0, hp = 100, deadT = 0, respawnT = 0;
    int mag[4] = {0, 0, 0, 0}, res[4] = {0, 0, 0, 0};
    float fireCd = 0, reloadT = 0, ads = 0, kick = 0, flash = 0, bob = 0, hurt = 0, hitmark = 0, headmark = 0;
    int target = -1;
    float targetT = 0, reactT = 0, strafeT = 0, strafeDir = 1, replanT = 0, stuckT = 0, wanderT = 0, biasT = 0, biasY = 0, biasP = 0;
    std::vector<int> path;
    size_t pathPos = 0;
    bool jumpReq = false;
};

inline Vector3 eyeOf(const Actor& a) { return V(a.pos.x, a.pos.y + 1.62f, a.pos.z); }
inline Vector3 fwdOf(float yaw, float pitch) { return V(cosf(pitch) * sinf(yaw), sinf(pitch), -cosf(pitch) * cosf(yaw)); }
inline Vector3 rightOf(float yaw) { return V(cosf(yaw), 0, sinf(yaw)); }

struct KillMsg { std::string text; float t; bool head; };
struct Tracer { Vector3 a, b; float life; };
struct Part { Vector3 p, v; float life, size; Color c; };

struct Match {
    Map* mp = nullptr;
    Sounds* snd = nullptr;
    int mode = MODE_TDM5, diff = 1, target = 100;
    float timeLeft = 0, clock = 0;
    std::vector<Actor> A;
    int teamScore[2] = {0, 0};
    bool over = false;
    int winTeam = -1, winActor = -1;
    std::vector<KillMsg> feed;
    std::vector<Tracer> trc;
    std::vector<Part> parts;

    bool solo() const { return mode == MODE_SOLO; }
    std::string playerName = "YOU";
    std::string nameOf(int i) const { return i == 0 ? playerName : "BOT " + std::to_string(i); }

    void start(Map* m, int md, int df, int tgt, int minutes) {
        mp = m; mode = md; diff = df; target = tgt;
        over = false; winTeam = -1; winActor = -1;
        teamScore[0] = teamScore[1] = 0;
        feed.clear(); trc.clear(); parts.clear();
        clock = 0;
        timeLeft = (float)minutes * 60.f;
        A.clear();
        int n = solo() ? 10 : (mode == MODE_TDM5 ? 5 : 10);
        int total = solo() ? 10 : n * 2;
        for (int i = 0; i < total; i++) {
            Actor a;
            a.id = i;
            a.bot = i != 0;
            a.team = solo() ? i : (i < n ? 0 : 1);
            for (int w = 0; w < 4; w++) { a.mag[w] = WDEFS[w].mag; a.res[w] = WDEFS[w].reserve; }
            a.replanT = frange(0.f, 2.f);
            a.targetT = frange(0.f, 0.3f);
            a.wanderDir = V(1, 0, 0);
            A.push_back(a);
        }
        for (int i = 0; i < (int)A.size(); i++) spawn(i);
    }

    void spawn(int i) {
        Actor& a = A[i];
        Vector3 best = a.pos;
        float bestScore = -1.f;
        for (int k = 0; k < 40; k++) {
            int u = mp->randomNode();
            if (u < 0) break;
            Vector3 p = mp->nodePos(u);
            float md = 1e9f;
            for (size_t j = 0; j < A.size(); j++) {
                if ((int)j == i || !A[j].alive || A[j].team == a.team) continue;
                float dx = A[j].pos.x - p.x, dz = A[j].pos.z - p.z;
                md = fminf(md, sqrtf(dx * dx + dz * dz));
            }
            if (md > bestScore) { bestScore = md; best = p; }
        }
        a.pos = best;
        a.vel = V(0, 0, 0);
        a.yaw = frange(-PI, PI);
        a.pitch = 0;
        a.hp = 100; a.alive = true; a.deadT = 0; a.respawnT = 0;
        a.grounded = true;
        a.fireCd = 0; a.reloadT = 0; a.ads = 0; a.kick = 0; a.flash = 0; a.hurt = 0; a.hitmark = 0; a.headmark = 0;
        a.target = -1; a.path.clear(); a.pathPos = 0; a.stuckT = 0; a.replanT = 0;
        a.lastPos = a.pos;
        for (int w = 0; w < 4; w++) { a.mag[w] = WDEFS[w].mag; a.res[w] = WDEFS[w].reserve; }
    }

    void burst(Vector3 pos, Vector3 n, Color c, int cnt) {
        for (int i = 0; i < cnt; i++) {
            Vector3 v = n * frange(1.f, 3.f) + V(gauss() * 2, gauss() * 2 + 1, gauss() * 2);
            parts.push_back(Part{pos, v, frange(0.3f, 0.8f), frange(0.03f, 0.07f), c});
        }
    }

    void applyDamage(int j, float dmg, bool head, int shooter) {
        Actor& v = A[j];
        if (!v.alive) return;
        v.hp -= dmg;
        if (j == 0) { v.hurt = 0.45f; Play(snd->hurt, 0.8f); }
        if (shooter == 0) {
            A[0].hitmark = 0.15f;
            if (head) A[0].headmark = 0.15f;
            Play(head ? snd->head : snd->hit);
        }
        if (v.hp <= 0.f) killActor(j, shooter, head);
    }

    void killActor(int j, int k, bool head) {
        Actor& v = A[j];
        Actor& kk = A[k];
        v.alive = false; v.hp = 0; v.deadT = 0; v.respawnT = RESPAWN_T; v.deaths++;
        v.fireCd = 0; v.reloadT = 0; v.ads = 0; v.target = -1;
        kk.kills++;
        if (!solo()) teamScore[kk.team]++;
        KillMsg m;
        m.text = nameOf(k) + "  >  " + nameOf(j) + (head ? "  [HEADSHOT]" : "");
        m.t = 6.f;
        m.head = head;
        feed.insert(feed.begin(), m);
        if (feed.size() > 6) feed.pop_back();
        burst(v.pos + V(0, 1, 0), V(0, 1, 0), C(180, 20, 20), 14);
        if (k == 0 || j == 0) Play(snd->kill, 0.7f);
        if (!solo() && !over && teamScore[kk.team] >= target) { over = true; winTeam = kk.team; }
    }

    bool los(Vector3 a, Vector3 b) const {
        Vector3 d = b - a;
        float L = Vector3Length(d);
        if (L < 0.01f) return true;
        float t = 0.f;
        bool hit = mp->raycast(a, d * (1.f / L), L, t);
        return !hit || t > L - 0.3f;
    }

    void shootRay(int ai, Vector3 o, Vector3 d, float range, float dmg, Vector3 muzzle, bool tracer) {
        float tw = range;
        mp->raycast(o, d, range, tw);
        int bi = -1;
        float bt = tw;
        bool head = false;
        for (size_t j = 0; j < A.size(); j++) {
            if ((int)j == ai || !A[j].alive || A[j].team == A[ai].team) continue;
            const Vector3& e = A[j].pos;
            Box hb{V(e.x - 0.22f, e.y + 1.62f, e.z - 0.22f), V(e.x + 0.22f, e.y + 1.98f, e.z + 0.22f)};
            Box bb{V(e.x - 0.38f, e.y, e.z - 0.38f), V(e.x + 0.38f, e.y + 1.62f, e.z + 0.38f)};
            float t = 0.f;
            if (rayBox(o, d, hb, t) && t < bt && t >= 0.f) { bt = t; bi = (int)j; head = true; }
            if (rayBox(o, d, bb, t) && t < bt && t >= 0.f) { bt = t; bi = (int)j; head = false; }
        }
        Vector3 hp = o + d * bt;
        if (bi >= 0) {
            float fall = 1.f - 0.45f * Clamp(bt / range, 0.f, 1.f);
            applyDamage(bi, dmg * fall * (head ? 2.5f : 1.f), head, ai);
            burst(hp, -d, C(170, 20, 20), 6);
        } else if (tw < range) {
            burst(hp, V(0, 1, 0), C(200, 200, 190), 4);
        }
        if (tracer) trc.push_back(Tracer{muzzle, hp, 0.06f});
    }

    void playerFire(Actor& p, const Settings& st) {
        (void)st;
        const WeaponDef& wd = WDEFS[p.weapon];
        p.mag[p.weapon]--;
        p.fireCd = 60.f / wd.rpm;
        float sp2 = sqrtf(p.vel.x * p.vel.x + p.vel.z * p.vel.z);
        float sp = Lerp(wd.spread, wd.adsSpread, p.ads) * (1.f + sp2 * 0.08f + (p.grounded ? 0.f : 1.5f));
        Vector3 o = eyeOf(p), f = fwdOf(p.yaw, p.pitch), r = rightOf(p.yaw), u = Vector3CrossProduct(r, f);
        Vector3 muzzle = o + f * 0.9f + r * 0.14f - u * 0.1f;
        for (int i = 0; i < wd.pellets; i++) {
            Vector3 d = Vector3Normalize(f + r * (gauss() * sp) + u * (gauss() * sp));
            shootRay(0, o, d, wd.range, wd.dmg, muzzle, wd.pellets == 1 || frand() < 0.4f);
        }
        p.pitch += wd.recoil * 0.0045f * (1.f - 0.4f * p.ads);
        p.yaw += gauss() * wd.recoil * 0.0015f;
        p.kick = 1.f;
        p.flash = 0.05f;
        Play(snd->shot[p.weapon]);
    }

    void startReload(Actor& p) {
        p.reloadT = WDEFS[p.weapon].reload;
        p.ads = 0;
        Play(snd->click);
    }

    void switchTo(Actor& p, int i) {
        if (i == p.weapon) return;
        p.weapon = i;
        p.reloadT = 0;
        p.fireCd = 0.35f;
        Play(snd->click);
    }

    void updatePlayer(float dt, const Settings& st) {
        Actor& p = A[0];
