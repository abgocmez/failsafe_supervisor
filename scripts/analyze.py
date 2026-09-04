#!/usr/bin/env python3
"""Summarize the measurements: percentiles, markdown tables, and dependency-free
SVG charts -- detection latency (linear) and tick jitter (log scale)."""
import csv
import math
import os
import sys

CSV = sys.argv[1] if len(sys.argv) > 1 else "results/latency.csv"
OUT_DIR = os.path.dirname(CSV) or "."
JITTER_CSV = os.path.join(OUT_DIR, "jitter.csv")

LABEL = {"kill9": "crash-stop (SIGKILL)",
         "stop": "fail-silent (SIGSTOP)",
         "stall": "timing / late (300ms stall)"}
ORDER = ["kill9", "stop", "stall"]


def pct(sorted_vals, p):
    if not sorted_vals:
        return 0.0
    k = (len(sorted_vals) - 1) * p / 100.0
    lo, hi = int(k), min(int(k) + 1, len(sorted_vals) - 1)
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * (k - lo)


def load(path):
    rows = {}
    with open(path) as f:
        for r in csv.DictReader(f):
            rows.setdefault(r["class"], {"detect": [], "recover": [], "path": r["path"]})
            rows[r["class"]]["detect"].append(float(r["detect_ms"]))
            rows[r["class"]]["recover"].append(float(r["recover_ms"]))
    return rows


def stats(vals):
    s = sorted(vals)
    return {"n": len(s), "p50": pct(s, 50), "p90": pct(s, 90),
            "p99": pct(s, 99), "max": max(s), "min": min(s)}


def markdown(rows):
    out = ["| fault class | path | n | detect p50 | detect p99 | detect max | recover p50 | recover p99 |",
           "|---|---|---:|---:|---:|---:|---:|---:|"]
    for c in ORDER:
        if c not in rows:
            continue
        d = stats(rows[c]["detect"]); r = stats(rows[c]["recover"])
        out.append("| %s | %s | %d | %.2f | %.2f | %.2f | %.1f | %.1f |" % (
            LABEL[c], rows[c]["path"].replace("DETECT_", ""), d["n"],
            d["p50"], d["p99"], d["max"], r["p50"], r["p99"]))
    return "\n".join(out) + "\n\n(all latencies in ms; detection measured from fault injection to the supervisor's monotonic-clock event)\n"


def latency_svg(rows):
    W, H = 880, 470
    ml, mr, mt, mb = 78, 24, 60, 96
    pw, ph = W - ml - mr, H - mt - mb
    classes = [c for c in ORDER if c in rows]
    series = [("p50", "#4c8bf5"), ("p99", "#f5a623"), ("max", "#d0021b")]
    data = {c: stats(rows[c]["detect"]) for c in classes}
    ymax = max(1.0, max(data[c]["max"] for c in classes))
    ymax = (int(ymax / 20) + 1) * 20

    def x(i): return ml + pw * i / len(classes)
    def y(v): return mt + ph * (1 - v / ymax)

    p = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" font-family="system-ui,sans-serif">' % (W, H, W, H)]
    p.append('<rect width="%d" height="%d" fill="#ffffff"/>' % (W, H))
    p.append('<text x="%d" y="30" font-size="19" font-weight="700">Detection latency by fault class (100 trials each)</text>' % ml)
    for g in range(0, int(ymax) + 1, 20):
        yy = y(g)
        p.append('<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="#e8e8e8"/>' % (ml, yy, W - mr, yy))
        p.append('<text x="%d" y="%.1f" font-size="13" text-anchor="end" fill="#555">%d</text>' % (ml - 8, yy + 4, g))
    p.append('<text x="20" y="%d" font-size="13" fill="#555" transform="rotate(-90 20 %d)">detection latency (ms)</text>' % (mt + ph / 2, mt + ph / 2))
    gw = pw / len(classes)
    bw = gw * 0.72 / len(series)
    for i, c in enumerate(classes):
        base = x(i) + gw * 0.14
        for j, (key, col) in enumerate(series):
            v = data[c][key]; bx = base + j * bw; by = y(v)
            p.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="%s" rx="2"/>' % (bx, by, bw - 3, mt + ph - by, col))
            p.append('<text x="%.1f" y="%.1f" font-size="11" text-anchor="middle" fill="#333">%.1f</text>' % (bx + bw / 2, by - 4, v))
        p.append('<text x="%.1f" y="%d" font-size="13" text-anchor="middle" font-weight="600">%s</text>' % (x(i) + gw / 2, H - mb + 24, LABEL[c].split(" (")[0]))
        p.append('<text x="%.1f" y="%d" font-size="11" text-anchor="middle" fill="#777">via %s</text>' % (x(i) + gw / 2, H - mb + 42, rows[c]["path"].replace("DETECT_", "")))
    lx = ml
    for key, col in series:
        p.append('<rect x="%d" y="%d" width="13" height="13" fill="%s" rx="2"/>' % (lx, H - 26, col))
        p.append('<text x="%d" y="%d" font-size="13">%s</text>' % (lx + 18, H - 15, key))
        lx += 78
    p.append('</svg>')
    return "\n".join(p)


