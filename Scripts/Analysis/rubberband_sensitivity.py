#!/usr/bin/env python3
"""Pop threshold sensitivity: does the comparison between smoothing variants depend on the 750 cm/s choice?

Run rubberband_metrics.py once per threshold (--max-speed), then:
    python Scripts/Analysis/rubberband_sensitivity.py 600=<metrics dir> 750=<metrics dir> 900=<metrics dir> [--out DIR]

Writes sensitivity_report.md (pops/min per condition x variant x threshold, Ada-T vs Exp reduction,
rank agreement between thresholds: Kendall tau over variants within each condition).
"""
from __future__ import annotations

import argparse
import csv
import statistics
from collections import defaultdict
from itertools import combinations
from pathlib import Path

ORDER = ["Exp", "Exp-0.10", "Exp-0.15", "Exp-0.25", "Exp-0.40", "Ada-T", "Ada-T-g12.0", "Ada-T-m0.40", "Ada-T-m0.40-g12.0"]


def load(path: Path):
    vals = defaultdict(list)
    with (path / "summary.csv").open(encoding="utf-8-sig", newline="") as stream:
        for r in csv.DictReader(stream):
            try:
                vals[(r["Condition"], r["SmoothingMode"])].append(float(r["SpikePerMin"]))
            except (KeyError, ValueError):
                pass
    return {k: statistics.fmean(v) for k, v in vals.items()}


def kendall(a, b):
    pairs = list(combinations(range(len(a)), 2))
    if not pairs:
        return None
    s = 0
    for i, j in pairs:
        x, y = a[i] - a[j], b[i] - b[j]
        s += (x > 0) - (x < 0) if y == 0 else ((x > 0) - (x < 0)) * ((y > 0) - (y < 0))
    return s / len(pairs)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("runs", nargs="+", help="threshold=metrics_dir")
    ap.add_argument("--out", type=Path)
    args = ap.parse_args()
    data = {}
    for item in args.runs:
        th, path = item.split("=", 1)
        data[int(th)] = load(Path(path))
    ths = sorted(data)
    out = args.out or Path(args.runs[0].split("=", 1)[1])
    out.mkdir(parents=True, exist_ok=True)
    conds = sorted({c for d in data.values() for c, _ in d})
    lines = ["# 튐 기준 민감도", "", f"튐 기준(화면 속도 cm/s): {', '.join(map(str, ths))}. 값은 튐/분 평균(조건마다 3판).", ""]
    lines += ["## Ada-T의 튐 감소율 (Exp 대비)", "", "| 조건 | " + " | ".join(f"{t}: Exp → Ada-T" for t in ths) + " |",
              "|---|" + "---|" * len(ths)]
    for c in conds:
        cells = []
        for t in ths:
            e, a = data[t].get((c, "Exp")), data[t].get((c, "Ada-T"))
            cells.append("" if e is None or a is None else f"{e:.1f} → {a:.1f} ({'-' if e == 0 else f'{100 * (1 - a / e):.0f}%'})")
        lines.append(f"| {c} | " + " | ".join(cells) + " |")
    lines += ["", "## 방식 간 순위 일치도 (Kendall τ, 1 = 순위 동일)", "", "| 조건 | 방식 수 | " + " | ".join(f"{a} vs {b}" for a, b in combinations(ths, 2)) + " |",
              "|---|---|" + "---|" * len(list(combinations(ths, 2)))]
    for c in conds:
        variants = [v for v in ORDER if all((c, v) in data[t] for t in ths)]
        if len(variants) < 3:
            continue
        cells = []
        for a, b in combinations(ths, 2):
            k = kendall([data[a][(c, v)] for v in variants], [data[b][(c, v)] for v in variants])
            cells.append(f"{k:.2f}")
        lines.append(f"| {c} | {len(variants)} | " + " | ".join(cells) + " |")
    lines += ["", "## 전체 값", "", "| 조건 | 방식 | " + " | ".join(str(t) for t in ths) + " |", "|---|---|" + "---|" * len(ths)]
    for c in conds:
        for v in ORDER:
            if any((c, v) in data[t] for t in ths):
                lines.append(f"| {c} | {v} | " + " | ".join("" if (c, v) not in data[t] else f"{data[t][(c, v)]:.1f}" for t in ths) + " |")
    (out / "sensitivity_report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines[:40]))
    print(f"-> {out / 'sensitivity_report.md'}")


if __name__ == "__main__":
    main()
