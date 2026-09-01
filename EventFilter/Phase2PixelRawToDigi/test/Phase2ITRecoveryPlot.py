#!/usr/bin/env python3
"""Draw the recovery maps written by Phase2ITDigiRecovery: for each unpacking flow,
the digis fed to the packer, the digis that came back, and their difference.

    cmsRun Phase2ITUnpackAlpaka_cfg.py maxEvents=2 gapMode=KEEP accelerator=cpu recovery=1
    python3 Phase2ITRecoveryPlot.py phase2ITDigiRecovery_KEEP.root

One PDF per flow, plus a combined page. Δ is empty when the round trip is exact,
which is the point of the test, so the panel is annotated with the digi tally
rather than left to look like a plotting failure.
"""
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import uproot
from matplotlib.colors import LogNorm, TwoSlopeNorm

FLOWS = [("recoveryLegacy", "CPU (split)"),
         ("recoveryFused", "CPU (Ref.)"),
         ("recoveryAlpaka", "CPU (Alpaka)")]

args = [a for a in sys.argv[1:] if not a.startswith("--")]
outdir = "plots"
if "--out" in sys.argv:
    i = sys.argv.index("--out") + 1
    if i >= len(sys.argv):
        sys.exit("--out needs a directory")
    outdir = sys.argv[i]
    args = [a for a in args if a != outdir]
if not args:
    sys.exit("usage: Phase2ITRecoveryPlot.py <phase2ITDigiRecovery_*.root> [--out dir]")
path = args[0]
os.makedirs(outdir, exist_ok=True)
f = uproot.open(path)
mode = os.path.basename(path).replace("phase2ITDigiRecovery_", "").replace(".root", "")

def maps(folder):
    """the three maps as (col, row) arrays, cropped to the occupied region so the
    axes match the detector rather than the histogram bounds"""
    h = {k: f[f"{folder}/{k}"].values() for k in ("input", "output", "delta")}
    filled = (h["input"] != 0) | (h["output"] != 0)
    if not filled.any():
        return None
    cols, rows = np.where(filled)
    cx, rx = cols.max() + 1, rows.max() + 1
    return {k: v[:cx, :rx] for k, v in h.items()}, cx, rx

def panels(axes, m, cx, rx, label):
    vmax = max(m["input"].max(), m["output"].max())
    for ax, key, title in zip(axes, ("input", "output", "delta"), ("Input", "Output", r"$\Delta$(ADC)")):
        a = m[key].T                       # imshow wants (row, col)
        if key == "delta":
            lim = max(abs(a.min()), abs(a.max())) or 1.0
            im = ax.imshow(a, origin="lower", aspect="auto", cmap="bwr",
                           norm=TwoSlopeNorm(vmin=-lim, vcenter=0.0, vmax=lim),
                           extent=[0, cx, 0, rx], interpolation="nearest")
            cb = r"$\Delta$ ADC"
        else:
            im = ax.imshow(np.ma.masked_where(a <= 0, a), origin="lower", aspect="auto",
                           cmap="viridis", norm=LogNorm(vmin=1, vmax=max(vmax, 2)),
                           extent=[0, cx, 0, rx], interpolation="nearest")
            cb = r"$\Sigma$ ADC"
        ax.set_title(title)
        ax.set_xlabel("col")
        ax.set_ylabel("row")
        plt.colorbar(im, ax=ax, label=cb, fraction=0.046, pad=0.04)
    n_in, n_out = m["input"].sum(), m["output"].sum()
    worst = np.abs(m["delta"]).max()
    axes[2].text(0.5, 0.5, f"max |$\\Delta$| = {worst:g}\n" +
                 ("exact round trip" if worst == 0 else "MISMATCH"),
                 transform=axes[2].transAxes, ha="center", va="center", fontsize=11,
                 color=("green" if worst == 0 else "red"))
    axes[0].text(0.02, 0.98, label, transform=axes[0].transAxes, va="top", fontsize=11,
                 bbox=dict(fc="white", ec="0.7", alpha=0.85))
    return n_in, n_out, worst

print(f"{path}  (gap mode {mode})")
rows_ok = []
for folder, label in FLOWS:
    if folder not in [k.split(";")[0] for k in f.keys()]:
        print(f"  {folder}: absent, skipped")
        continue
    got = maps(folder)
    if got is None:
        print(f"  {folder}: empty maps, skipped")
        continue
    m, cx, rx = got
    fig, axes = plt.subplots(1, 3, figsize=(19, 6))
    n_in, n_out, worst = panels(axes, m, cx, rx, f"{label}, {mode}")
    fig.tight_layout()
    out = os.path.join(outdir, f"recovery_{folder.replace('recovery', '').lower()}_{mode}.pdf")
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    rows_ok.append((label, m, cx, rx, out))
    print(f"   {out}   in {n_in:.0f}, out {n_out:.0f}, max|delta| {worst:g}")

if rows_ok:
    fig, axes = plt.subplots(len(rows_ok), 3, figsize=(19, 6 * len(rows_ok)), squeeze=False)
    for row, (label, m, cx, rx, _) in zip(axes, rows_ok):
        panels(row, m, cx, rx, f"{label}, {mode}")
    fig.tight_layout()
    out = os.path.join(outdir, f"recovery_all_{mode}.pdf")
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    print(f"   {out}")
