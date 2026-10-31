import os, sys, struct, shutil
import numpy as np

CELL = 0.5
SAMPLE = 0.2
PAD = 3.0
CHUNK = 16.0
NONSOLID = {
    "Stationary_Water", "Stationary_Lava", "Grass", "Tall_Seagrass", "Seagrass", "Dandelion", "Poppy",
    "Azure_Bluet", "Oxeye_Daisy", "Cornflower", "Torch", "Wheat", "Carrot", "Potato", "Beetroot_Seeds",
    "Double_Tallgrass", "Dead_Bush", "Sugar_Cane", "Rail", "Cobweb", "Ladder", "Yellow_Carpet", "Kelp",
    "Potted_Dandelion",
}
CLIMB = {"Ladder"}

def parse_mtl(path):
    mats = {}; cur = None
    for line in open(path, encoding="utf8", errors="ignore"):
        p = line.split()
        if not p: continue
        if p[0] == "newmtl":
            cur = p[1]; mats[cur] = {"tex": None}
        elif p[0] == "map_Kd" and cur:
            mats[cur]["tex"] = p[-1]
    return mats

def parse_obj(path):
    V, VT, VN = [], [], []
    groups = []
    cur = None
    for line in open(path, encoding="utf8", errors="ignore"):
        if line.startswith("v "):
            p = line.split(); V.append((float(p[1]), float(p[2]), float(p[3])))
        elif line.startswith("vt "):
            p = line.split(); VT.append((float(p[1]), float(p[2])))
        elif line.startswith("vn "):
            p = line.split(); VN.append((float(p[1]), float(p[2]), float(p[3])))
        elif line.startswith("usemtl"):
            cur = [line.split()[1], []]; groups.append(cur)
        elif line.startswith("f "):
            cs = []
            for tok in line.split()[1:]:
                a = tok.split("/")
                cs.append((int(a[0]) - 1, int(a[1]) - 1 if len(a) > 1 and a[1] else -1, int(a[2]) - 1 if len(a) > 2 and a[2] else -1))
            for k in range(1, len(cs) - 1):
                cur[1].append((cs[0], cs[k], cs[k + 1]))
    return np.array(V, np.float64), np.array(VT, np.float64), np.array(VN, np.float64), groups

