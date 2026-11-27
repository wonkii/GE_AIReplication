#!/usr/bin/env python3
"""B sweep figures from rubberband_metrics.py output (summary.csv).

Usage:
    python Scripts/Analysis/rubberband_sweep_plot.py <metrics folder> [<metrics folder> ...] [--out DIR]

Reads every summary.csv, averages rows per (Condition, SmoothingMode) and writes into --out (default: first folder):
  sweep_table.csv      mean / sd / n per condition x variant for the key metrics
  sweep_report.md      the same as markdown tables (the table view for the figures)
  fig_jitter_sweep.png Exp 1 - jitter width at a fixed 200 ms mean (L200, J190-210 ... J50-350): pops/min and stop lag
  fig_tradeoff.png     Exp 2/3 - per condition: stop lag (cost) vs pops/min, fixed Exp times as a line, Ada-T variants as points
  fig_mechanism.png    Exp 1 cause check - p95 peak screen speed right after big corrections vs jitter width
Figures are skipped (with a note) when the needed conditions are missing.
"""
from __future__ import annotations

import argparse
import csv
import re
import statistics
from collections import defaultdict
from pathlib import Path

METRICS = ["SpikePerMin", "StopLagMs", "SettleMean", "JerkP95", "BigCorrPeakSpeedP95", "BigCorrPopPct",
           "GapAfterShift", "SmoothLocationTime", "JitterMean"]
# Reference categorical palette (validated, light mode): Exp blue, Ada-T orange, tuning variants after.
EXP_COLOR, ADA_COLOR = "#257de0", "#f47230"
TUNE_COLORS = ["#00b894", "#f2a600", "#ee7fac", "#6a4ce2"]
TEXT, TEXT2, GRID = "#191919", "#666666", "#e6e6e6"


