#!/usr/bin/env python3
"""Rubber-banding mitigation (B) metrics: how a remote character looks on each client.

Input: folders written by URubberbandRecordingSubsystem
    Saved/Experiments/Rubberband/<RunId>/<Condition>/{server_samples.csv, client_<Id>_samples.csv, client_<Id>_corrections.csv}
Every folder below the given roots that contains server_samples.csv is one condition.

Only rows with NetRole=Simulated (another player's character as seen by this client) are scored.
Times are aligned with HostTime (FPlatformTime::Seconds, shared by processes on one PC).

Metrics per condition x client x player:
  GapMean/P95/Max      |visual - server(t)|, same wall-clock time (includes latency)
  BestShift, GapAfterShift  delay d minimising mean |visual(t) - server(t - d)|, and the remaining error
  PopCount / PopPerMin  frames whose visual step differs from the (delay-shifted) server step by more than --pop-cm
  SpeedSpikes          frames whose visual speed exceeds --max-speed (cm/s)
  AccelP95, JerkP95    smoothness of the visual path
  Corrections, CorrDist*, NoSmooth  received corrections (NoSmooth = beyond NetworkNoSmoothUpdateDistance, teleported)
  SettleMean/P95       time from a correction until |visual - capsule| < --settle-cm
"""
from __future__ import annotations

import argparse
import bisect
import csv
import math
import statistics
from collections import Counter, defaultdict
from pathlib import Path

from recording_paths import recording_path
from task_readme import describe_task, task_id

MODE_NAMES = {0: "Disabled", 1: "Linear", 2: "Exponential", 3: "Replay"}


def read_csv(path: Path) -> list[dict[str, str]]:
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


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def norm(a):
    return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])


def pct(values, q):
    if not values:
        return None
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(q * (len(ordered) - 1) + 0.5))]


def mean(values):
    return statistics.fmean(values) if values else None


def r(value, digits=2):
    return None if value is None else round(value, digits)


class Track:
    """Server position over HostTime with linear interpolation; None outside the recorded span or across gaps."""

    def __init__(self, samples, max_gap=0.25):
        samples.sort(key=lambda s: s[0])
        self.t = [s[0] for s in samples]
        self.p = [s[1] for s in samples]
        self.max_gap = max_gap

    def at(self, time):
        i = bisect.bisect_left(self.t, time)
        if i < len(self.t) and self.t[i] == time:
            return self.p[i]
        if i == 0 or i >= len(self.t):
            return None
        t0, t1 = self.t[i - 1], self.t[i]
        if t1 - t0 > self.max_gap:
            return None
        a = (time - t0) / (t1 - t0)
        p0, p1 = self.p[i - 1], self.p[i]
        return (p0[0] + (p1[0] - p0[0]) * a, p0[1] + (p1[1] - p0[1]) * a, p0[2] + (p1[2] - p0[2]) * a)


