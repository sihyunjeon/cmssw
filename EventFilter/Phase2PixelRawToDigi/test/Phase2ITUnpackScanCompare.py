#!/usr/bin/env python3
"""Plot one or more multiscale scans (Phase2ITUnpackScan.sh output) in CMS style.

Each histogram is written to its own PDF in an output directory, so single plots
can be dropped into a talk without cropping. With several scans the curve is the
mean and the band is the min-max spread.

The fused CPU producer is the reference the other flows are measured against; the
older split legacy chain is read from the CSV but not plotted.

    python3 Phase2ITUnpackScanCompare.py                       globs scan_*/results.csv
    python3 Phase2ITUnpackScanCompare.py a/results.csv b/...   explicit
    python3 Phase2ITUnpackScanCompare.py --out plots  ...      output directory

Axis ranges autoscale by default. Fix one for a talk either by editing LIMITS below
or, for a one-off, with --range <plot>:<x|y>:<lo>:<hi> (repeatable, <plot> matches
on any part of the file name):

    ... --range speedup:y:0:15 --range exec_time:x:0:600
"""
import csv
import glob
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import mplhep as hep
from matplotlib.lines import Line2D

hep.style.use(hep.style.CMS)

# CMS-ish qualitative colours; one per flow, stable across every figure
REF, ACPU, AGPU = "#964A8B", "#F89C20", "#3F9950"
LSTY = {1: "-", 2: "--", 4: ":", 8: "-."}            # linestyle encodes thread count
SAMPLE = r"$t\bar{t}$, $\langle$PU$\rangle$ = 200"   # no page header, so the sample
                                                     # is named in the legend title
MODCOLS = ("rawToPixelProducer", "phase2ITRawToBitStream", "phase2ITBitStreamToPixel")

# per-plot axis ranges, (lo, hi) or None to autoscale; --range overrides these
LIMITS = {
    "exec_time_vs_events.pdf":    {"x": None, "y": None},
    "time_per_event_flows.pdf":   {"x": None, "y": None},
    "alpaka_backend_threads.pdf": {"x": None, "y": None},
    "gpu_stage2_threads.pdf":     {"x": None, "y": None},
    "throughput_vs_threads.pdf":  {"x": None, "y": None},
    "speedup_vs_events.pdf":      {"x": None, "y": None},
}

# ---------------------------------------------------------------- arguments
paths, outdir, ranges, argv, i = [], "plots", [], sys.argv[1:], 0
while i < len(argv):
    a = argv[i]
    if a in ("--out", "--range"):
        if i + 1 >= len(argv):
            sys.exit(f"{a} needs a value")
        if a == "--out":
            outdir = argv[i + 1]
        else:
            ranges.append(argv[i + 1])
        i += 2
    elif a.startswith("--"):
        sys.exit(f"unknown option {a}")
    else:
        paths.append(a)
        i += 1
paths = paths or sorted(glob.glob("scan_*/results.csv"))

for spec in ranges:
    parts = spec.split(":")
    if len(parts) != 4 or parts[1] not in ("x", "y"):
        sys.exit(f"--range wants <plot>:<x|y>:<lo>:<hi>, got {spec!r}")
    name, axis, lo, hi = parts
    try:
        lo, hi = float(lo), float(hi)
    except ValueError:
        sys.exit(f"--range bounds must be numbers, got {spec!r}")
    hit = [k for k in LIMITS if name in k]
    if not hit:
        sys.exit(f"--range: nothing matches {name!r}; pick from {', '.join(sorted(LIMITS))}")
    for k in hit:
        LIMITS[k][axis] = (lo, hi)

# ---------------------------------------------------------------- load

scans = []
for path in paths:
    if not os.path.isfile(path):
        print(f"skipping {path}: not found")
        continue
    rows = []
    for r in csv.DictReader(open(path)):
        if r["eventsRun"] in ("", "FAILED"):
            continue
        r["threads"], r["N"] = int(r["threads"]), int(r["eventsRun"])
        # a kept row may come from a job that aborted after a complete report; the
        # numbers are valid but the run is flagged
        r["rc"] = int(r.get("rc") or 0)
        for k in MODCOLS:
            r[k] = float(r[k]) if r.get(k) else None
        rows.append(r)
    if rows:
        scans.append((os.path.basename(os.path.dirname(path)) or path, rows))