def num(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def cond_info(c):
    m = re.match(r"^(N0|L(\d+)|J(\d+)-(\d+))(?:-P(\d+))?$", c or "", re.I)
    lo = hi = loss = 0
    if m:
        if m.group(2):
            lo = hi = int(m.group(2))
        elif m.group(3):
            lo, hi = int(m.group(3)), int(m.group(4))
        loss = int(m.group(5) or 0)
    return lo, hi, loss


def exp_time(variant):
    if variant == "Exp":
        return 0.06
    m = re.match(r"^Exp-(\d+\.\d+)$", variant or "")
    return float(m.group(1)) if m else None


def load(folders):
    groups = defaultdict(lambda: defaultdict(list))
    for folder in folders:
        path = folder / "summary.csv" if folder.is_dir() else folder
        with path.open(encoding="utf-8-sig", newline="") as stream:
            for row in csv.DictReader(stream):
                key = (row["Condition"], row["SmoothingMode"])
                for m in METRICS:
                    v = num(row.get(m))
                    if v is not None:
                        groups[key][m].append(v)
    table = {}
    for key, values in groups.items():
        table[key] = {m: (statistics.fmean(v), statistics.stdev(v) if len(v) > 1 else 0.0, len(v))
                      for m, v in values.items() if v}
    return table


def style(ax, title, xlabel, ylabel):
    ax.set_title(title, color=TEXT, fontsize=11, loc="left")
    ax.set_xlabel(xlabel, color=TEXT2, fontsize=9)
    ax.set_ylabel(ylabel, color=TEXT2, fontsize=9)
    ax.grid(True, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color("#bbbbbb")
    ax.tick_params(colors=TEXT2, labelsize=8)
    ax.set_ylim(bottom=0)


def get(table, cond, variant, metric):
    return table.get((cond, variant), {}).get(metric)


def fig_jitter(table, out, plt):
    conds = sorted({c for c, _ in table if sum(cond_info(c)[:2]) == 400 and cond_info(c)[2] == 0},
                   key=lambda c: cond_info(c)[1] - cond_info(c)[0])
    if len(conds) < 3:
        return "fig_jitter_sweep: needs L200 and J<a>-<b> conditions with a 200 ms mean (rb.Plan group J)"
    fig, axes = plt.subplots(1, 2, figsize=(10, 3.8), dpi=150)
    for variant, color, marker in (("Exp", EXP_COLOR, "o"), ("Ada-T", ADA_COLOR, "s")):
        for ax, metric in zip(axes, ("SpikePerMin", "StopLagMs")):
            pts = [(cond_info(c)[1] - cond_info(c)[0], get(table, c, variant, metric)) for c in conds]
            pts = [(x, v) for x, v in pts if v is not None]
            if not pts:
                continue
            xs = [x for x, _ in pts]
            ax.errorbar(xs, [v[0] for _, v in pts], yerr=[v[1] for _, v in pts], color=color, marker=marker,
                        markersize=6, linewidth=2, capsize=3, label=variant)
            ax.annotate(variant, (xs[-1], pts[-1][1][0]), xytext=(6, 0), textcoords="offset points",
                        color=TEXT, fontsize=8, va="center")
    style(axes[0], "Pops per minute vs jitter width (mean delay 200 ms)", "jitter width = max - min delay (ms)", "pops / min")
    style(axes[1], "Stop reaction lag vs jitter width", "jitter width = max - min delay (ms)", "stop lag (ms, median)")
    axes[0].legend(frameon=False, fontsize=8)
    fig.tight_layout()
    fig.savefig(out / "fig_jitter_sweep.png")
    plt.close(fig)
    return None


def fig_tradeoff(table, out, plt):
    conds = [c for c in ("N0", "J50-150", "J100-300") if any(k[0] == c and exp_time(k[1]) for k in table)]
    if not conds:
        return "fig_tradeoff: needs N0/J50-150/J100-300 with Exp variants (rb.Plan group T)"
    fig, axes = plt.subplots(1, len(conds), figsize=(4.2 * len(conds), 3.9), dpi=150, squeeze=False)
    tune_variants = sorted({v for _, v in table if v.startswith("Ada-T-")})
    for ax, cond in zip(axes[0], conds):
        exps = sorted(((exp_time(v), v) for c, v in table if c == cond and exp_time(v)), key=lambda t: t[0])
        pts = [(get(table, cond, v, "StopLagMs"), get(table, cond, v, "SpikePerMin"), t) for t, v in exps]
        pts = [p for p in pts if p[0] and p[1]]
        ax.plot([p[0][0] for p in pts], [p[1][0] for p in pts], color=EXP_COLOR, marker="o", markersize=6,
                linewidth=2, label="Exp fixed time")
        for x, y, t in pts:
            ax.annotate(f"{t:.2f}s", (x[0], y[0]), xytext=(5, 5), textcoords="offset points", color=TEXT2, fontsize=7)
        for variant, color, marker in [("Ada-T", ADA_COLOR, "s")] + [(v, TUNE_COLORS[i % 4], "D") for i, v in enumerate(tune_variants)]:
            x, y = get(table, cond, variant, "StopLagMs"), get(table, cond, variant, "SpikePerMin")
            if not x or not y:
                continue
            ax.errorbar(x[0], y[0], xerr=x[1], yerr=y[1], color=color, marker=marker, markersize=8,
                        markeredgecolor="white", markeredgewidth=1.5, capsize=3, linestyle="none", label=variant)
            ax.annotate(variant, (x[0], y[0]), xytext=(6, -10), textcoords="offset points", color=TEXT, fontsize=7)
        style(ax, cond, "stop reaction lag (ms) - lower is faster", "pops / min - lower is smoother")
    axes[0][0].legend(frameon=False, fontsize=7, loc="upper right")
    fig.suptitle("Trade-off: longer fixed smoothing = fewer pops but slower; adaptive moves along it", color=TEXT, fontsize=11, x=0.01, ha="left")
    fig.tight_layout()
    fig.savefig(out / "fig_tradeoff.png")
    plt.close(fig)
    return None


def fig_mechanism(table, out, plt):
    conds = sorted({c for c, _ in table if sum(cond_info(c)[:2]) == 400 and cond_info(c)[2] == 0},
                   key=lambda c: cond_info(c)[1] - cond_info(c)[0])
    if len(conds) < 3:
        return "fig_mechanism: needs the 200 ms mean jitter conditions (rb.Plan group J)"
    fig, ax = plt.subplots(figsize=(5.5, 3.8), dpi=150)
    for variant, color, marker in (("Exp", EXP_COLOR, "o"), ("Ada-T", ADA_COLOR, "s")):
        pts = [(cond_info(c)[1] - cond_info(c)[0], get(table, c, variant, "BigCorrPeakSpeedP95")) for c in conds]
        pts = [(x, v) for x, v in pts if v is not None]
        if pts:
            ax.errorbar([x for x, _ in pts], [v[0] for _, v in pts], yerr=[v[1] for _, v in pts], color=color,
                        marker=marker, markersize=6, linewidth=2, capsize=3, label=variant)
    ax.axhline(750, color=TEXT2, linewidth=1, linestyle="--")
    ax.annotate("pop threshold 750 cm/s", (0, 750), xytext=(2, 4), textcoords="offset points", color=TEXT2, fontsize=7)
    style(ax, "Peak screen speed right after big corrections (p95)", "jitter width (ms)", "cm/s")
    ax.legend(frameon=False, fontsize=8)
    fig.tight_layout()
    fig.savefig(out / "fig_mechanism.png")
    plt.close(fig)
    return None


def write_tables(table, out):
    def cond_key(c):
        lo, hi, loss = cond_info(c)
        return (lo + hi, hi - lo, loss, c)
    keys = sorted(table, key=lambda k: (cond_key(k[0]), k[1]))
    with (out / "sweep_table.csv").open("w", encoding="utf-8", newline="") as stream:
        w = csv.writer(stream)
        w.writerow(["Condition", "Variant"] + [f"{m}_{s}" for m in METRICS for s in ("mean", "sd", "n")])
        for k in keys:
            w.writerow(list(k) + [x for m in METRICS for x in (table[k].get(m) or ("", "", ""))])
    lines = ["# B 스윕 결과 (평균 ± 표준편차, n = 판 수)", ""]
    for m, label in [("SpikePerMin", "튐/분"), ("StopLagMs", "멈춤 반응 지연 ms"), ("SettleMean", "보정 후 수렴 s"),
                     ("BigCorrPeakSpeedP95", "큰 보정 직후 화면 최대 속도 p95 cm/s"), ("BigCorrPopPct", "큰 보정 중 튐 비율 %"),
                     ("GapAfterShift", "지연 뺀 화면-서버 차이 cm"), ("SmoothLocationTime", "평균 스무딩 시간 s")]:
        variants = sorted({k[1] for k in keys}, key=lambda v: (0 if v == "Exp" else 1 if exp_time(v) else 2, exp_time(v) or 0, v))
        conds = sorted({k[0] for k in keys}, key=cond_key)
        lines += [f"## {label}", "", "| 조건 | " + " | ".join(variants) + " |", "|---|" + "---|" * len(variants)]
        for c in conds:
            cells = []
            for v in variants:
                x = get(table, c, v, m)
                cells.append("" if not x else f"{x[0]:.3g} ± {x[1]:.2g} (n={x[2]})" if x[2] > 1 else f"{x[0]:.3g}")
            lines.append(f"| {c} | " + " | ".join(cells) + " |")
        lines.append("")
    (out / "sweep_report.md").write_text("\n".join(lines), encoding="utf-8")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folders", nargs="+", type=Path, help="rubberband_metrics.py output folders (or summary.csv files)")
    ap.add_argument("--out", type=Path)
    args = ap.parse_args()
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    out = args.out or (args.folders[0] if args.folders[0].is_dir() else args.folders[0].parent)
    out.mkdir(parents=True, exist_ok=True)
    table = load(args.folders)
    write_tables(table, out)
    for make in (fig_jitter, fig_tradeoff, fig_mechanism):
        note = make(table, out, plt)
        print(f"skip  {note}" if note else f"wrote {make.__name__.replace('fig_', 'fig_')}")
    print(f"-> {out}")


if __name__ == "__main__":
    main()
