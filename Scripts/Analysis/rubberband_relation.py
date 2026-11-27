#!/usr/bin/env python3
"""Rubber-banding B: test the pop model on recorded corrections, and derive the adaptive gain from it.

Model
  A correction of size D shown with smoothing time constant tau moves the mesh at about v + D/tau right after it.
  A pop is a frame faster than 1.5 * v_max, so with v ~ v_max a correction pops when D/tau > 0.5 * v_max.
  => no pop needs tau >= D / (0.5 * v_max).  With D growing with the arrival gap g (D ~ c * v_max * g) this is tau >= 2c * g:
  the smoothing time has to scale with the jitter, which is what Ada-T (tau = T0 + k * (J - deadzone)) does.

Input: rb.Batch / rb.Plan recordings (Saved/Experiments/Rubberband/<RunId>/<Condition>/).
Corrections that arrive in the same frame (bunched packets) are merged: D = |last New - first Old|.

Usage:
    python Scripts/Analysis/rubberband_relation.py Saved/Experiments/Rubberband --batch P1127-1001 [--out DIR]

Outputs (default Saved/Tasks/<date>-rubberband-relation):
  relation_corrections.csv  one row per (merged) correction: D, tau, D/tau, gap, jitter, measured peak speed after it
  relation_report.md        pop probability by D/tau, D vs gap, required tau per condition and the implied gain k
  fig_relation_pop.png      measured pop probability and peak speed vs D/tau (model threshold marked)
  fig_relation_tau.png      required tau (to keep 95 % / 99 % of corrections below the pop line) vs jitter, with Ada-T k=6/12
"""
from __future__ import annotations

import argparse
import bisect
import csv
import math
import statistics
from collections import defaultdict
from pathlib import Path

from recording_paths import recording_path
from task_readme import describe_task, task_id

EXP_COLOR, ADA_COLOR = "#257de0", "#f47230"
TUNE_COLORS = ["#00b894", "#f2a600", "#ee7fac", "#6a4ce2"]
TEXT, TEXT2, GRID = "#191919", "#666666", "#e6e6e6"


def read_csv(path):
    if not path.exists():
        return []
    with path.open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def f(row, key, default=None):
    try:
        return float(row[key])
    except (KeyError, TypeError, ValueError):
        return default


def vec(row, prefix):
    return (f(row, prefix + "X", 0.0), f(row, prefix + "Y", 0.0), f(row, prefix + "Z", 0.0))


def dist(a, b):
    return math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))


def pct(values, q):
    if not values:
        return None
    s = sorted(values)
    return s[min(len(s) - 1, int(q * (len(s) - 1) + 0.5))]


def moving_player(server_rows, min_travel=100.0):
    first, best = {}, {}
    for r in server_rows:
        pid = r.get("PlayerId")
        p = vec(r, "Pos")
        first.setdefault(pid, p)
        best[pid] = max(best.get(pid, 0.0), dist(p, first[pid]))
    movers = [pid for pid, d in best.items() if d >= min_travel]
    return movers[0] if movers else None


SPIKES = []  # (variant, predicted) per pop frame: was any correction in the window before it predicted to pop?