if not scans:
    sys.exit("no results.csv found")

os.makedirs(outdir, exist_ok=True)
THREADS = sorted({r["threads"] for _, rows in scans for r in rows})
BACKENDS = sorted({r["backend"] for _, rows in scans for r in rows})
GPU_BE = [b for b in BACKENDS if "gpu" in b]
CPU_BE = [b for b in BACKENDS if "gpu" not in b]
GPUNAME = next((r["gpu_name"] for _, rows in scans for r in rows if r.get("gpu_name")), "")
flagged = sum(1 for _, rows in scans for r in rows if r["rc"])
if flagged:
    print(f"note: {flagged} row(s) from runs that exited non-zero after a complete report")

be_name = lambda be: "GPU (Alpaka)" if "gpu" in be else "CPU (Alpaka)"
be_color = lambda be: AGPU if "gpu" in be else ACPU

def ms(*cols):
    """ms/event summed over module columns; None if the scan lacks any of them, so a
    job that did not schedule a flow drops out of the plot instead of crashing it"""
    def f(r):
        v = [r[c] for c in cols]
        return None if any(x is None for x in v) else sum(v) * 1e3
    return f

secs = lambda fn: (lambda r: None if fn(r) is None else fn(r) * r["N"] / 1e3)
ratio = lambda a, b: (lambda r: None if not a(r) or not b(r) else a(r) / b(r))

alp = ms("phase2ITRawToBitStream", "phase2ITBitStreamToPixel")
ref = ms("rawToPixelProducer")
s2g = ms("phase2ITBitStreamToPixel")

# ---------------------------------------------------------------- helpers
def points(be, t, fn):
    """per N across all scans: mean, min, max, and how many scans contributed"""
    per_n = {}
    for _, rows in scans:
        for r in rows:
            if r["backend"] == be and r["threads"] == t and fn(r) is not None:
                per_n.setdefault(r["N"], []).append(fn(r))
    ns = sorted(per_n)
    return (ns,
            [sum(per_n[n]) / len(per_n[n]) for n in ns],
            [min(per_n[n]) for n in ns],
            [max(per_n[n]) for n in ns],
            [len(per_n[n]) for n in ns])

def draw(ax, be, t, fn, color, ls="-", label=None, band=True):
    ns, mean, lo, hi, cnt = points(be, t, fn)
    if not ns:
        return False
    ax.plot(ns, mean, ls, color=color, lw=2, label=label)
    # filled marker where every scan contributed, hollow where only some did, so a
    # narrow band is never mistaken for agreement when it is really just sparse
    for n, m, c in zip(ns, mean, cnt):
        ax.plot(n, m, "o", color=color, ms=6, mfc=(color if c == len(scans) else "none"))
    if band and len(scans) > 1:
        full = [i for i, c in enumerate(cnt) if c == len(scans)]
        if full:
            ax.fill_between([ns[i] for i in full], [lo[i] for i in full],
                            [hi[i] for i in full], color=color, alpha=0.18, lw=0)
    return True

def common_n(be):
    """largest event count present in every scan for every thread count"""
    sets = [{r["N"] for r in rows if r["backend"] == be and r["threads"] == t}
            for _, rows in scans for t in THREADS]
    sets = [s for s in sets if s]
    shared = set.intersection(*sets) if sets else set()
    return max(shared) if shared else None

def figure(xlabel, ylabel):
    fig, ax = plt.subplots(figsize=(9, 8))
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.grid(alpha=0.25)
    return fig, ax

def save(fig, ax, name, legend_extra=()):
    handles = list(ax.get_legend_handles_labels()[0]) + list(legend_extra)
    if handles:
        ax.legend(handles=handles, fontsize=17, frameon=False, title=SAMPLE,
                  title_fontsize=17)
    ax.margins(y=0.15)
    lim = LIMITS.get(name, {})
    if lim.get("x"):
        ax.set_xlim(*lim["x"])
    if lim.get("y"):
        ax.set_ylim(*lim["y"])
    path = os.path.join(outdir, name)
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)
    print("  ", path)

thread_key = [Line2D([], [], color="gray", ls=LSTY.get(t, "-"), lw=2, label=f"{t} thread(s)")
              for t in THREADS]

