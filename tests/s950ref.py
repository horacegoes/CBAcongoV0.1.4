# Bit-exact model of the Akai S950 v1.2 STRETCH routine (ROM 0x7BF6-0x7F6E), offline.
import numpy as np

def sine_table():
    # ROM 0x7C17: coupled-form oscillator, 4097 entries, ES=0x1000
    x = 0x10000000          # [b6d4] 32-bit
    y = 0                   # si:bp 32-bit
    t = []
    for _ in range(0x1001):
        si = (y >> 16) & 0xFFFF; bp = y & 0xFFFF
        t.append((si + (1 if bp >= 0x8000 else 0)) & 0xFFFF)
        y = (y + 0x19 * ((x >> 16) & 0xFFFF)) & 0xFFFFFFFF
        x = (x - 0x19 * ((y >> 16) & 0xFFFF)) & 0xFFFFFFFF
    return np.array([v - 65536 if v >= 32768 else v for v in t], dtype=np.int64)

TABLE = sine_table()

def period(D, pct):
    # ROM 0x7C63: P = D*pct/|pct-100| (signed 16-bit idiv); on overflow D is halved and retried
    if pct == 100: return D, D
    while True:
        P = (D * pct) // abs(pct - 100)
        if P <= 0x7FFF: return P, D        # idiv quotient must fit int16
        D >>= 1

def sat16(v): return max(-32768, min(32767, v))

def stretch(x12, D, pct, poly=True, start=0, end=None):
    """x12: int array of signed 12-bit samples. Returns 16-bit words (12-bit DAC uses word>>4)."""
    x = np.asarray(x12, dtype=np.int64)
    n = len(x); end = n if end is None else end
    P, D = period(D, pct)
    if pct == 100: return (x << 4).copy()
    jump = -D if pct > 100 else D
    if pct > 100 and start < D: start = D       # ROM 0x7D52: zone start pushed to >= D
    out = [int(v) << 4 for v in x[:start]]       # pre-zone copied verbatim
    inc = (1 << 20) // P
    a = start
    def g(phase):
        idx = (phase >> 8) & 0xFFFF
        return int(TABLE[idx]) if poly else idx
    while True:
        b = a + jump
        seg = []
        for i in range(P):                       # pass 1: voice A fading out (write)
            v = (int(x[a + i]) * g((1 << 20) - i * inc)) >> 8
            seg.append(sat16(v))
        for i in range(P):                       # pass 2: voice B fading in (add, saturating)
            v = sat16((int(x[b + i]) * g(i * inc)) >> 8)
            seg[i] = sat16(seg[i] + v)
        out.extend(seg)
        a = b + P                                # voice B continues as next voice A
        nxt = a + P + (D if pct < 100 else 0)
        if nxt >= end: break                     # ROM 0x7E63 loop test
    out.extend(int(v) << 4 for v in x[a:n])      # tail copied verbatim (ROM 0x7E7C)
    return np.array(out, dtype=np.int64)

if __name__ == '__main__':
    t = TABLE
    print('table len', len(t), 'first', t[:6], 'last', t[-4:], 'max', t.max())
    import math
    ref = np.round(4096*np.sin(np.arange(4097)*25/65536))
    print('max |table - 4096 sin(25k/65536)|', np.abs(t-ref).max())
    for D,pct in [(1000,200),(1000,700),(1000,101),(1000,50),(400,375)]:
        print(D,pct,'->P,D',period(D,pct))
