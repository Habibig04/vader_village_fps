#pragma once
#include "common.hpp"
#include <queue>

struct MapDef { const char* id; const char* name; const char* sub; };
static const MapDef MAPS[4] = {
    {"village1", "OAKVALE VILLAGE", "Oak towns, cobble walls and stone paths"},
    {"desert", "SANDSTONE DESERT", "Dunes, cacti and terracotta mesas"},
    {"snow", "FROSTVALE", "Spruce forests and frozen outposts"},
    {"beach", "CORAL BEACH", "Open sand, palms and shallow shores"},
};

inline bool rd(FILE* f, void* p, size_t n) { return fread(p, 1, n, f) == n; }

struct MapChunk { int mat; Vector3 mn, mx, c; float r; Mesh mesh; };

struct Map {
    static constexpr int K = 3;
    int id = -1;
    bool ready = false, failed = false;
    float cell = 0.5f;
    Vector3 org{0, 0, 0};
    int nx = 0, ny = 0, nz = 0;
    std::vector<unsigned char> bits;
    std::vector<unsigned char> cbits;
    std::vector<unsigned char> wbits;
    std::vector<MapChunk> chunks;
    std::vector<Material> mats;
    std::vector<Texture2D> texs;
    std::vector<unsigned char> texAlpha;
    Vector3 mid{0, 0, 0};
    float radius = 100.f;
    FILE* f = nullptr;
    int stage = 0;
    uint32_t left = 0, total = 0;
    int gw = 0, gh = 0;
    std::vector<unsigned char> gcnt, gmain;
    std::vector<float> glvl;
    std::vector<int> mainNodes;
    std::vector<int> spawnNodes;
    mutable std::vector<float> gG;
    mutable std::vector<int> gPar;
    mutable std::vector<unsigned char> gDone;

    void fail() {
        if (f) { fclose(f); f = nullptr; }
        failed = true;
    }

    void unload() {
        for (auto& c : chunks) UnloadMesh(c.mesh);
        chunks.clear();
        for (auto& m : mats) MemFree(m.maps);
        mats.clear();
        for (auto& t : texs) UnloadTexture(t);
        texs.clear();
        texAlpha.clear();
        if (f) { fclose(f); f = nullptr; }
        std::vector<unsigned char>().swap(bits);
        std::vector<unsigned char>().swap(cbits);
        std::vector<unsigned char>().swap(wbits);
        std::vector<unsigned char>().swap(gcnt);
        std::vector<unsigned char>().swap(gmain);
        std::vector<float>().swap(glvl);
        std::vector<float>().swap(gG);
        std::vector<int>().swap(gPar);
        std::vector<unsigned char>().swap(gDone);
        std::vector<int>().swap(mainNodes);
        std::vector<int>().swap(spawnNodes);
        ready = false; failed = false; id = -1; stage = 0; left = 0; total = 0; gw = 0; gh = 0;
    }

    bool beginLoad(int idx, const std::string& root) {
        unload();
        id = idx;
        std::string dir = root + "/maps/" + MAPS[idx].id;
        f = fopen((dir + "/map.vmap").c_str(), "rb");
        if (!f) { failed = true; return false; }
        char mg[4] = {0, 0, 0, 0};
        uint32_t ver = 0, nt = 0, nc = 0;
        int dims[3] = {0, 0, 0};
        if (!rd(f, mg, 4) || memcmp(mg, "VMAP", 4) != 0 || !rd(f, &ver, 4) || ver != 3 ||
            !rd(f, &cell, 4) || !rd(f, &org, 12) || !rd(f, dims, 12) || !rd(f, &nt, 4)) { fail(); return false; }
        nx = dims[0]; ny = dims[1]; nz = dims[2];
        for (uint32_t i = 0; i < nt; i++) {
            uint32_t l = 0;
            unsigned char a = 0;
            if (!rd(f, &l, 4) || l == 0 || l > 256) { fail(); return false; }
            std::string name(l, '\0');
            if (!rd(f, &name[0], l) || !rd(f, &a, 1)) { fail(); return false; }
            texs.push_back(LoadTexture((dir + "/" + name).c_str()));
            texAlpha.push_back(a);
            Material m = LoadMaterialDefault();
            SetMaterialTexture(&m, MATERIAL_MAP_DIFFUSE, texs.back());
            mats.push_back(m);
        }
        if (!rd(f, &nc, 4)) { fail(); return false; }
        total = nc; left = nc; stage = 1;
        return true;
    }