print(f"CMS-style plots from {len(scans)} scan(s) -> {outdir}/")

# ---------------------------------------------------------------- 1. headline
fig, ax = figure("Number of events", "Unpacker execution time [s]")
for be in CPU_BE:
    draw(ax, be, 1, secs(ref), REF, "-", "CPU (Ref.)", band=False)
    draw(ax, be, 1, secs(alp), ACPU, "-", "CPU (Alpaka)", band=False)
for be in GPU_BE:
    draw(ax, be, 1, secs(alp), AGPU, "-", "GPU (Alpaka)", band=False)
save(fig, ax, "exec_time_vs_events.pdf")

# ---------------------------------------------------------------- 2. per-event, all flows
fig, ax = figure("Number of events", "Unpacker time per event [ms]")
for be in CPU_BE:
    draw(ax, be, 1, ref, REF, "-", "CPU (Ref.)")
    draw(ax, be, 1, alp, ACPU, "-", "CPU (Alpaka)")
for be in GPU_BE:
    draw(ax, be, 1, alp, AGPU, "-", "GPU (Alpaka)")
save(fig, ax, "time_per_event_flows.pdf")

# ---------------------------------------------------------------- 3. backend x threads
fig, ax = figure("Number of events", "Unpacker time per event [ms]")
for be in CPU_BE:
    for i, t in enumerate(THREADS):
        draw(ax, be, t, ref, REF, LSTY.get(t, "-"), "CPU (Ref.)" if i == 0 else None,
             band=False)
for be in BACKENDS:
    for i, t in enumerate(THREADS):
        draw(ax, be, t, alp, be_color(be), LSTY.get(t, "-"),
             be_name(be) if i == 0 else None, band=False)
save(fig, ax, "alpaka_backend_threads.pdf", thread_key)

# ---------------------------------------------------------------- 4. GPU stage 2 contention
if GPU_BE:
    fig, ax = figure("Number of events", "Decoding stage time per event [ms]")
    for be in GPU_BE:
        for t in THREADS:
            draw(ax, be, t, s2g, AGPU, LSTY.get(t, "-"), None)
    save(fig, ax, "gpu_stage2_threads.pdf", thread_key)

# ---------------------------------------------------------------- 5. throughput
# every curve is that flow's own rate, 1 / time per event, so the reference and the
# two backends are directly comparable; the job wall clock is not used here because
# one cmsRun schedules all three flows and cannot be attributed to any one of them
fig, ax = figure("nThreads", "Throughput [events / s]")

def scaling(be, fn, color, label):
    n = common_n(be)
    if n is None:
        return
    vals = []
    for t in THREADS:
        v = [1e3 / fn(r) for _, rows in scans for r in rows
             if r["backend"] == be and r["threads"] == t and r["N"] == n and fn(r)]
        if v:
            vals.append((t, sum(v) / len(v), min(v), max(v)))
    if not vals:
        return
    ts, mean, lo, hi = zip(*vals)
    ax.plot(ts, mean, "-o", color=color, lw=2, ms=7, label=label)
    ax.plot(ts, [mean[0] * t / ts[0] for t in ts], ":", color=color, lw=1.5, alpha=0.6)
    if len(scans) > 1:
        ax.fill_between(ts, lo, hi, color=color, alpha=0.18, lw=0)

for be in CPU_BE:
    scaling(be, ref, REF, "CPU (Ref.)")
for be in BACKENDS:
    scaling(be, alp, be_color(be), be_name(be))
ax.set_xticks(THREADS)
save(fig, ax, "throughput_vs_threads.pdf",
     [Line2D([], [], color="gray", ls=":", lw=1.5, label="Ideal Scaling")])

# ---------------------------------------------------------------- 6. speedup
fig, ax = figure("Number of events", "Speedup vs CPU (Ref.)")
for be in BACKENDS:
    ns, mean, _, _, _ = points(be, 1, ratio(ref, alp))
    if ns:
        ax.plot(ns, mean, "-o", color=be_color(be), lw=2, ms=6, label=be_name(be))
ax.axhline(1.0, color="gray", lw=1, ls="--")
save(fig, ax, "speedup_vs_events.pdf")

if GPUNAME:
    print(f"   GPU: {GPUNAME}")
