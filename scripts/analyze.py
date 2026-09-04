#!/usr/bin/env python3
"""Summarize the fault-injection measurements: percentiles per class, a markdown
table, and a dependency-free SVG bar chart (detection latency p50/p99/max)."""
import csv
import os
import sys

CSV = sys.argv[1] if len(sys.argv) > 1 else "results/latency.csv"
OUT_DIR = os.path.dirname(CSV) or "."

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


def svg(rows):
    W, H = 720, 420
    ml, mr, mt, mb = 70, 20, 56, 90
    pw, ph = W - ml - mr, H - mt - mb
    classes = [c for c in ORDER if c in rows]
    series = [("p50", "#4c8bf5"), ("p99", "#f5a623"), ("max", "#d0021b")]
    ymax = 0.0
    data = {}
    for c in classes:
        d = stats(rows[c]["detect"])
        data[c] = d
        ymax = max(ymax, d["max"])
    ymax = (int(ymax / 20) + 1) * 20  # round up to 20

    def x(i): return ml + pw * i / len(classes)
    def y(v): return mt + ph * (1 - v / ymax)

    p = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
         'viewBox="0 0 %d %d" font-family="system-ui,sans-serif">' % (W, H, W, H)]
    p.append('<rect width="%d" height="%d" fill="#ffffff"/>' % (W, H))
    p.append('<text x="%d" y="28" font-size="17" font-weight="700">Detection latency by fault class (100 trials each)</text>' % ml)
    # y grid + labels
    for g in range(0, int(ymax) + 1, 20):
        yy = y(g)
        p.append('<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="#e5e5e5"/>' % (ml, yy, W - mr, yy))
        p.append('<text x="%d" y="%.1f" font-size="12" text-anchor="end" fill="#555">%d</text>' % (ml - 8, yy + 4, g))
    p.append('<text x="18" y="%d" font-size="12" fill="#555" transform="rotate(-90 18 %d)">detection latency (ms)</text>' % (mt + ph / 2, mt + ph / 2))
    # bars
    gw = pw / len(classes)
    bw = gw * 0.7 / len(series)
    for i, c in enumerate(classes):
        base = x(i) + gw * 0.15
        for j, (key, col) in enumerate(series):
            v = data[c][key]
            bx = base + j * bw
            by = y(v)
            p.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="%s"/>' % (bx, by, bw - 2, mt + ph - by, col))
            p.append('<text x="%.1f" y="%.1f" font-size="10" text-anchor="middle" fill="#333">%.1f</text>' % (bx + bw / 2, by - 3, v))
        p.append('<text x="%.1f" y="%d" font-size="12" text-anchor="middle" font-weight="600">%s</text>' % (x(i) + gw / 2, H - mb + 20, LABEL[c].split(" (")[0]))
        p.append('<text x="%.1f" y="%d" font-size="10" text-anchor="middle" fill="#777">%s</text>' % (x(i) + gw / 2, H - mb + 36, rows[c]["path"].replace("DETECT_", "")))
    # legend
    lx = ml
    for key, col in series:
        p.append('<rect x="%d" y="%d" width="12" height="12" fill="%s"/>' % (lx, H - 24, col))
        p.append('<text x="%d" y="%d" font-size="12">%s</text>' % (lx + 16, H - 14, key))
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
        f.write(svg(rows))
    print("wrote %s/summary.md and %s/latency.svg" % (OUT_DIR, OUT_DIR))


if __name__ == "__main__":
    main()