def shift_curve(track, samples, max_shift, step):
    curve = []
    for k in range(int(round(max_shift / step)) + 1):
        d = k * step
        errors = [norm(sub(s["visual"], p)) for s in samples if (p := track.at(s["t"] - d)) is not None]
        if len(errors) >= max(10, len(samples) // 2):
            curve.append((d, statistics.fmean(errors)))
    return curve


def analyse_player(samples, track, corrections, args):
    samples.sort(key=lambda s: s["t"])
    gaps = [norm(sub(s["visual"], p)) for s in samples if (p := track.at(s["t"])) is not None]
    curve = shift_curve(track, samples, args.max_shift, args.shift_step)
    best = min(curve, key=lambda c: c[1]) if curve else (None, None)
    shift = best[0] or 0.0

    pops = spikes = 0
    speeds, accels, jerks = [], [], []
    prev_v = prev_a = None
    for a, b in zip(samples, samples[1:]):
        dt = b["t"] - a["t"]
        if dt <= 0 or dt > 0.2:
            prev_v = prev_a = None
            continue
        step = sub(b["visual"], a["visual"])
        s0, s1 = track.at(a["t"] - shift), track.at(b["t"] - shift)
        if s0 is not None and s1 is not None and norm(sub(step, sub(s1, s0))) > args.pop_cm:
            pops += 1
        v = (step[0] / dt, step[1] / dt, step[2] / dt)
        if norm((v[0], v[1], 0.0)) > args.max_speed:
            spikes += 1
        speeds.append(norm(v))
        if prev_v is not None:
            acc = ((v[0] - prev_v[0]) / dt, (v[1] - prev_v[1]) / dt, (v[2] - prev_v[2]) / dt)
            accels.append(norm(acc))
            if prev_a is not None:
                jerks.append(norm(((acc[0] - prev_a[0]) / dt, (acc[1] - prev_a[1]) / dt, (acc[2] - prev_a[2]) / dt)))
            prev_a = acc
        prev_v = v

    settle = []
    times = [s["t"] for s in samples]
    for c in corrections:
        i = bisect.bisect_right(times, c["t"])
        while i < len(samples) and samples[i]["t"] - c["t"] <= args.settle_max:
            if norm(sub(samples[i]["visual"], samples[i]["capsule"])) < args.settle_cm:
                settle.append(samples[i]["t"] - c["t"])
                break
            i += 1
    span = samples[-1]["t"] - samples[0]["t"] if len(samples) > 1 else 0.0
    dists = [c["dist"] for c in corrections]
    modes = Counter(s["mode"] for s in samples)
    times_used = Counter(round(s["smooth_time"], 4) for s in samples)
    return {
        "SmoothingMode": MODE_NAMES.get(modes.most_common(1)[0][0], "?") if modes else None,
        "SmoothLocationTime": times_used.most_common(1)[0][0] if times_used else None,
        "DurationSec": r(span),
        "Frames": len(samples),
        "GapMean": r(mean(gaps)), "GapP95": r(pct(gaps, 0.95)), "GapMax": r(max(gaps) if gaps else None),
        "BestShiftSec": r(best[0], 3), "GapAfterShift": r(best[1]),
        "PopCount": pops, "PopPerMin": r(pops / span * 60 if span else None),
        "SpeedSpikes": spikes,
        "AccelP95": r(pct(accels, 0.95), 0), "JerkP95": r(pct(jerks, 0.95), 0),
        "Corrections": len(corrections), "CorrPerSec": r(len(corrections) / span if span else None),
        "CorrDistMean": r(mean(dists)), "CorrDistP95": r(pct(dists, 0.95)), "CorrDistMax": r(max(dists) if dists else None),
        "NoSmooth": sum(1 for c in corrections if c["no_smooth"]),
        "SettleMean": r(mean(settle), 3), "SettleP95": r(pct(settle, 0.95), 3),
    }, curve


def analyse_condition(folder: Path, args):
    server = read_csv(folder / "server_samples.csv")
    tracks = defaultdict(list)
    for row in server:
        t, pid = f(row, "HostTime"), row.get("PlayerId")
        if t is not None and pid is not None:
            tracks[pid].append((t, vec(row, "Pos")))
    tracks = {pid: Track(s) for pid, s in tracks.items()}
    condition = (server[0].get("Condition") if server else None) or folder.name
    run_id = (server[0].get("RunId") if server else None) or folder.parent.name
    rows, curves = [], []
    for sample_file in sorted(folder.glob("client_*_samples.csv")):
        client = sample_file.name[len("client_"):-len("_samples.csv")]
        per_player = defaultdict(list)
        for row in read_csv(sample_file):
            if row.get("NetRole") != "Simulated":
                continue
            t = f(row, "HostTime")
            if t is None:
                continue
            per_player[row["PlayerId"]].append({
                "t": t, "visual": vec(row, "Visual"), "capsule": vec(row, "Capsule"),
                "mode": int(f(row, "SmoothingMode", -1)), "smooth_time": f(row, "SmoothLocationTime", -1.0)})
        corr_by_player = defaultdict(list)
        for row in read_csv(folder / f"client_{client}_corrections.csv"):
            corr_by_player[row["PlayerId"]].append({
                "t": f(row, "HostTime", 0.0), "dist": f(row, "Distance", 0.0), "no_smooth": row.get("NoSmooth") == "1"})
        for pid, samples in per_player.items():
            track = tracks.get(pid)
            if track is None or len(samples) < 10:
                continue
            metrics, curve = analyse_player(samples, track, corr_by_player.get(pid, []), args)
            ident = {"RunId": run_id, "Condition": condition, "ClientId": client, "PlayerId": pid}
            rows.append({**ident, **metrics})
            curves += [{**ident, "ShiftSec": round(d, 3), "MeanGap": round(e, 3)} for d, e in curve]
    return rows, curves


def write_csv(path: Path, rows):
    if not rows:
        return
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, list(dict.fromkeys(k for row in rows for k in row)))
        writer.writeheader()
        writer.writerows(rows)


