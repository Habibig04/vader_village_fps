import json,struct,glob,os,io,sys
import numpy as np
from PIL import Image
def qmat(q):
    x,y,z,w=q
    return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),0],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),0],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y),0],[0,0,0,1]])
def nmat(n):
    if 'matrix' in n:return np.array(n['matrix']).reshape(4,4).T
    T=np.eye(4);T[:3,3]=n.get('translation',[0,0,0])
    R=qmat(n.get('rotation',[0,0,0,1]));S=np.diag(list(n.get('scale',[1,1,1]))+[1])
    return T@R@S
def load(f):
    d=open(f,'rb').read();n=struct.unpack('<I',d[12:16])[0];j=json.loads(d[20:20+n]);o=20+n
    bl=struct.unpack('<I',d[o:o+4])[0];b=d[o+8:o+8+bl];return j,b
CT={5120:np.int8,5121:np.uint8,5122:np.int16,5123:np.uint16,5125:np.uint32,5126:np.float32}
NC={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}
def acc(j,b,i):
    a=j['accessors'][i];bv=j['bufferViews'][a['bufferView']];dt=np.dtype(CT[a['componentType']]);nc=NC[a['type']]
    st=bv.get('byteStride') or dt.itemsize*nc;off=bv.get('byteOffset',0)+a.get('byteOffset',0)
    raw=np.frombuffer(b,dtype=np.uint8,count=a['count']*st,offset=off).reshape(a['count'],st)
    return np.frombuffer(np.ascontiguousarray(raw[:,:dt.itemsize*nc]).tobytes(),dtype=dt).reshape(a['count'],nc)
def bake(f,name,out,skip=()):
    j,b=load(f);os.makedirs(out,exist_ok=True)
    nodes=j['nodes'];par={}
    for i,n in enumerate(nodes):
        for c in n.get('children',[]):par[c]=i
    def world(i):
        m=nmat(nodes[i])
        while i in par:i=par[i];m=nmat(nodes[i])@m
        return m
    imgs={}
    def img(ti):
        s=j['textures'][ti]['source']
        if s in imgs:return imgs[s]
        iv=j['bufferViews'][j['images'][s]['bufferView']];im=Image.open(io.BytesIO(b[iv.get('byteOffset',0):iv.get('byteOffset',0)+iv['byteLength']])).convert('RGB')
        im.thumbnail((1024,1024));fn=f'{name}_{s}.png';im.save(os.path.join(out,fn));imgs[s]=fn;return fn
    P=[];N=[];U=[];F={}
    off=0
    for ni,n in enumerate(nodes):
        if 'mesh' not in n or ni in skip:continue
        M=world(ni);R=np.linalg.inv(M[:3,:3]).T
        for p in j['meshes'][n['mesh']]['primitives']:
            at=p['attributes'];pos=acc(j,b,at['POSITION']).astype(np.float64)
            pos=(M[:3,:3]@pos.T).T+M[:3,3]
            nr=(R@acc(j,b,at['NORMAL']).astype(np.float64).T).T if 'NORMAL' in at else np.zeros_like(pos)
            uv=acc(j,b,at['TEXCOORD_0']).astype(np.float64) if 'TEXCOORD_0' in at else np.zeros((len(pos),2))
            idx=acc(j,b,p['indices'])[:,0].astype(np.int64) if 'indices' in p else np.arange(len(pos))
            mat=j['materials'][p['material']];bc=mat['pbrMetallicRoughness'].get('baseColorTexture')
            mn=f"m{p['material']}";tex=img(bc['index']) if bc else None
            F.setdefault(mn,[tex,[]])[1].append(idx.reshape(-1,3)+off)
            P.append(pos);N.append(nr);U.append(uv);off+=len(pos)
    P=np.vstack(P);N=np.vstack(N);U=np.vstack(U)
    ext=P.max(0)-P.min(0);ax=int(np.argmax(ext))
    # permute so barrel axis -> X, then choose up
    ctr=(P.max(0)+P.min(0))/2
    other=[i for i in range(3) if i!=ax]
    return P,N,U,F,ax,other,ctr
def write(name,out,P,N,U,F,fw,up,sgn_f,sgn_u,length):
    # fw,up: axis indices. new coords: x=side, y=up, z=-forward
    sd=[i for i in range(3) if i not in(fw,up)][0]
    P0=P.copy();c=(P.max(0)+P.min(0))/2
    f=(P[:,fw]-c[fw])*sgn_f;u=(P[:,up]-P[:,up].min())*sgn_u if sgn_u>0 else (P[:,up].max()-P[:,up])
    s=(P[:,sd]-c[sd])
    sc=length/(f.max()-f.min())
    # chirality: ensure right-handed (side,up,-fwd)
    M=np.zeros((3,3));M[0,sd]=1;M[1,up]=sgn_u;M[2,fw]=-sgn_f
    if np.linalg.det(M)<0:M[0,sd]=-1;s=-s
    X=np.stack([s,u,-f],1)*sc
    X[:,2]-=X[:,2].min()*0  # keep
    Nn=(M@N.T).T
    X[:,1]-=X[:,1].max()*0.0
    with open(os.path.join(out,name+'.obj'),'w') as o:
        o.write(f'mtllib {name}.mtl\n')
        for v in X:o.write('v %.5f %.5f %.5f\n'%tuple(v))
        for v in Nn:o.write('vn %.4f %.4f %.4f\n'%tuple(v))
        for v in U:o.write('vt %.5f %.5f\n'%(v[0],1-v[1]))
        for mn,(tex,fl) in F.items():
            o.write(f'usemtl {mn}\n')
            for tri in np.vstack(fl)+1:o.write('f '+' '.join(f'{t}/{t}/{t}' for t in tri)+'\n')
    with open(os.path.join(out,name+'.mtl'),'w') as o:
        for mn,(tex,fl) in F.items():
            o.write(f'newmtl {mn}\nKd 1 1 1\nKa 1 1 1\nKs 0 0 0\nd 1\n'+(f'map_Kd {tex}\n' if tex else ''))
    return X
if __name__=='__main__':
    srcs={'scar':'x/Scar 3D Model/Scar Set/29307548/1/Weapon-Scar.glb','shotgun':'x/Shotgun 3D Model/Shotgun Set/46780314/1/Weapon-Shotgun.glb','sniper':'x/Sniper 3D Model/Sniper Set/31055964/1/Weapon-Sniper.glb','tec9':'x/Tec-9 3D Model/Tec-9 Set/31196825/1/Weapon-Tec-9.glb'}
    for k,f in srcs.items():
        P,N,U,F,ax,other,ctr=bake(f,k,'raw')
        print(k,len(P),'verts','extent',(P.max(0)-P.min(0)).round(2),'longest axis',ax,'mats',{m:v[0] for m,v in F.items()})
        np.savez(f'raw_{k}.npz',P=P)
