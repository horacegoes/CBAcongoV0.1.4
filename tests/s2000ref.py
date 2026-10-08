# Independent Python model of the Akai S2000 OS 2.00 STRETCH (per-period, 16-bit)
import numpy as np
def ratio88(p): return (p<<8)//abs(p-100)
def cfg(q,hi): return ((q+1)*2,40,2000) if hi else (q+1,20,1000)
def maxlen(ratio,xfd,rng):
    aba=min(ratio,(xfd+1)<<8); prod=aba*rng
    return 0x4000 if prod>>24 else min(prod>>8,0x4000)
def search(x,pos,comp,ratio,xfd,q,hi):
    qp1,minl,rng=cfg(q,hi); aba=min(ratio,(xfd+1)<<8); mx=maxlen(ratio,xfd,rng)
    best=[0xFFFFFFFF, minl+4]
    def scan(lag,n,fine):
        prod=lag*aba; ln=(prod>>8)&0xFFFF
        if prod>>24 or ln>mx: ln=mx
        st=max(1,ln//n); off=lag if comp else -lag
        s=sum(abs(int(x[pos+k*st])-int(x[pos+k*st+off])) for k in range(n))
        return s>>1 if fine else s
    cnt=(rng+1-minl)>>3; tbl=[]; lag=minl+4
    for j in range(cnt):
        s=scan(lag,qp1,False); tbl.append(s)
        if s<best[0]: best[:]=[s,lag]
        lag+=8
    lag=minl
    for j in range(cnt):
        h=tbl[j]>>1
        if h+(h>>1)<best[0]:
            for f in range(4):
                s=scan(lag,qp1*2,True)
                if s<best[0]: best[:]=[s,lag]
                lag+=2
        else: lag+=8
    return best[1]
def run(x,a0,N,pct,C0,typ,xfd,q,hi=True):
    ratio=ratio88(pct); comp=pct<100; out=[]; a=a0; cycles=[]
    while len(out)<N:
        C=search(x,a,comp,ratio,xfd,q,hi) if typ=='intel' else C0
        cycles.append(C)
        P=max(1,(C*ratio)>>8); L=min((xfd+1)*C,P); inc=(0x7FFF0000//L) if L else 0x7FFF0000
        b=a+(C if comp else -C); lo=hi16=0
        for i in range(P):
            g=hi16
            cur=int(x[a+i]); nw=int(x[b+i])
            out.append(((cur*(0x7FFF-g))>>15)+((nw*g)>>15))
            t=lo+(inc&0xFFFF); lo=t&0xFFFF
            h=(hi16+(inc>>16)+(t>>16))&0xFFFF; hi16=0x7FFF if h&0x8000 else h
        a=b+P
    return np.array(out[:N]),cycles
