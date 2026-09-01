#!/usr/bin/env python3
# Render the multiscale scan (Phase2ITUnpackScan.sh output) as a table page and
# four analysis plots.  Usage:  python3 Phase2ITUnpackScanPlot.py results.csv [out.pdf]
import csv
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages

CPU, GPU, LEGACY, FUSED, FAINT = "#0F766E", "#C2410C", "#64748B", "#1D4ED8", "#8B96A3"
MODS = ["rawToBitStreamProducer", "bitstreamToPixelProducer", "rawToPixelProducer",
        "phase2ITRawToBitStream", "phase2ITBitStreamToPixel"]

path = sys.argv[1] if len(sys.argv) > 1 else "results.csv"
out = sys.argv[2] if len(sys.argv) > 2 else "scan.pdf"
rows = [r for r in csv.DictReader(open(path)) if r["eventsRun"] not in ("", "FAILED")]
for r in rows:
    r["threads"] = int(r["threads"])
    r["N"] = int(r["eventsRun"])
    # a kept row may come from a job that aborted after reporting (e.g. a filesystem
    # error during shutdown); the numbers are valid but the run is flagged
    r["rc"] = int(r.get("rc") or 0)
    for m in MODS + ["cpu_per_event", "real_per_event"]:
        r[m] = float(r[m]) if r[m] else None
BACKENDS = sorted({r["backend"] for r in rows})
FLAGGED = sum(1 for r in rows if r["rc"])
if FLAGGED:
    print(f"note: {FLAGGED} of {len(rows)} rows come from runs that exited non-zero "
          f"after a complete report; drawn with hollow markers")

def sel(be, t):
    return sorted((r for r in rows if r["backend"] == be and r["threads"] == t), key=lambda r: r["N"])

def common_n(be):
    """largest event count present for EVERY thread count of this backend"""
    per_t = [{r["N"] for r in sel(be, t)} for t in threads if sel(be, t)]
    shared = set.intersection(*per_t) if per_t else set()
    return max(shared) if shared else None

def at_n(be, t, n):
    return next((r for r in sel(be, t) if r["N"] == n), None)

def mfc(r):
    """hollow marker for a run that exited non-zero"""
    return "none" if r["rc"] else None

def col(be, t, key):
    return [r["N"] for r in sel(be, t)], [r[key] * 1e3 for r in sel(be, t)]

threads = sorted({r["threads"] for r in rows})
gpu_name = next((r["gpu_name"] for r in rows if r.get("gpu_name")), "")

