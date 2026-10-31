import numpy as np,struct,os,shutil
from PIL import Image
from bake import bake
OUT='/home/claude/fps/assets/models'
def light(n,boost=1.0):
    n=n/np.maximum(np.linalg.norm(n,axis=1,keepdims=True),1e-9)
    b=0.58+0.30*(n[:,1]*0.5+0.5)+0.12*np.clip(n@np.array([0.5,0.3,0.8])/0.99,0,1)
    return np.clip(b*boost,0,1)
def wr(path,subs):
    with open(path,'wb') as f:
        f.write(struct.pack('<4sI',b'MDL1',len(subs)))
        for s in subs:
            tex=(s['tex'] or '').encode()[:63].ljust(64,b'\0')
            f.write(tex+bytes(s['rgba'])+struct.pack('<I',len(s['p'])))
            f.write(s['p'].astype('<f4').tobytes());f.write(s['n'].astype('<f4').tobytes());f.write(s['uv'].astype('<f4').tobytes());f.write(s['c'].astype(np.uint8).tobytes())
def weapon(name,src,skip,fw,length):
    P,N,U,F,_,_,_=bake(src,name,OUT,skip)
    up=1;sd=[i for i in range(3) if i not in(fw,up)][0]
    M=np.zeros((3,3));M[0,sd]=1;M[1,up]=1;M[2,fw]=-1
    if np.linalg.det(M)<0:M[0,sd]=-1
    X=(M@(P-(P.max(0)+P.min(0))/2).T).T
    sc=length/(X[:,2].max()-X[:,2].min());X*=sc
    X[:,1]-=(X[:,1].max()+X[:,1].min())/2
    Nn=(M@N.T).T;subs=[]
    for mn,(tex,fl) in F.items():
        t=np.vstack(fl).reshape(-1)
        c=light(Nn[t])*255;c=np.stack([c,c,c,np.full(len(c),255)],1)
        subs.append(dict(tex=tex,rgba=(255,255,255,255),p=X[t],n=Nn[t],uv=U[t],c=c))
    wr(f'{OUT}/{name}.mdl',subs);print(name,sum(len(s['p']) for s in subs)//3,'tris','muzzle z',X[:,2].min().round(2),'height',(X[:,1].max()-X[:,1].min()).round(2))
weapon('scar','x/Scar 3D Model/Scar Set/29307548/1/Weapon-Scar.glb',(),0,0.95)
weapon('shotgun','x/Shotgun 3D Model/Shotgun Set/46780314/1/Weapon-Shotgun.glb',(0,),0,1.0)
weapon('sniper','x/Sniper 3D Model/Sniper Set/31055964/1/Weapon-Sniper.glb',(),2,1.35)
weapon('tec9','x/Tec-9 3D Model/Tec-9 Set/31196825/1/Weapon-Tec-9.glb',(),0,0.55)
# ---- Vader
L=open('x/VADER/VADER/DARTH VADER.obj',errors='replace').read().split('\n')
v=[];vt=[];vn=[];cur=None;mat=None;skip={'FLOOR_Cylinder','ENV_Sphere'};faces={};
for l in L:
    if l.startswith('v '):v.append(l.split()[1:4])
    elif l.startswith('vt '):vt.append(l.split()[1:3])
    elif l.startswith('vn '):vn.append(l.split()[1:4])
    elif l.startswith('o '):cur=l[2:].strip()
    elif l.startswith('usemtl'):mat=l.split()[1].lower()
    elif l.startswith('f ') and cur not in skip:
        t=[tuple((int(x) if x else 0) for x in (tok.split('/')+['','',''])[:3]) for tok in l.split()[1:]]
        for i in range(1,len(t)-1):faces.setdefault(mat,[]).extend([t[0],t[i],t[i+1]])
v=np.array(v,float);vt=np.array(vt,float) if vt else np.zeros((1,2));vn=np.array(vn,float)
# bend T-pose arms down
th=np.radians(72)
sx=np.sign(v[:,0]);ax=np.abs(v[:,0]);w=np.clip((ax-0.5)/0.3,0,1)*(v[:,1]>2.55)
a=th*w;pxm=0.5;py=3.35
dx=ax-pxm;dy=v[:,1]-py
nx=dx*np.cos(a)+dy*np.sin(a);ny=-dx*np.sin(a)+dy*np.cos(a)
v2=v.copy();v2[:,0]=sx*(pxm+nx);v2[:,1]=py+ny
# normals: rotate with same weight (per vertex index approx via face vertex)
used=np.unique(np.array([f[0]-1 for fl in faces.values() for f in fl]))
u2=v2[used];hh=u2[:,1].max()-u2[:,1].min();S=1.95/hh;v2*=S;u2=v2[used]
v2[:,2]-=np.median(u2[:,2]);v2[:,1]-=u2[:,1].min();v2[:,0]-=(u2[:,0].min()+u2[:,0].max())/2
print('vader bounds',v2[used].min(0).round(2),v2[used].max(0).round(2))
col={'cloth':(.10,.10,.12),'metal':(.17,.17,.20),'glass':(.16,.03,.03),'glow':(1,.12,.08),'glove':(.09,.09,.09),'plastic':(.20,.20,.22),'utility_':(.55,.55,.6),'weapon':(.45,.45,.5)}
subs=[]
for m,fl in faces.items():
    vi=np.array([f[0]-1 for f in fl]);ti=np.array([f[1]-1 for f in fl]);ni=np.array([f[2]-1 for f in fl])
    p=v2[vi];n=vn[ni] if len(vn) and (ni>=0).all() else np.tile([0,1,0.],(len(vi),1))
    # rotate normals for bent verts
    aa=a[vi];ssx=sx[vi];nxm=ssx*n[:,0];n2=n.copy()
    n2[:,0]=ssx*(nxm*np.cos(aa)+n[:,1]*np.sin(aa));n2[:,1]=-nxm*np.sin(aa)+n[:,1]*np.cos(aa)
    b=light(n2) if m!='glow' else np.ones(len(p))
    c0=np.array(col.get(m,(.15,.15,.15)))
    c=np.clip(np.outer(b,c0)*255*1.0,0,255)
    c=np.concatenate([c,np.full((len(c),1),255)],1)
    subs.append(dict(tex=None,rgba=(255,255,255,255),p=p,n=n2,uv=np.zeros((len(p),2)),c=c));print(m,len(p)//3)
wr(f'{OUT}/vader.mdl',subs)
# preview front view
import matplotlib;matplotlib.use('Agg');import matplotlib.pyplot as plt
Q=np.vstack([s['p'] for s in subs]);i=np.random.choice(len(Q),20000,replace=False)
fig,ax_=plt.subplots(1,2,figsize=(8,5));ax_[0].scatter(Q[i,0],Q[i,1],s=.2);ax_[0].set_aspect('equal');ax_[1].scatter(Q[i,2],Q[i,1],s=.2);ax_[1].set_aspect('equal');plt.savefig('vader.png',dpi=60)