def analyse_folder(folder: Path, args):
    pid = moving_player(read_csv(folder / "server_samples.csv"))
    if pid is None:
        return []
    out = []
    for corr_file in sorted(folder.glob("client_*_corrections.csv")):
        client = corr_file.name[len("client_"):-len("_corrections.csv")]
        # Buffer variants (M4) have no smoothing time constant: the D/tau model does not apply.
        corr = [r for r in read_csv(corr_file) if r.get("PlayerId") == pid and r.get("NoSmooth") != "1"
                and not (r.get("Variant") or "").startswith("Buf")]
        samples = [r for r in read_csv(folder / f"client_{client}_samples.csv")
                   if r.get("NetRole") == "Simulated" and r.get("PlayerId") == pid]
        if not corr or len(samples) < 10:
            continue
        samples.sort(key=lambda r: f(r, "HostTime", 0.0))
        st = [f(r, "HostTime", 0.0) for r in samples]
        sv = [vec(r, "Visual") for r in samples]
        svel = [vec(r, "Vel") for r in samples]
        # horizontal visual speed per sample step, stamped at the later sample
        hs_t, hs_v = [], []
        for i in range(1, len(st)):
            dt = st[i] - st[i - 1]
            if 0 < dt <= 0.2:
                hs_t.append(st[i])
                hs_v.append(math.hypot(sv[i][0] - sv[i - 1][0], sv[i][1] - sv[i - 1][1]) / dt)
        corr.sort(key=lambda r: f(r, "HostTime", 0.0))
        groups, cur = [], [corr[0]]
        for r in corr[1:]:
            if f(r, "HostTime", 0.0) - f(cur[-1], "HostTime", 0.0) <= args.same_frame:
                cur.append(r)
            else:
                groups.append(cur)
                cur = [r]
        groups.append(cur)
        prev_t = None
        first = len(out)
        for g in groups:
            t = f(g[-1], "HostTime", 0.0)
            d = dist(vec(g[0], "Old"), vec(g[-1], "New"))
            tau = f(g[-1], "SmoothLocationTime", 0.0)
            gap = None if prev_t is None else f(g[0], "HostTime", 0.0) - prev_t
            prev_t = t
            i = bisect.bisect_right(hs_t, t)
            window = []
            while i < len(hs_t) and hs_t[i] - t <= args.window:
                window.append(hs_v[i])
                i += 1
            if not window or tau <= 0:
                continue
            # vector model: capsule velocity just before the correction + correction vector / tau (horizontal)
            j = max(0, bisect.bisect_left(st, f(g[0], "HostTime", 0.0)) - 1)
            v = svel[j]
            dv = [vec(g[-1], "New")[k] - vec(g[0], "Old")[k] for k in range(2)]
            pred = math.hypot(v[0] + dv[0] / tau, v[1] + dv[1] / tau)
            out.append({
                "RunId": g[0].get("RunId"), "Condition": g[0].get("Condition"), "Variant": g[0].get("Variant"),
                "ClientId": client, "HostTime": round(t, 4), "Merged": len(g),
                "D": round(d, 2), "Tau": round(tau, 4), "DOverTau": round(d / tau, 1),
                "Gap": None if gap is None else round(gap, 4), "Jitter": f(g[-1], "Jitter"),
                "SpeedBefore": f(g[0], "Speed"), "PeakSpeed": round(max(window), 1),
                "PredPeak": round(pred, 1), "Pop": int(max(window) > args.max_speed),
                "PredPop": int(pred > args.max_speed)})
        mine = out[first:]
        mt = [r["HostTime"] for r in mine]
        variant = mine[0]["Variant"] if mine else None
        for t, v in zip(hs_t, hs_v):
            if v <= args.max_speed:
                continue
            i = bisect.bisect_right(mt, t)
            k = i - 1
            predicted = False
            while k >= 0 and t - mt[k] <= args.window:
                predicted = predicted or bool(mine[k]["PredPop"])
                k -= 1
            SPIKES.append((variant, predicted))
    return out


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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("roots", nargs="+", type=Path)
    ap.add_argument("--batch", help="only RunIds starting with this id")
    ap.add_argument("--out", type=Path)
    ap.add_argument("--vmax", type=float, default=500.0, help="character max walk speed (cm/s)")
    ap.add_argument("--max-speed", type=float, default=750.0, help="pop threshold, same as rubberband_metrics.py")
    ap.add_argument("--window", type=float, default=0.1, help="peak search window after a correction (s)")
    ap.add_argument("--same-frame", type=float, default=0.002, help="corrections closer than this (s) are one frame")
    ap.add_argument("--t0", type=float, default=0.06)
    ap.add_argument("--deadzone", type=float, default=0.010)
    args = ap.parse_args()

    folders = sorted({p.parent for root in args.roots for p in recording_path(root).rglob("server_samples.csv")})
    if args.batch:
        folders = [p for p in folders if p.parent.name.startswith(args.batch)]
    if not folders:
        raise SystemExit("no recordings found")
    out = args.out or Path(__file__).resolve().parents[2] / "Saved" / "Tasks" / task_id("rubberband-relation")
    describe_task(out, "Rubberband B 관계식 검증", "보정 크기/스무딩 시간(D/tau)으로 튐을 예측하는 모델 검증과 적응 기울기 유도",
                  f"rubberband_relation.py roots={[str(r) for r in args.roots]} batch={args.batch} vmax={args.vmax} max_speed={args.max_speed}",
                  ["relation_corrections.csv: 보정 단위 표", "relation_report.md: 요약", "fig_relation_pop.png, fig_relation_tau.png"])
    rows = []
    for folder in folders:
        rows += analyse_folder(folder, args)
    with (out / "relation_corrections.csv").open("w", encoding="utf-8", newline="") as stream:
        w = csv.DictWriter(stream, list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    line = args.max_speed - args.vmax  # D/tau at which v_max + D/tau = max_speed (250 for 750 / 500)
    lines = ["# Rubberband B 관계식 검증", "",
             f"모델: 보정 직후 화면 속도 ≈ v + D/τ. 튐 기준 {args.max_speed:.0f}cm/s = 1.5·v_max(v_max={args.vmax:.0f})이므로 "
             f"**D/τ > {line:.0f}cm/s면 튄다**. 보정 {len(rows)}건(같은 프레임에 온 보정은 합침, 순간이동 제외).", ""]

    # 1. pop probability by D/tau bin (all variants)
    edges = [0, 50, 100, 150, 200, 250, 300, 400, 500, 700, 1000, 1e9]
    lines += ["## 1. D/τ 구간별 실제 튐 확률", "", "| D/τ (cm/s) | 보정 수 | 튐 확률 % | 보정 직후 최대 속도 중앙값 | p95 |", "|---|---|---|---|---|"]
    bins = []
    for lo, hi in zip(edges, edges[1:]):
        sel = [r for r in rows if lo <= r["DOverTau"] < hi]
        if not sel:
            continue
        pk = [r["PeakSpeed"] for r in sel]
        p = 100.0 * sum(r["Pop"] for r in sel) / len(sel)
        bins.append((lo, hi, len(sel), p, statistics.median(pk), pct(pk, 0.95)))
        label = f"{lo:.0f}~{hi:.0f}" if hi < 1e8 else f"{lo:.0f}~"
        lines.append(f"| {label} | {len(sel)} | {p:.1f} | {statistics.median(pk):.0f} | {pct(pk, 0.95):.0f} |")
    below = [r for r in rows if r["DOverTau"] <= line]
    above = [r for r in rows if r["DOverTau"] > line]
    tp = sum(r["Pop"] for r in above)
    fn = sum(r["Pop"] for r in below)
    pops = tp + fn
    lines += ["", f"- 모델 경계 D/τ = {line:.0f} 기준: 실제 튐 {pops}건 중 경계를 넘은 보정에서 생긴 것 {tp}건"
              f" ({100.0 * tp / pops if pops else 0:.1f}%). 경계를 넘은 보정 {len(above)}건 중 실제로 튄 것 {tp}건"
              f" ({100.0 * tp / len(above) if above else 0:.1f}%). 경계 아래 보정 {len(below)}건 중 튄 것 {fn}건"
              f" ({100.0 * fn / len(below) if below else 0:.2f}%).", ""]

    tp2 = sum(1 for r in rows if r["Pop"] and r["PredPop"])
    fp2 = sum(1 for r in rows if not r["Pop"] and r["PredPop"])
    fn2 = sum(1 for r in rows if r["Pop"] and not r["PredPop"])
    lines += ["### 벡터 모델: 예측 속도 = |v(보정 직전 캡슐 속도) + D벡터/τ|", "",
              f"- 예측 {args.max_speed:.0f} 초과 {tp2 + fp2}건 중 실제 튐 {tp2}건 (정밀도 {100.0 * tp2 / max(1, tp2 + fp2):.1f}%), "
              f"실제 튐 {tp2 + fn2}건 중 예측한 것 {tp2}건 (재현율 {100.0 * tp2 / max(1, tp2 + fn2):.1f}%)."]
    sp_pred = sum(1 for _, pr in SPIKES if pr)
    lines += [f"- 튐 프레임 기준 {len(SPIKES)}개 중 직전 {args.window:.1f}s 안에 예측 속도 > {args.max_speed:.0f}인 보정이 있었던 것 "
              f"{sp_pred}개 (재현율 {100.0 * sp_pred / max(1, len(SPIKES)):.1f}%)."]
    pk_bins = [0, 500, 600, 700, 750, 800, 900, 1000, 1200, 1e9]
    lines += ["", "| 예측 속도 (cm/s) | 보정 수 | 실제 튐 % | 실제 최대 속도 중앙값 |", "|---|---|---|---|"]
    vec_bins = []
    for lo, hi in zip(pk_bins, pk_bins[1:]):
        sel = [r for r in rows if lo <= r["PredPeak"] < hi]
        if sel:
            pp = 100.0 * sum(r["Pop"] for r in sel) / len(sel)
            vec_bins.append((lo, hi, pp, statistics.median([r["PeakSpeed"] for r in sel])))
            lines.append(f"| {lo:.0f}~{'' if hi > 1e8 else f'{hi:.0f}'} | {len(sel)} | {pp:.1f} | {statistics.median([r['PeakSpeed'] for r in sel]):.0f} |")
    lines.append("")

    # per variant check
    lines += ["### 방식별", "", "| 방식 | 보정 수 | 실제 튐 % | D/τ > 경계 % | 경계 넘은 보정 중 튐 % |", "|---|---|---|---|---|"]
    by_var = defaultdict(list)
    for r in rows:
        by_var[r["Variant"]].append(r)
    for v in sorted(by_var):
        sel = by_var[v]
        ab = [r for r in sel if r["DOverTau"] > line]
        lines.append(f"| {v} | {len(sel)} | {100.0 * sum(r['Pop'] for r in sel) / len(sel):.2f} | "
                     f"{100.0 * len(ab) / len(sel):.2f} | {100.0 * sum(r['Pop'] for r in ab) / len(ab) if ab else 0:.0f} |")
    lines.append("")

    # 2. D vs arrival gap, required tau per condition (D does not depend on smoothing: capsule extrapolation error)
    by_cond = defaultdict(list)
    for r in rows:
        by_cond[r["Condition"]].append(r)
    lines += ["## 2. 조건별 보정 크기와 필요한 스무딩 시간", "",
              f"필요 τ_q = D의 q 분위수 / {line:.0f}: 보정의 q만큼을 튐 경계 아래로 두는 최소 스무딩 시간. "
              "D는 캡슐 외삽 오차라 메시 스무딩과 무관하므로 같은 조건의 모든 방식을 합쳐 쓴다.", "",
              "| 조건 | 추정 지터 J ms | 간격 p95 ms | D p95 cm | D p99 cm | D p95 / 간격 p95 (cm/s) | 필요 τ95 s | 필요 τ99 s | Ada-T k=6 τ | k=12 τ |",
              "|---|---|---|---|---|---|---|---|---|---|"]
    tau_pts = []
    for c in sorted(by_cond, key=lambda c: statistics.fmean([r["Jitter"] for r in by_cond[c] if r["Jitter"] is not None] or [0])):
        sel = by_cond[c]
        J = statistics.fmean([r["Jitter"] for r in sel if r["Jitter"] is not None])
        gaps = [r["Gap"] for r in sel if r["Gap"]]
        d95, d99 = pct([r["D"] for r in sel], 0.95), pct([r["D"] for r in sel], 0.99)
        g95 = pct(gaps, 0.95)
        t95, t99 = d95 / line, d99 / line
        k6 = min(max(args.t0 + 6 * max(0.0, J - args.deadzone), args.t0), 0.25)
        k12 = min(max(args.t0 + 12 * max(0.0, J - args.deadzone), args.t0), 0.25)
        tau_pts.append((c, J, t95, t99))
        lines.append(f"| {c} | {J * 1000:.1f} | {g95 * 1000:.0f} | {d95:.1f} | {d99:.1f} | {d95 / g95:.0f} | {t95:.3f} | {t99:.3f} | {k6:.3f} | {k12:.3f} |")

    def slope(qi):
        xs = [max(0.0, p[1] - args.deadzone) for p in tau_pts]
        ys = [p[qi] - args.t0 for p in tau_pts]
        num = sum(x * y for x, y in zip(xs, ys))
        den = sum(x * x for x in xs)
        return num / den if den else None

    k95, k99 = slope(2), slope(3)
    lines += ["", f"- τ = T0 + k·max(0, J − {args.deadzone * 1000:.0f}ms)에 맞춘 k(원점 지나는 최소제곱): "
              f"**τ95 → k ≈ {k95:.1f}**, **τ99 → k ≈ {k99:.1f}**. 실험한 k=6(기본), k=12와 비교.", ""]
    (out / "relation_report.md").write_text("\n".join(lines), encoding="utf-8")
    print("\n".join(lines))

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(1, 2, figsize=(10, 3.8), dpi=150)
    mids = [(lo + min(hi, 1400)) / 2 for lo, hi, *_ in bins]
    axes[0].plot(mids, [b[3] for b in bins], color=EXP_COLOR, marker="o", markersize=6, linewidth=2)
    axes[0].axvline(line, color=TEXT2, linestyle="--", linewidth=1)
    axes[0].annotate(f"model: D/tau = {line:.0f}", (line, 0), xytext=(4, 6), textcoords="offset points", color=TEXT2, fontsize=7)
    style(axes[0], "Measured pop probability per correction", "D / tau (cm/s)", "pop probability (%)")
    axes[1].plot(mids, [b[4] for b in bins], color=EXP_COLOR, marker="o", markersize=6, linewidth=2, label="median")
    axes[1].plot(mids, [b[5] for b in bins], color=ADA_COLOR, marker="s", markersize=6, linewidth=2, label="p95")
    xs = [0, 1400]
    axes[1].plot(xs, [args.vmax + x for x in xs], color=TEXT2, linewidth=1, linestyle=":", label="model v_max + D/tau")
    axes[1].axhline(args.max_speed, color=TEXT2, linestyle="--", linewidth=1)
    style(axes[1], "Peak screen speed after a correction", "D / tau (cm/s)", "cm/s")
    axes[1].legend(frameon=False, fontsize=7)
    for ax in axes:
        ax.set_xlim(0, 1400)
        ax.set_ylim(bottom=0)
    fig.tight_layout()
    fig.savefig(out / "fig_relation_pop.png")
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(5.8, 3.9), dpi=150)
    js = [p[1] * 1000 for p in tau_pts]
    ax.plot(js, [p[2] for p in tau_pts], color=EXP_COLOR, marker="o", markersize=6, linewidth=0, label="needed tau (95%)")
    ax.plot(js, [p[3] for p in tau_pts], color=TUNE_COLORS[2], marker="D", markersize=6, linewidth=0, label="needed tau (99%)")
    jj = [i / 1000 for i in range(0, 41)]
    for k, color, label in ((6, ADA_COLOR, "Ada-T k=6"), (12, TUNE_COLORS[0], "Ada-T k=12")):
        ax.plot([j * 1000 for j in jj], [min(max(args.t0 + k * max(0.0, j - args.deadzone), args.t0), 0.25) for j in jj],
                color=color, linewidth=2, label=label)
    style(ax, "Smoothing time needed vs estimated jitter", "estimated jitter J (ms, RFC 3550)", "tau (s)")
    ax.set_ylim(bottom=0)
    ax.legend(frameon=False, fontsize=7)
    fig.tight_layout()
    fig.savefig(out / "fig_relation_tau.png")
    plt.close(fig)
    print(f"-> {out}")


if __name__ == "__main__":
    main()