with PdfPages(out) as pdf:
    # ---- page 1: steady-state table (largest N per backend x threads) ----
    fig = plt.figure(figsize=(13.333, 7.5))
    fig.text(0.06, 0.93, "IT unpacker multiscale scan - steady state (largest N)",
             fontsize=17, fontweight="semibold")
    fig.text(0.06, 0.885, f"per event, TimeReport real time - {gpu_name}", fontsize=10.5, color=FAINT)
    hdr = ["backend", "thr", "legacy s1", "legacy s2", "legacy tot", "fused",
           "alpaka s1", "alpaka s2", "alpaka tot", "vs legacy", "real/ev [s]", "ev/s"]
    lines = [hdr]
    used_n = {}
    for be in BACKENDS:
        n = common_n(be)
        if n is None:
            continue
        used_n[be] = n
        for t in threads:
            r = at_n(be, t, n)
            if r is None:
                continue
            l1, l2 = r[MODS[0]] * 1e3, r[MODS[1]] * 1e3
            a1, a2 = r[MODS[3]] * 1e3, r[MODS[4]] * 1e3
            lines.append([be, str(t), f"{l1:.1f}", f"{l2:.1f}", f"{l1+l2:.1f}",
                          f"{r[MODS[2]]*1e3:.1f}", f"{a1:.2f}", f"{a2:.2f}", f"{a1+a2:.2f}",
                          f"{(l1+l2)/(a1+a2):.1f}x", f"{r['real_per_event']:.3f}",
                          f"{1/r['real_per_event']:.2f}"])
    tab = plt.table(cellText=lines[1:], colLabels=lines[0], loc="center",
                    cellLoc="right", colLoc="right", bbox=[0.05, 0.18, 0.9, 0.62])
    tab.auto_set_font_size(False)
    tab.set_fontsize(10.5)
    for (ri, ci), cell in tab.get_celld().items():
        cell.set_edgecolor("#DCE3EA")
        if ri == 0:
            cell.set_text_props(fontweight="semibold")
            cell.set_facecolor("#F1F5F9")
    fig.text(0.06, 0.11,
             "module columns in ms/event; s1 = raw->bitstream, s2 = bitstream->pixel(digi)\n"
             "N used per backend: " + ", ".join(f"{b}={n}" for b, n in used_n.items()),
             fontsize=9.5, color=FAINT)
    plt.axis("off")
    pdf.savefig(fig)
    plt.close(fig)

    # ---- page 2: four panels ----
    fig, ax = plt.subplots(2, 2, figsize=(13.333, 7.5))
    fig.subplots_adjust(hspace=0.42, wspace=0.28, left=0.07, right=0.97, top=0.92, bottom=0.09)

    # (a) module time vs N at 1 thread: flat = no hidden startup cost
    a = ax[0][0]
    for key, be, c, ls, lab in [
        (MODS[0], "cpu", LEGACY, "-", "legacy raw->bitstream"),
        (MODS[1], "cpu", LEGACY, "--", "legacy bitstream->pixel"),
        (MODS[2], "cpu", FUSED, "-", "fused raw->pixel"),
        (MODS[4], "cpu", CPU, "-", "alpaka s2 (serial)"),
        (MODS[4], "gpu-nvidia", GPU, "-", "alpaka s2 (T4)"),
        (MODS[3], "gpu-nvidia", GPU, ":", "alpaka s1 (T4)"),
    ]:
        s = sel(be, 1)
        if not s:
            continue
        a.plot([r["N"] for r in s], [r[key] * 1e3 for r in s], ls, color=c, lw=1.6, label=lab)
        for r in s:
            a.plot(r["N"], r[key] * 1e3, "o", color=c, ms=3.5, mfc=mfc(r))
    a.set_xlabel("events in job"); a.set_ylabel("ms / event")
    a.set_title("per-module cost vs job length (1 thread)", fontsize=11)
    if a.get_legend_handles_labels()[0]:
        a.legend(fontsize=7.5, ncol=2, frameon=False)
    a.grid(alpha=0.25)

    # (b) GPU stage 2 vs N per thread count: contention + convergence
    b = ax[0][1]
    gpu_be = next((b_ for b_ in BACKENDS if "gpu" in b_), None)
    for t, c in zip(threads, ("#F5A623", GPU, "#7C2D12", "#4A1A08")):
        s = sel(gpu_be, t) if gpu_be else []
        if not s:
            continue
        b.plot([r["N"] for r in s], [r[MODS[4]] * 1e3 for r in s], "-", color=c, lw=1.6,
               label=f"{t} thread(s)")
        for r in s:
            b.plot(r["N"], r[MODS[4]] * 1e3, "o", color=c, ms=3.5, mfc=mfc(r))
    b.set_xlabel("events in job"); b.set_ylabel("ms / event")
    b.set_title("alpaka stage 2 on the T4: thread contention", fontsize=11)
    if b.get_legend_handles_labels()[0]:
        b.legend(fontsize=8.5, frameon=False)
    b.grid(alpha=0.25)

    # (c) throughput scaling with threads (largest N)
    c_ = ax[1][0]
    for be in BACKENDS:
        c = GPU if "gpu" in be else CPU
        n = common_n(be)
        pts = [(t, at_n(be, t, n)) for t in threads] if n else []
        pts = [(t, r) for t, r in pts if r and r["real_per_event"]]
        if not pts:
            continue
        ts = [t for t, _ in pts]
        thr = [1 / r["real_per_event"] for _, r in pts]
        c_.plot(ts, thr, "-o", color=c, lw=1.8, label=f"{be} (N={n})")
        c_.plot(ts, [thr[0] * t / ts[0] for t in ts], "--", color=c, lw=0.9, alpha=0.5)
    c_.set_xticks(threads)
    c_.set_xlabel("framework threads"); c_.set_ylabel("events / s (whole job)")
    c_.set_title("job throughput vs threads (dashed = ideal)", fontsize=11)
    if c_.get_legend_handles_labels()[0]:
        c_.legend(fontsize=8.5, frameon=False)
    c_.grid(alpha=0.25)

    # (d) one-time cost amortization in the event-loop total
    d = ax[1][1]
    for be in BACKENDS:
        c = GPU if "gpu" in be else CPU
        s = [r for r in sel(be, 1) if r["cpu_per_event"] and r["N"] > 0]
        if len(s) < 2:
            continue
        x = [r["N"] for r in s]
        y = [r["cpu_per_event"] for r in s]
        d.plot(x, y, "-", color=c, lw=1.6, label=f"{be} t=1")
        for r in s:
            d.plot(r["N"], r["cpu_per_event"], "o", color=c, ms=3.5, mfc=mfc(r))
        t0 = (y[0] - y[-1]) * x[0]
        d.plot(x, [y[-1] + t0 / n for n in x], "--", color=c, lw=0.9, alpha=0.5)
    d.set_xlabel("events in job"); d.set_ylabel("event-loop CPU / event  [s]")
    d.set_title("one-time cost amortization (dashed: c + T0/N)", fontsize=11)
    if d.get_legend_handles_labels()[0]:
        d.legend(fontsize=8.5, frameon=False)
    d.grid(alpha=0.25)

    pdf.savefig(fig)
    plt.close(fig)

print("wrote", out)