def markdown(rows):
    cols = [("Condition", "조건"), ("ClientId", "클라"), ("SmoothingMode", "스무딩"), ("SmoothLocationTime", "시간"),
            ("GapMean", "차이 평균"), ("BestShiftSec", "추정 지연"), ("GapAfterShift", "당긴 뒤"),
            ("PopPerMin", "튐/분"), ("SpeedSpikes", "속도 튐"), ("JerkP95", "Jerk p95"),
            ("CorrDistP95", "보정 p95"), ("NoSmooth", "순간이동"), ("SettleMean", "수렴(s)")]
    lines = ["| " + " | ".join(c[1] for c in cols) + " |", "|" + "---|" * len(cols)]
    for row in sorted(rows, key=lambda x: (x["Condition"], x["ClientId"], str(x["SmoothingMode"]))):
        lines.append("| " + " | ".join("" if row.get(k) is None else str(row.get(k)) for k, _ in cols) + " |")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("roots", nargs="+", type=Path, help="RunId or Condition folders")
    ap.add_argument("--out", type=Path, help="output folder (default Saved/Tasks/<id>-rubberband-metrics)")
    ap.add_argument("--pop-cm", type=float, default=8.0)
    ap.add_argument("--max-speed", type=float, default=750.0)
    ap.add_argument("--settle-cm", type=float, default=1.0)
    ap.add_argument("--settle-max", type=float, default=2.0)
    ap.add_argument("--max-shift", type=float, default=0.6)
    ap.add_argument("--shift-step", type=float, default=0.01)
    args = ap.parse_args()

    folders = sorted({p.parent for root in args.roots for p in recording_path(root).rglob("server_samples.csv")})
    if not folders:
        raise SystemExit("server_samples.csv not found under the given roots")
    out = args.out or Path(__file__).resolve().parents[2] / "Saved" / "Tasks" / task_id("rubberband-metrics")
    describe_task(out, "Rubberband B 지표", "원격 캐릭터 화면 움직임의 튐·끌려감·서버 차이를 조건별로 비교",
                  f"rubberband_metrics.py roots={[str(r) for r in args.roots]} pop_cm={args.pop_cm} max_speed={args.max_speed}",
                  ["summary.csv: 조건 x 클라 x 플레이어별 지표", "shift_curve.csv: 비교 시점을 당긴 거리별 평균 차이",
                   "report.md: 요약 표"])
    rows, curves = [], []
    for folder in folders:
        a, b = analyse_condition(folder, args)
        rows += a
        curves += b
    write_csv(out / "summary.csv", rows)
    write_csv(out / "shift_curve.csv", curves)
    (out / "report.md").write_text("# Rubberband B 지표\n\n" + markdown(rows) + "\n", encoding="utf-8")
    print(markdown(rows))
    print(f"\n-> {out}")


if __name__ == "__main__":
    main()