    float step() {
        if (ready) return 1.f;
        if (!f) { failed = true; return 0.f; }
        if (stage == 1) {
            for (int k = 0; k < 24 && left > 0; k++) {
                if (!readChunk()) { fail(); return 0.f; }
                left--;
            }
            if (left == 0) stage = 2;
            return 0.08f + 0.78f * (1.f - (float)left / (float)(total ? total : 1));
        }
        if (stage == 2) {
            if (!readGrid()) { fail(); return 0.f; }
            stage = 3;
            return 0.9f;
        }
        if (stage == 3) { buildNav(); stage = 4; return 0.97f; }
        fclose(f); f = nullptr;
        ready = true;
        return 1.f;
    }

    void applyFilter(bool linear) {
        for (auto& t : texs) SetTextureFilter(t, linear ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
    }

    bool readChunk() {
        uint32_t t = 0, nv = 0, ni = 0;
        float bb[6] = {0, 0, 0, 0, 0, 0};
        if (!rd(f, &t, 4) || !rd(f, bb, 24) || !rd(f, &nv, 4) || !rd(f, &ni, 4)) return false;
        if (t >= mats.size() || nv == 0 || ni == 0 || nv > 65535 || ni % 3 != 0) return false;
        std::vector<unsigned char> vb((size_t)nv * 24);
        std::vector<uint32_t> ib(ni);
        if (!rd(f, vb.data(), vb.size()) || !rd(f, ib.data(), ni * 4)) return false;
        for (uint32_t i = 0; i < ni; i++) if (ib[i] >= nv) return false;
        Mesh m = {};
        m.vertexCount = (int)nv;
        m.triangleCount = (int)(ni / 3);
        m.vertices = (float*)MemAlloc(nv * 3 * sizeof(float));
        m.texcoords = (float*)MemAlloc(nv * 2 * sizeof(float));
        m.colors = (unsigned char*)MemAlloc(nv * 4);
        m.indices = (unsigned short*)MemAlloc(ni * sizeof(unsigned short));
        for (uint32_t i = 0; i < nv; i++) {
            const unsigned char* p = vb.data() + (size_t)i * 24;
            memcpy(m.vertices + i * 3, p, 12);
            memcpy(m.texcoords + i * 2, p + 12, 8);
            memcpy(m.colors + i * 4, p + 20, 4);
        }
        for (uint32_t i = 0; i < ni; i++) m.indices[i] = (unsigned short)ib[i];
        UploadMesh(&m, false);
        MapChunk c;
        c.mat = (int)t;
        c.mn = V(bb[0], bb[1], bb[2]);
        c.mx = V(bb[3], bb[4], bb[5]);
        c.c = V((bb[0] + bb[3]) * 0.5f, (bb[1] + bb[4]) * 0.5f, (bb[2] + bb[5]) * 0.5f);
        c.r = Vector3Distance(c.c, c.mx);
        c.mesh = m;
        chunks.push_back(c);
        return true;
    }

    bool readGrid() {
        uint32_t npk = 0;
        if (!rd(f, &npk, 4)) return false;
        bits.resize(npk);
        if (npk && !rd(f, bits.data(), npk)) return false;
        uint32_t ncb = 0;
        if (!rd(f, &ncb, 4)) return false;
        cbits.resize(ncb);
        if (ncb && !rd(f, cbits.data(), ncb)) return false;
        uint32_t nwb = 0;
        if (!rd(f, &nwb, 4)) return false;
        wbits.resize(nwb);
        if (nwb && !rd(f, wbits.data(), nwb)) return false;
        Vector3 mn = V(1e9f, 1e9f, 1e9f), mx = V(-1e9f, -1e9f, -1e9f);
        for (auto& c : chunks) {
            mn = V(fminf(mn.x, c.mn.x), fminf(mn.y, c.mn.y), fminf(mn.z, c.mn.z));
            mx = V(fmaxf(mx.x, c.mx.x), fmaxf(mx.y, c.mx.y), fmaxf(mx.z, c.mx.z));
        }
        if (chunks.empty()) { mn = V(0, 0, 0); mx = V(100, 10, 100); }
        mid = V((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f);
        radius = fmaxf(mx.x - mn.x, mx.z - mn.z) * 0.5f;
        return true;
    }

    int vx(float x) const { return (int)floorf((x - org.x) / cell); }
    int vy(float y) const { return (int)floorf((y - org.y) / cell); }
    int vz(float z) const { return (int)floorf((z - org.z) / cell); }

    bool solid(int x, int y, int z) const {
        if (y < 0) return true;
        if (y >= ny) return false;
        if (x < 0 || z < 0 || x >= nx || z >= nz) return true;
        size_t i = ((size_t)y * nz + z) * nx + x;
        return (bits[i >> 3] >> (i & 7)) & 1;
    }

    bool climbAt(int x, int y, int z) const {
        if (x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz) return false;
        size_t i = ((size_t)y * nz + z) * nx + x;
        return (cbits[i >> 3] >> (i & 7)) & 1;
    }

    bool wetAbove(float x, float top, float z) const {
        if (wbits.empty()) return false;
        int ix = vx(x), iz = vz(z);
        int y0 = vy(top + 0.05f), y1 = vy(top + 1.8f);
        for (int iy = y0; iy <= y1; iy++) {
            if (ix < 0 || iz < 0 || iy < 0 || ix >= nx || iy >= ny || iz >= nz) continue;
            size_t i = ((size_t)iy * nz + iz) * nx + ix;
            if ((wbits[i >> 3] >> (i & 7)) & 1) return true;
        }
        return false;
    }

    bool nearLadder(Vector3 p, float r) const {
        if (cbits.empty()) return false;
        int x0 = vx(p.x - r), x1 = vx(p.x + r), z0 = vz(p.z - r), z1 = vz(p.z + r);
        int y0 = vy(p.y + 0.2f), y1 = vy(p.y + 1.6f);
        for (int iy = y0; iy <= y1; iy++)
            for (int iz = z0; iz <= z1; iz++)
                for (int ix = x0; ix <= x1; ix++)
                    if (climbAt(ix, iy, iz)) return true;
        return false;
    }

    bool blocked(float x, float y, float z, float r, float h) const {
        int x0 = vx(x - r), x1 = vx(x + r), z0 = vz(z - r), z1 = vz(z + r);
        int y0 = vy(y + 0.05f), y1 = vy(y + h - 0.05f);
        for (int iy = y0; iy <= y1; iy++)
            for (int iz = z0; iz <= z1; iz++)
                for (int ix = x0; ix <= x1; ix++) {
                    if (!solid(ix, iy, iz)) continue;
                    float bx0 = org.x + ix * cell, bz0 = org.z + iz * cell;
                    float px = Clamp(x, bx0, bx0 + cell), pz = Clamp(z, bz0, bz0 + cell);
                    float dx = x - px, dz = z - pz;
                    if (dx * dx + dz * dz < r * r) return true;
                }
        return false;
    }

    float groundBelow(float x, float z, float fromY) const {
        int ix = vx(x), iz = vz(z);
        int iy = vy(fromY);
        if (iy >= ny) iy = ny - 1;
        for (; iy >= 0; iy--) if (solid(ix, iy, iz)) return org.y + (iy + 1) * cell;
        return org.y - 1000.f;
    }

    bool raycast(Vector3 o, Vector3 d, float maxT, float& tHit) const {
        tHit = maxT;
        int ix = vx(o.x), iy = vy(o.y), iz = vz(o.z);
        if (solid(ix, iy, iz)) { tHit = 0.f; return true; }
        const float INF = 1e30f;
        int sx = d.x >= 0 ? 1 : -1, sy = d.y >= 0 ? 1 : -1, sz = d.z >= 0 ? 1 : -1;
        float tmx = INF, tmy = INF, tmz = INF, tdx = INF, tdy = INF, tdz = INF;
        if (fabsf(d.x) > 1e-7f) { tmx = (org.x + (sx > 0 ? ix + 1 : ix) * cell - o.x) / d.x; tdx = cell / fabsf(d.x); }
        if (fabsf(d.y) > 1e-7f) { tmy = (org.y + (sy > 0 ? iy + 1 : iy) * cell - o.y) / d.y; tdy = cell / fabsf(d.y); }
        if (fabsf(d.z) > 1e-7f) { tmz = (org.z + (sz > 0 ? iz + 1 : iz) * cell - o.z) / d.z; tdz = cell / fabsf(d.z); }
        float t = 0.f;
        for (int guard = 0; guard < 4096; guard++) {
            if (tmx < tmy && tmx < tmz) { t = tmx; tmx += tdx; ix += sx; }
            else if (tmy < tmz) { t = tmy; tmy += tdy; iy += sy; }
            else { t = tmz; tmz += tdz; iz += sz; }
            if (t > maxT) break;
            if (solid(ix, iy, iz)) { tHit = t; return true; }
        }
        return false;
    }

    void tryAxis(Vector3& p, float d, int ax, float r, float h) const {
        if (fabsf(d) < 1e-7f) return;
        float qx = p.x + (ax == 0 ? d : 0.f), qz = p.z + (ax == 2 ? d : 0.f);
        qx = Clamp(qx, org.x + 0.6f, org.x + nx * cell - 0.6f);
        qz = Clamp(qz, org.z + 0.6f, org.z + nz * cell - 0.6f);
        if (!blocked(qx, p.y, qz, r, h)) { p.x = qx; p.z = qz; return; }
        float g = groundBelow(qx, qz, p.y + 0.55f);
        if (g > p.y + 0.02f && g <= p.y + 0.55f && !blocked(qx, g + 0.02f, qz, r, h)) { p.x = qx; p.z = qz; p.y = g; }
    }

    void moveBody(Vector3& p, Vector3& v, float dt, float r, float h, bool& grounded) const {
        tryAxis(p, v.x * dt, 0, r, h);
        tryAxis(p, v.z * dt, 2, r, h);
        v.y -= 24.f * dt;
        float ny2 = p.y + v.y * dt;
        if (v.y > 0.f) {
            if (blocked(p.x, ny2, p.z, r, h)) v.y = 0.f; else p.y = ny2;
            grounded = false;
        } else {
            float g = groundBelow(p.x, p.z, p.y + 0.3f);
            if (ny2 <= g) { p.y = g; v.y = 0.f; grounded = true; }
            else { p.y = ny2; grounded = false; }
        }
    }

    void buildNav() {
        gw = (int)ceilf(nx * cell);
        gh = (int)ceilf(nz * cell);
        size_t cells = (size_t)gw * gh;
        gcnt.assign(cells, 0);
        glvl.assign(cells * K, 0.f);
        for (int gz = 0; gz < gh; gz++)
            for (int gx = 0; gx < gw; gx++) {
                size_t c = (size_t)gz * gw + gx;
                float cx = org.x + gx + 0.5f, cz = org.z + gz + 0.5f;
                int ix = vx(cx), iz = vz(cz), n = 0;
                if (ix < 0 || iz < 0 || ix >= nx || iz >= nz) continue;
                for (int iy = 0; iy < ny - 1 && n < K; iy++) {
                    if (!solid(ix, iy, iz) || solid(ix, iy + 1, iz)) continue;
                    float top = org.y + (iy + 1) * cell;
                    if (!blocked(cx, top + 0.02f, cz, 0.3f, 1.75f) && !wetAbove(cx, top, cz)) glvl[c * K + n++] = top;
                }
                gcnt[c] = (unsigned char)n;
            }
        size_t N = cells * K;
        std::vector<int> comp(N, -1);
        std::vector<int> stk;
        int bestC = -1, ncomp = 0;
        size_t bestN = 0;
        for (size_t s = 0; s < N; s++) {
            if (s % K >= gcnt[s / K] || comp[s] >= 0) continue;
            stk.clear();
            stk.push_back((int)s);
            comp[s] = ncomp;
            size_t cnt = 0;
            while (!stk.empty()) {
                int u = stk.back();
                stk.pop_back();
                cnt++;
                forNeighbors(u, [&](int v) {
                    if (comp[v] < 0) { comp[v] = ncomp; stk.push_back(v); }
                });
            }
            if (cnt > bestN) { bestN = cnt; bestC = ncomp; }
            ncomp++;
        }
        gmain.assign(N, 0);
        mainNodes.clear();
        for (size_t s = 0; s < N; s++)
            if (bestC >= 0 && comp[s] == bestC) { gmain[s] = 1; mainNodes.push_back((int)s); }
        pickSpawnLevel();
    }

    void pickSpawnLevel() {
        spawnNodes.clear();
        std::vector<int> cnt(64, 0);
        for (int u : mainNodes) {
            int b = (int)floorf(glvl[u] * 2.f);
            if (b >= 0 && b < 64) cnt[b]++;
        }
        int bestB = -1, bestCnt = 0;
        for (int b = 2; b < 64; b++)
            if (cnt[b] >= 200 && cnt[b] > bestCnt) { bestCnt = cnt[b]; bestB = b; }
        for (int u : mainNodes) {
            if (bestB < 0) { spawnNodes = mainNodes; return; }
            int b = (int)floorf(glvl[u] * 2.f);
            if (b == bestB) spawnNodes.push_back(u);
        }
        if (spawnNodes.empty()) spawnNodes = mainNodes;
    }

    int spawnNode() const {
        const std::vector<int>& v = spawnNodes.empty() ? mainNodes : spawnNodes;
        if (v.empty()) return -1;
        return v[GetRandomValue(0, (int)v.size() - 1)];
    }

    template <class F> void forNeighbors(int u, F fn) const {
        int c = u / K, gx = c % gw, gz = c / gw;
        float lv = glvl[u];
        static const int DX[4] = {1, -1, 0, 0}, DZ[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; d++) {
            int x = gx + DX[d], z = gz + DZ[d];
            if (x < 0 || z < 0 || x >= gw || z >= gh) continue;
            int nc = z * gw + x;
            for (int k = 0; k < gcnt[nc]; k++) {
                int v = nc * K + k;
                if (fabsf(glvl[v] - lv) <= 1.1f) fn(v);
            }
        }
    }

    Vector3 nodePos(int u) const {
        int c = u / K, gx = c % gw, gz = c / gw;
        return V(org.x + gx + 0.5f, glvl[u], org.z + gz + 0.5f);
    }

    float hc(int u, int t) const {
        Vector3 a = nodePos(u), b = nodePos(t);
        return fabsf(a.x - b.x) + fabsf(a.z - b.z);
    }

    int randomNode() const {
        if (mainNodes.empty()) return -1;
        return mainNodes[GetRandomValue(0, (int)mainNodes.size() - 1)];
    }

    int nodeNear(Vector3 p) const {
        if (gmain.empty()) return -1;
        int gx = (int)floorf(p.x - org.x), gz = (int)floorf(p.z - org.z);
        for (int r = 0; r <= 3; r++) {
            int best = -1;
            float bd = 1e9f;
            for (int dz = -r; dz <= r; dz++)
                for (int dx = -r; dx <= r; dx++) {
                    if (r > 0 && abs(dx) != r && abs(dz) != r) continue;
                    int x = gx + dx, z = gz + dz;
                    if (x < 0 || z < 0 || x >= gw || z >= gh) continue;
                    size_t c = (size_t)z * gw + x;
                    for (int k = 0; k < gcnt[c]; k++) {
                        int u = (int)c * K + k;
                        if (!gmain[u]) continue;
                        float d = fabsf(glvl[u] - p.y);
                        if (d < bd) { bd = d; best = u; }
                    }
                }
            if (best >= 0) return best;
        }
        return -1;
    }

    bool astar(int from, int to, std::vector<int>& out) const {
        out.clear();
        if (from < 0 || to < 0) return false;
        size_t N = gcnt.size() * K;
        gG.assign(N, 1e9f);
        gPar.assign(N, -1);
        gDone.assign(N, 0);
        typedef std::pair<float, int> QN;
        std::priority_queue<QN, std::vector<QN>, std::greater<QN>> pq;
        gG[from] = 0.f;
        pq.push(QN(hc(from, to), from));
        int bestN = from;
        float bestH = hc(from, to);
        int expansions = 0;
        while (!pq.empty() && expansions < 6000) {
            int u = pq.top().second;
            pq.pop();
            if (gDone[u]) continue;
            gDone[u] = 1;
            expansions++;
            if (u == to) { bestN = u; break; }
            float hu = hc(u, to);
            if (hu < bestH) { bestH = hu; bestN = u; }
            forNeighbors(u, [&](int v) {
                if (gDone[v]) return;
                float ng = gG[u] + 1.f + fabsf(glvl[v] - glvl[u]) * 0.5f;
                if (ng < gG[v]) {
                    gG[v] = ng;
                    gPar[v] = u;
                    pq.push(QN(ng + hc(v, to), v));
                }
            });
        }
        for (int u = bestN; u >= 0; u = gPar[u]) out.push_back(u);
        std::reverse(out.begin(), out.end());
        return !out.empty();
    }

    void draw(Vector3 cp, Vector3 f, float dist) const {
        for (int pass = 0; pass < 2; pass++)
            for (const auto& c : chunks) {
                bool al = texAlpha[c.mat] != 0;
                if ((pass == 1) != al) continue;
                Vector3 d = c.c - cp;
                float L = Vector3Length(d);
                if (L - c.r > dist) continue;
                if (L > c.r && Vector3DotProduct(d, f) < -c.r - 2.f) continue;
                DrawMesh(c.mesh, mats[c.mat], MatrixIdentity());
            }
    }
};
