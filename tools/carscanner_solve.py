#!/usr/bin/env python3
"""Given Car Scanner's raw log.txt + its decoded CSV (same session), find for
each CSV column the request, byte offset(s), and linear scale that reproduce
it. Alignment offset is found from RPM (known formula)."""
import csv, sys
import numpy as np
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from carscanner_parse import parse

log_path, csv_path = sys.argv[1], sys.argv[2]
raw = parse(log_path)
rows = list(csv.reader(open(csv_path, newline="")))
hdr = rows[0]
cols = {h: np.array([float(r[i]) if len(r) > i and r[i] != "" else np.nan for r in rows[1:]]) for i, h in enumerate(hdr) if i > 0}

def compact(a):
    return a[~np.isnan(a)]

n_cycles = len(raw["010C"])
# features per key: dict name -> array (nan where payload missing)
def features(key):
    pl = raw[key]
    L = max((len(p) for p in pl if p), default=0)
    feats = {}
    for i in range(L):
        for name, width, signed in (("u8", 1, False), ("i8", 1, True), ("u16", 2, False), ("i16", 2, True), ("u24", 3, False), ("u32", 4, False)):
            if i + width > L: continue
            arr = np.full(len(pl), np.nan)
            for k, p in enumerate(pl):
                if p and len(p) >= i + width:
                    arr[k] = int.from_bytes(p[i:i + width], "big", signed=signed)
            feats[(name, i)] = arr
    return feats

allfeats = {k: features(k) for k in raw}

# alignment from rpm
rpm_csv = compact(cols["Engine RPM (rpm)"])
rpm_log = np.array([((p[2] << 8) | p[3]) / 4.0 if p else np.nan for p in raw["010C"]])
def _score(d):
    m = min(n_cycles, len(rpm_csv) - d)
    return np.sum(np.abs(rpm_csv[d:d + m] - rpm_log[:m]) < 1.0) if m > 3000 else -1
best = max(range(0, len(rpm_csv)), key=_score)
print("alignment offset:", best, file=sys.stderr)

results = []
for h, col in cols.items():
    if h in ("Latitude", "Longtitude") or np.all(np.isnan(col)): continue
    c = compact(col)
    if len(c) < 1000 or np.nanstd(c) == 0: continue
    step = 2 if len(c) < 0.6 * len(rpm_csv) else 1   # coolant is polled every other cycle
    for shift in (-2, -1, 0, 1, 2):
        d = best + shift
        seg = c[d if step == 1 else d // 2 : (d if step == 1 else d // 2) + (n_cycles if step == 1 else n_cycles // 2)]
        for key, feats in allfeats.items():
            for (name, i), arr in feats.items():
                a = arr[::step][:len(seg)]
                m = ~np.isnan(a) & ~np.isnan(seg[:len(a)])
                if m.sum() < 500: continue
                x, y = a[m], seg[:len(a)][m]
                if np.std(x) == 0: continue
                r = np.corrcoef(x, y)[0, 1]
                if abs(r) > 0.995:
                    A = np.vstack([x, np.ones_like(x)]).T
                    coef = np.linalg.lstsq(A, y, rcond=None)[0]
                    err = np.max(np.abs(A @ coef - y))
                    results.append((h, key, name, i, shift, r, coef[0], coef[1], err, m.sum()))
seen = {}
for res in sorted(results, key=lambda t: (t[0], -abs(t[5]), t[8])):
    seen.setdefault(res[0], []).append(res)
for h, lst in seen.items():
    print(f"\n{h}")
    for (h, key, name, i, shift, r, a, b, err, n) in lst[:4]:
        print(f"   req {key} {name}@{i} shift={shift:+d} r={r:.5f}  value = {a:.6g}*x + {b:.6g}  (maxerr {err:.3g}, n={n})")
