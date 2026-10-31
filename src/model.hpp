#pragma once
#include "common.hpp"

// Loads the baked .mdl files produced by the asset pipeline (see tools/).
// Format: "MDL1", u32 nsub, then per sub: char tex[64], u8 rgba[4], u32 nvert,
// float pos[3n], float nrm[3n], float uv[2n], u8 col[4n]  (non-indexed triangles)
inline Model LoadMDL(const std::string& dir, const std::string& name) {
    Model m = {};
    std::string path = dir + "/" + name + ".mdl";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { TraceLog(LOG_ERROR, "missing model %s", path.c_str()); return m; }
    char magic[4]; uint32_t n = 0;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "MDL1", 4) != 0 || fread(&n, 4, 1, f) != 1) { fclose(f); return m; }
    m.transform = MatrixIdentity();
    m.meshCount = (int)n; m.materialCount = (int)n;
    m.meshes = (Mesh*)MemAlloc(n * sizeof(Mesh));
    m.materials = (Material*)MemAlloc(n * sizeof(Material));
    m.meshMaterial = (int*)MemAlloc(n * sizeof(int));
    for (uint32_t i = 0; i < n; i++) {
        char tex[64]; unsigned char rgba[4]; uint32_t nv;
        if (fread(tex, 1, 64, f) != 64 || fread(rgba, 1, 4, f) != 4 || fread(&nv, 4, 1, f) != 1) break;
        Mesh me = {};
        me.vertexCount = (int)nv; me.triangleCount = (int)(nv / 3);
        me.vertices = (float*)MemAlloc(nv * 12);
        me.normals = (float*)MemAlloc(nv * 12);
        me.texcoords = (float*)MemAlloc(nv * 8);
        me.colors = (unsigned char*)MemAlloc(nv * 4);
        size_t r = fread(me.vertices, 12, nv, f) + fread(me.normals, 12, nv, f) + fread(me.texcoords, 8, nv, f) + fread(me.colors, 4, nv, f);
        (void)r;
        UploadMesh(&me, false);
        m.meshes[i] = me;
        Material mat = LoadMaterialDefault();
        if (tex[0]) {
            Texture2D t = LoadTexture((dir + "/" + tex).c_str());
            SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
            SetMaterialTexture(&mat, MATERIAL_MAP_DIFFUSE, t);
        }
        mat.maps[MATERIAL_MAP_DIFFUSE].color = Color{rgba[0], rgba[1], rgba[2], rgba[3]};
        m.materials[i] = mat;
        m.meshMaterial[i] = (int)i;
    }
    fclose(f);
    return m;
}