def jitter_svg(path):
    if not os.path.exists(path):
        return None
    rows = list(csv.DictReader(open(path)))
    labels = {("baseline", "noload"): "baseline\nidle",
              ("rt", "noload"): "RT\nidle",
              ("baseline", "load"): "baseline\nunder load",
              ("rt+isolcpus", "load"): "RT + isolcpus\nunder load"}
    items = [(labels.get((r["config"], r["load"]), r["config"]),
              float(r["p99_us"]), float(r["max_us"])) for r in rows]
    W, H = 880, 480
    ml, mr, mt, mb = 80, 24, 60, 104
    pw, ph = W - ml - mr, H - mt - mb
    lo, hi = 1.0, 100000.0  # log decades 10^0..10^5 microseconds

    def y(v):
        v = max(lo, v)
        return mt + ph * (1 - (math.log10(v) - math.log10(lo)) / (math.log10(hi) - math.log10(lo)))

    p = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" font-family="system-ui,sans-serif">' % (W, H, W, H)]
    p.append('<rect width="%d" height="%d" fill="#ffffff"/>' % (W, H))
    p.append('<text x="%d" y="30" font-size="19" font-weight="700">Tick jitter: RT tuning vs baseline (log scale)</text>' % ml)
    dec = 0
    v = lo
    while v <= hi + 1:
        yy = y(v)
        p.append('<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="#e8e8e8"/>' % (ml, yy, W - mr, yy))
        lab = ("%gus" % v) if v < 1000 else ("%gms" % (v / 1000.0))
        p.append('<text x="%d" y="%.1f" font-size="12" text-anchor="end" fill="#555">%s</text>' % (ml - 8, yy + 4, lab))
        v *= 10; dec += 1
    p.append('<text x="20" y="%d" font-size="13" fill="#555" transform="rotate(-90 20 %d)">tick jitter (log)</text>' % (mt + ph / 2, mt + ph / 2))
    n = len(items)
    gw = pw / n
    series = [("p99", "#4c8bf5"), ("max", "#d0021b")]
    bw = gw * 0.6 / len(series)
    base_y = mt + ph
    for i, (lab, p99, mx) in enumerate(items):
        base = ml + gw * i + gw * 0.2
        for j, (key, col, val) in enumerate([("p99", "#4c8bf5", p99), ("max", "#d0021b", mx)]):
            bx = base + j * bw; by = y(val)
            p.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="%s" rx="2"/>' % (bx, by, bw - 3, base_y - by, col))
            txt = ("%.0fus" % val) if val < 1000 else ("%.1fms" % (val / 1000.0))
            p.append('<text x="%.1f" y="%.1f" font-size="11" text-anchor="middle" fill="#333">%s</text>' % (bx + bw / 2, by - 4, txt))
        for k, line in enumerate(lab.split("\n")):
            p.append('<text x="%.1f" y="%d" font-size="12" text-anchor="middle" font-weight="600" fill="#333">%s</text>' % (ml + gw * i + gw / 2, H - mb + 24 + k * 16, line))
    lx = ml
    for key, col in series:
        p.append('<rect x="%d" y="%d" width="13" height="13" fill="%s" rx="2"/>' % (lx, H - 24, col))
        p.append('<text x="%d" y="%d" font-size="13">%s</text>' % (lx + 18, H - 13, key))
        lx += 70
    p.append('</svg>')
    return "\n".join(p)


def main():
    rows = load(CSV)
    md = markdown(rows)
    print(md)
    with open(os.path.join(OUT_DIR, "summary.md"), "w") as f:
        f.write(md)
    with open(os.path.join(OUT_DIR, "latency.svg"), "w") as f:
        f.write(latency_svg(rows))
    js = jitter_svg(JITTER_CSV)
    if js:
        with open(os.path.join(OUT_DIR, "jitter.svg"), "w") as f:
            f.write(js)
        print("wrote latency.svg + jitter.svg + summary.md")
    else:
        print("wrote latency.svg + summary.md (no jitter.csv)")


if __name__ == "__main__":
    main()
