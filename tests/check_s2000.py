# Optional: bit-exact check of the S2000 engine against the independent Python model.
#   1) build tests, run:  congo_dsp_tests dump     (writes s2000_out_*.raw + s2000_meta.txt)
#   2) python check_s2000.py
import numpy as np
from s2000ref import run
x = np.fromfile('amen16x3.raw', dtype=np.int16); xx = np.tile(x, 3); ok = True
for line in open('s2000_meta.txt'):
    idx, pct, cyc, typ, xfd, q, a0, re_, lead = line.split()
    lead = int(lead)
    idx, pct, cyc, xfd, q, a0 = map(int, (idx, pct, cyc, xfd, q, a0))
    rt = np.fromfile(f's2000_out_{idx}.raw', dtype=np.int16).astype(int)
    # like the ROM, a stretch first plays 'lead' samples straight, then the periods start there
    if lead > 0:
        body, _ = run(xx, a0 + lead, len(rt) - lead, pct, cyc, typ, xfd, q, True)
        ref = np.concatenate([xx[a0:a0 + lead].astype(int), np.asarray(body, dtype=int)])
    else:
        ref, _ = run(xx, a0, len(rt), pct, cyc, typ, xfd, q, True)
    m = int(np.sum(ref[64:] != rt[64:])); ok &= m == 0
    print(f'[{"PASS" if m == 0 else "FAIL"}] S2000 {typ} {pct}% cycle {cyc} xfd {xfd} q {q}: {m} mismatches')
print('ALL MATCH' if ok else 'MISMATCH')