def bake(name, obj_path, mtl_path, out_dir, tex_dir):
    V, VT, VN, groups = parse_obj(obj_path)
    mats = parse_mtl(mtl_path)
    tris_pos, tris_nrm, tris_tex, tris_corner_keys, tris_solid = [], [], [], [], []
    tex_names = []
    tex_index = {}
    for gname, tris in groups:
        if not tris: continue
        tex = mats.get(gname, {}).get("tex") or ""
        if tex not in tex_index:
            tex_index[tex] = len(tex_names); tex_names.append(tex)
        ti = tex_index[tex]
        solid = gname not in NONSOLID
        arr = np.array(tris, np.int64)
        vi = arr[:, :, 0]; ti_ = arr[:, :, 1]; ni = arr[:, :, 2]
        P = V[vi]
        N = VN[ni[:, 0]]
        tris_pos.append(P)
        tris_nrm.append(N)
        tris_tex.append(np.full(len(arr), ti, np.int64))
        tris_corner_keys.append(np.stack([vi, ti_, ni], -1))
        tris_solid.append(np.full(len(arr), solid, bool))
    P = np.concatenate(tris_pos); Nrm = np.concatenate(tris_nrm)
    T = np.concatenate(tris_tex); K = np.concatenate(tris_corner_keys); S = np.concatenate(tris_solid)

    mn = P.reshape(-1, 3).min(0) - PAD; mx = P.reshape(-1, 3).max(0) + PAD
    mn[1] = 0.0
    origin = np.floor(mn / 1.0) * 1.0
    dims = np.ceil((mx - origin) / CELL).astype(np.int64)
    nx, ny, nz = int(dims[0]), int(dims[1]), int(dims[2])
    solid_flat = np.zeros(nx * ny * nz, bool)

    sp = P[S]; sn = Nrm[S]
    edges = np.maximum.reduce([np.linalg.norm(sp[:, 1] - sp[:, 0], axis=1), np.linalg.norm(sp[:, 2] - sp[:, 1], axis=1), np.linalg.norm(sp[:, 0] - sp[:, 2], axis=1)])
    kk = np.clip(np.ceil(edges / SAMPLE).astype(np.int64), 1, 24)
    for k in np.unique(kk):
        sel = kk == k
        a = sp[sel]; n = sn[sel]
        pts = []
        for i in range(k + 1):
            for j in range(k + 1 - i):
                u = i / k; v = j / k; w = 1 - u - v
                pts.append(a[:, 0] * w + a[:, 1] * u + a[:, 2] * v)
        pts = np.stack(pts, 1).reshape(-1, 3)
        nn = np.repeat(n, len(pts) // len(n), axis=0)
        inside = pts - nn * (CELL / 2)
        ix = np.floor((inside[:, 0] - origin[0]) / CELL).astype(np.int64)
        iy = np.floor((inside[:, 1] - origin[1]) / CELL).astype(np.int64)
        iz = np.floor((inside[:, 2] - origin[2]) / CELL).astype(np.int64)
        ok = (ix >= 0) & (ix < nx) & (iy >= 0) & (iy < ny) & (iz >= 0) & (iz < nz)
        idx = (iy[ok] * nz + iz[ok]) * nx + ix[ok]
        solid_flat[idx] = True

    packed = np.packbits(solid_flat, bitorder="little")

    climb_flat = np.zeros(nx * ny * nz, bool)
    cl_tris = []
    for gname, tris in groups:
        if gname in CLIMB and tris:
            arr = np.array(tris, np.int64)
            cl_tris.append((V[arr[:, :, 0]], VN[arr[:, 0, 2]]))
    for cp, cn in cl_tris:
        for k2 in [0.0, 0.1, -0.1]:
            pts = cp.mean(1) + cn * k2
            ix = np.floor((pts[:, 0] - origin[0]) / CELL).astype(np.int64)
            iy = np.floor((pts[:, 1] - origin[1]) / CELL).astype(np.int64)
            iz = np.floor((pts[:, 2] - origin[2]) / CELL).astype(np.int64)
            ok = (ix >= 0) & (ix < nx) & (iy >= 0) & (iy < ny) & (iz >= 0) & (iz < nz)
            climb_flat[(iy[ok] * nz + iz[ok]) * nx + ix[ok]] = True
    cpacked = np.packbits(climb_flat, bitorder="little")

    os.makedirs(out_dir, exist_ok=True)
    for t in tex_names:
        if t and os.path.exists(os.path.join(tex_dir, t)):
            shutil.copy(os.path.join(tex_dir, t), os.path.join(out_dir, t))

    shade_n = Nrm.copy()
    shade = np.where(shade_n[:, 1] > 0.5, 1.0, np.where(shade_n[:, 1] < -0.5, 0.45, np.where(np.abs(shade_n[:, 0]) > 0.5, 0.8, 0.65)))

    cx = np.floor((P[:, :, 0].mean(1) - origin[0]) / CHUNK).astype(np.int64)
    cz = np.floor((P[:, :, 2].mean(1) - origin[2]) / CHUNK).astype(np.int64)
    cid = cx * 100000 + cz

    chunks = []
    for c in np.unique(cid):
        csel = np.where(cid == c)[0]
        for t in np.unique(T[csel]):
            sel = csel[T[csel] == t]
            keys = K[sel].reshape(-1, 3)
            uk, inv = np.unique(keys, axis=0, return_inverse=True)
            inv = inv.reshape(-1)
            pos = V[uk[:, 0]]
            uv = VT[uk[:, 1]].copy(); uv[:, 1] = 1.0 - uv[:, 1]
            sh = np.repeat(shade[sel], 3)
            sh_v = np.zeros(len(uk))
            sh_v[inv] = sh
            col = np.clip(sh_v * 255, 0, 255).astype(np.uint8)
            rgba = np.stack([col, col, col, np.full(len(col), 255, np.uint8)], 1)
            if len(uk) > 65535:
                raise SystemExit(f"chunk too large in {name}: {len(uk)} verts, lower CHUNK")
            chunks.append((int(t), pos.astype(np.float32), uv.astype(np.float32), rgba, inv.reshape(-1, 3).astype(np.uint32)))

    alpha_tex = {i for i, t in enumerate(tex_names) if "RGBA" in t}
    with open(os.path.join(out_dir, "map.vmap"), "wb") as f:
        f.write(b"VMAP")
        f.write(struct.pack("<I", 2))
        f.write(struct.pack("<f", CELL))
        f.write(struct.pack("<3f", *origin))
        f.write(struct.pack("<3i", nx, ny, nz))
        f.write(struct.pack("<I", len(tex_names)))
        for i, t in enumerate(tex_names):
            b = t.encode("utf8")
            f.write(struct.pack("<I", len(b))); f.write(b)
            f.write(struct.pack("<B", 1 if i in alpha_tex else 0))
        f.write(struct.pack("<I", len(chunks)))
        for t, pos, uv, rgba, idx in chunks:
            mins = pos.min(0); maxs = pos.max(0)
            f.write(struct.pack("<I", t))
            f.write(struct.pack("<6f", *mins, *maxs))
            f.write(struct.pack("<II", len(pos), idx.size))
            vb = np.zeros(len(pos), dtype=[("p", "<f4", 3), ("u", "<f4", 2), ("c", "u1", 4)])
            vb["p"] = pos; vb["u"] = uv; vb["c"] = rgba
            f.write(vb.tobytes())
            f.write(idx.astype("<u4").tobytes())
        f.write(struct.pack("<I", len(packed)))
        f.write(packed.tobytes())
        f.write(struct.pack("<I", len(cpacked)))
        f.write(cpacked.tobytes())

    print(f"{name}: tris={len(P)} chunks={len(chunks)} grid={nx}x{ny}x{nz} solid={int(solid_flat.sum())} textures={tex_names} bytes={os.path.getsize(os.path.join(out_dir, 'map.vmap'))}")

if __name__ == "__main__":
    src = sys.argv[1]; dst = sys.argv[2]
    maps = [("village1", "Village1"), ("desert", "Desert"), ("snow", "Snowy"), ("beach", "Beach")]
    for name, base in maps:
        bake(name, os.path.join(src, base + ".obj"), os.path.join(src, base + ".mtl"), os.path.join(dst, name), src)
