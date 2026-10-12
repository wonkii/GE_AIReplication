#!/usr/bin/env python3
"""Sharp Movement 실험 CSV 분석 (M3 Metrics 오프라인 구현, M4 탐지 평가, M8 모드 비교).

Recorder가 남긴 CSV를 읽어 서버 궤적 대비 클라이언트 Capsule·Mesh 위치 오차를 계산한다.
표준 라이브러리만 사용한다. CSV 형식은 같은 폴더의 README.md 참고 (임시안).

사용 예:
    python sharp_metrics.py Saved/Experiments
    python sharp_metrics.py Saved/Experiments/RunA Saved/Experiments/RunB --out results
"""
from __future__ import annotations

import argparse
import bisect
import csv
import json
import math
import statistics
import sys
from dataclasses import dataclass, field
from pathlib import Path

SERVER_FILE = "server_samples.csv"
CLIENT_GLOB = "client_*_samples.csv"
GROUND_TRUTH_FILE = "ground_truth.csv"
DETECTOR_FILE = "detector.csv"
META_FILE = "run_meta.json"


# --------------------------------------------------------------------------- 입력

def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as f:
        return [{k.strip(): (v or "").strip() for k, v in row.items() if k} for row in csv.DictReader(f)]


def fnum(row: dict[str, str], key: str, default: float | None = None) -> float | None:
    value = row.get(key, "")
    if value == "":
        return default
    try:
        return float(value)
    except ValueError:
        return default


def vec(row: dict[str, str], prefix: str) -> tuple[float, float, float]:
    return (fnum(row, prefix + "X", 0.0), fnum(row, prefix + "Y", 0.0), fnum(row, prefix + "Z", 0.0))


def dist(a, b) -> float:
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2)


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


@dataclass
class ServerTrack:
    t: list[float] = field(default_factory=list)
    p: list[tuple[float, float, float]] = field(default_factory=list)
    v: list[tuple[float, float, float]] = field(default_factory=list)

    def speed_at(self, time: float) -> float | None:
        """time 직전 서버 샘플의 속력 (cm/s). 속도 기록이 없으면 None."""
        if not self.v or not self.t or time < self.t[0] or time > self.t[-1]:
            return None
        i = max(0, bisect.bisect_right(self.t, time) - 1)
        vx, vy, vz = self.v[i]
        return math.sqrt(vx * vx + vy * vy + vz * vz)

    def at(self, time: float) -> tuple[float, float, float] | None:
        """서버 궤적을 time 시각으로 선형 보간한다. 범위를 벗어나면 None."""
        if not self.t or time < self.t[0] or time > self.t[-1]:
            return None
        i = bisect.bisect_left(self.t, time)
        if i < len(self.t) and self.t[i] == time:
            return self.p[i]
        t0, t1 = self.t[i - 1], self.t[i]
        a = (time - t0) / (t1 - t0) if t1 > t0 else 0.0
        p0, p1 = self.p[i - 1], self.p[i]
        return (p0[0] + (p1[0] - p0[0]) * a, p0[1] + (p1[1] - p0[1]) * a, p0[2] + (p1[2] - p0[2]) * a)


@dataclass
class ClientSeries:
    """한 클라이언트에서 본 한 Actor의 프레임별 샘플 (서버 궤적과 정렬 완료)."""
    t: list[float]
    err_cap: list[float]
    err_mesh: list[float]
    correction: list[float]  # 프레임 간 Mesh 이동 중 실제 서버 이동으로 설명되지 않는 양
    receipts: list[float]  # 이동 상태 수신 시각 (UpdateCount 증가 시점)
    receipt_states: list[float | None]  # 각 수신에 담긴 상태의 서버 시각 (RxStateTime 열이 있을 때만)
    update_total: float = 0.0  # UpdateCount 증가량 합계 (샘플 간격이 길어도 수신 횟수를 그대로 셈)
    t_first: float = 0.0  # --min-speed로 거르기 전 전체 구간
    t_last: float = 0.0
    cap: list = field(default_factory=list)  # 클라이언트 Capsule 위치 (시간 어긋남 진단용)
    frame_ms: list = field(default_factory=list)  # 클라이언트 프레임 시간 (FrameMs 열이 있을 때만)


@dataclass
class Trial:
    path: Path
    run_id: str
    trial_id: str
    mode: str
    meta: dict
    server: dict[str, ServerTrack]
    clients: dict[str, dict[str, ClientSeries]]  # ClientId -> ActorId -> series
    ground_truth: list[dict[str, str]]
    detections: list[dict[str, str]]
    duration: float


def build_server(rows: list[dict[str, str]]) -> dict[str, ServerTrack]:
    tracks: dict[str, list[tuple[float, tuple]]] = {}
    for r in rows:
        t = fnum(r, "ServerTime")
        if t is None:
            continue
        tracks.setdefault(r.get("ActorId", "0"), []).append((t, vec(r, "Pos"), vec(r, "Vel")))
    out = {}
    for actor, samples in tracks.items():
        samples.sort(key=lambda s: s[0])
        dedup = []
        for s in samples:  # 같은 시각 중복 샘플은 마지막 값만 사용
            if dedup and dedup[-1][0] == s[0]:
                dedup[-1] = s
            else:
                dedup.append(s)
        out[actor] = ServerTrack([s[0] for s in dedup], [s[1] for s in dedup], [s[2] for s in dedup])
    return out


def build_client(rows: list[dict[str, str]], server: dict[str, ServerTrack],
                 min_speed: float = 0.0) -> dict[str, ClientSeries]:
    per_actor: dict[str, list[dict[str, str]]] = {}
    for r in rows:
        per_actor.setdefault(r.get("ActorId", "0"), []).append(r)
    out = {}
    for actor, samples in per_actor.items():
        track = server.get(actor)
        if track is None:
            continue
        samples.sort(key=lambda r: fnum(r, "ServerTime", 0.0))
        t, ec, em, corr, rec, rec_state = [], [], [], [], [], []
        prev_mesh = prev_srv = prev_count = None
        update_total, t_first, t_last = 0.0, None, None
        caps, frames = [], []
        for r in samples:
            time = fnum(r, "ServerTime")
            srv = track.at(time) if time is not None else None
            if srv is None:
                continue
            count = fnum(r, "UpdateCount")
            t_first = time if t_first is None else t_first
            t_last = time
            if prev_count is not None and count is not None and count > prev_count:
                update_total += count - prev_count
                rec.append(time)
                rec_state.append(fnum(r, "RxStateTime"))
            if count is not None:
                prev_count = count
            if min_speed > 0:
                speed = track.speed_at(time)
                if speed is None or speed < min_speed:
                    prev_mesh = prev_srv = None  # 구간이 끊기면 보정 거리를 이어서 계산하지 않음
                    continue
            cap, mesh = vec(r, "Capsule"), vec(r, "Mesh")
            caps.append(cap)
            frame = fnum(r, "FrameMs")
            if frame is not None:
                frames.append(frame)
            t.append(time)
            ec.append(dist(cap, srv))
            em.append(dist(mesh, srv))
            if prev_mesh is None:
                corr.append(0.0)
            else:
                corr.append(dist(sub(mesh, prev_mesh), sub(srv, prev_srv)))
            prev_mesh, prev_srv = mesh, srv
        if t:
            out[actor] = ClientSeries(t, ec, em, corr, rec, rec_state, update_total, t_first, t_last, caps, frames)
    return out


def client_id_of(path: Path, rows: list[dict[str, str]]) -> str:
    if rows and rows[0].get("ClientId"):
        return rows[0]["ClientId"]
    stem = path.stem  # client_<id>_samples
    parts = stem.split("_")
    return parts[1] if len(parts) >= 3 else stem


def load_trial(path: Path, min_speed: float = 0.0) -> Trial:
    meta = {}
    if (path / META_FILE).exists():
        meta = json.loads((path / META_FILE).read_text(encoding="utf-8-sig"))
    server_rows = read_csv(path / SERVER_FILE)
    server = build_server(server_rows)
    first = server_rows[0] if server_rows else {}
    clients = {}
    for cpath in sorted(path.glob(CLIENT_GLOB)):
        rows = read_csv(cpath)
        clients[client_id_of(cpath, rows)] = build_client(rows, server, min_speed)
    gt = read_csv(path / GROUND_TRUTH_FILE) if (path / GROUND_TRUTH_FILE).exists() else []
    det = read_csv(path / DETECTOR_FILE) if (path / DETECTOR_FILE).exists() else []
    starts = [tr.t[0] for tr in server.values() if tr.t]
    ends = [tr.t[-1] for tr in server.values() if tr.t]
    return Trial(
        path=path,
        run_id=str(meta.get("RunId") or first.get("RunId") or path.parent.name),
        trial_id=str(meta.get("TrialId") or first.get("TrialId") or path.name),
        mode=str(meta.get("Mode") or first.get("Mode") or "Unknown"),
        meta=meta,
        server=server,
        clients=clients,
        ground_truth=gt,
        detections=det,
        duration=(max(ends) - min(starts)) if starts else 0.0,
    )


def find_trials(roots: list[Path]) -> list[Path]:
    found = set()
    for root in roots:
        if (root / SERVER_FILE).exists():
            found.add(root.resolve())
        for p in root.rglob(SERVER_FILE):
            found.add(p.parent.resolve())
    return sorted(found)


def net_profile(trial: Trial, client_id: str) -> str:
    clients = trial.meta.get("Clients") or {}
    info = clients.get(str(client_id)) or {}
    return str(info.get("Profile") or "Unknown")


# --------------------------------------------------------------------------- 지표

def segment(series: ClientSeries, values: list[float], start: float, end: float):
    """[start, end] 구간의 (시각, 값) 목록."""
    i = bisect.bisect_left(series.t, start)
    j = bisect.bisect_right(series.t, end)
    return series.t[i:j], values[i:j]


def area(ts: list[float], vs: list[float]) -> float:
    return sum((ts[k] - ts[k - 1]) * (vs[k] + vs[k - 1]) * 0.5 for k in range(1, len(ts)))


def time_above(ts: list[float], vs: list[float], threshold: float) -> float:
    total = 0.0
    for k in range(1, len(ts)):
        if vs[k - 1] > threshold:
            total += ts[k] - ts[k - 1]
    return total


def time_mean(ts: list[float], vs: list[float]) -> float | None:
    if not vs:
        return None
    span = ts[-1] - ts[0] if len(ts) > 1 else 0.0
    return area(ts, vs) / span if span > 0 else vs[0]


def percentile(values: list[float], q: float) -> float | None:
    if not values:
        return None
    s = sorted(values)
    pos = (len(s) - 1) * q
    lo, hi = math.floor(pos), math.ceil(pos)
    return s[lo] + (s[hi] - s[lo]) * (pos - lo)


def first_arrival(series: ClientSeries, t0: float) -> tuple[float | None, bool]:
    """사건 이후 상태가 처음 도착한 클라이언트 시각.

    RxStateTime 열이 있으면 '담긴 상태의 서버 시각 >= t0'인 첫 수신을 쓴다 (정확).
    없으면 t0 이후 첫 수신을 쓴다 (사건 전 상태일 수도 있음). 두 번째 값은 정확 여부.
    """
    exact = any(s is not None for s in series.receipt_states)
    for t, state_time in zip(series.receipts, series.receipt_states):
        if t < t0:
            continue
        if not exact or (state_time is not None and state_time >= t0 - 1e-6):
            return t, exact
    return None, exact


def recovery_time(ts, vs, t0, threshold, hold):
    """최대 오차 이후 오차가 threshold 미만으로 hold 초 이상 유지되기 시작한 시각 - t0."""
    if not vs:
        return None
    peak_idx = max(range(len(vs)), key=vs.__getitem__)
    if vs[peak_idx] < threshold:
        return 0.0
    k = peak_idx
    while k < len(vs):
        if vs[k] < threshold:
            start = ts[k]
            m = k
            while m < len(vs) and vs[m] < threshold:
                if ts[m] - start >= hold:
                    return start - t0
                m += 1
            k = m
        else:
            k += 1
    return None  # 구간 안에서 회복하지 못함


def event_time(row: dict[str, str]) -> float | None:
    for key in ("ActualVelocityChangeTime", "ImpulseCallTime", "VolumeOverlapTime"):
        value = fnum(row, key)
        if value is not None:
            return value
    return None


def is_sharp(row: dict[str, str]) -> bool:
    return row.get("IsSharp", "1") not in ("0", "false", "False")


def r(x, nd=3):
    return None if x is None else round(x, nd)


def event_metrics(trial: Trial, args) -> list[dict]:
    rows = []
    events = [e for e in trial.ground_truth if event_time(e) is not None]
    by_actor: dict[str, list[dict]] = {}
    for e in events:
        by_actor.setdefault(e.get("ActorId", "0"), []).append(e)
    for actor_events in by_actor.values():
        actor_events.sort(key=event_time)

    for client_id, actors in trial.clients.items():
        for actor, series in actors.items():
            evs = by_actor.get(actor, [])
            for idx, e in enumerate(evs):
                t0 = event_time(e)
                end = t0 + args.window
                truncated = False
                if idx + 1 < len(evs) and event_time(evs[idx + 1]) < end:
                    end, truncated = event_time(evs[idx + 1]), True
                ts, cap = segment(series, series.err_cap, t0, end)
                _, mesh = segment(series, series.err_mesh, t0, end)
                _, corr = segment(series, series.correction, t0, end)
                if len(ts) < 2:
                    continue
                pre_ts, pre_cap = segment(series, series.err_cap, t0 - args.pre, t0)
                _, pre_mesh = segment(series, series.err_mesh, t0 - args.pre, t0)
                base_cap = time_mean(pre_ts, pre_cap) or 0.0
                base_mesh = time_mean(pre_ts, pre_mesh) or 0.0
                first, exact = first_arrival(series, t0)
                if first is not None and first > end:
                    first = None
                first_delay = (first - t0) if first is not None else None

                def split(values):
                    if first_delay is None:
                        return values, []
                    cut = bisect.bisect_left(ts, first)
                    return values[:cut], values[cut:]

                cap_b, cap_a = split(cap)
                mesh_b, mesh_a = split(mesh)
                ts_b, ts_a = split(ts)
                cap_peak_i = max(range(len(cap)), key=cap.__getitem__)
                rows.append({
                    "RunId": trial.run_id,
                    "TrialId": trial.trial_id,
                    "Mode": trial.mode,
                    "ClientId": client_id,
                    "NetProfile": net_profile(trial, client_id),
                    "ActorId": actor,
                    "EventId": e.get("EventId", str(idx)),
                    "Kind": e.get("Kind", ""),
                    "IsSharp": int(is_sharp(e)),
                    "EventTime": r(t0),
                    "WindowSec": r(end - t0),
                    "Truncated": int(truncated),
                    "FirstUpdateDelay": r(first_delay, 4),
                    "FirstUpdateExact": int(exact),
                    "UpdatesInWindow": sum(1 for t in series.receipts if t0 <= t <= end),
                    "PreMeanErr": r(base_cap),
                    "CapPeak": r(cap[cap_peak_i]),
                    "CapPeakAt": r(ts[cap_peak_i] - t0, 4),
                    "CapArea": r(area(ts, cap)),
                    "CapTimeAbove": r(time_above(ts, cap, args.error_threshold), 4),
                    "CapRecovery": r(recovery_time(ts, cap, t0, base_cap + args.recover_threshold, args.recover_hold), 4),
                    "MeshPeak": r(max(mesh)),
                    "MeshArea": r(area(ts, mesh)),
                    "MeshTimeAbove": r(time_above(ts, mesh, args.error_threshold), 4),
                    "MeshRecovery": r(recovery_time(ts, mesh, t0, base_mesh + args.recover_threshold, args.recover_hold), 4),
                    "CapPeakBeforeFirst": r(max(cap_b)) if cap_b else None,
                    "CapMeanBeforeFirst": r(time_mean(ts_b, cap_b)),
                    "CapPeakAfterFirst": r(max(cap_a)) if cap_a else None,
                    "CapMeanAfterFirst": r(time_mean(ts_a, cap_a)),
                    "MeshPeakBeforeFirst": r(max(mesh_b)) if mesh_b else None,
                    "MeshPeakAfterFirst": r(max(mesh_a)) if mesh_a else None,
                    "MaxCorrection": r(max(corr)),
                    "SnapCount": sum(1 for c in corr if c > args.snap_threshold),
                })
    return rows


def best_time_shift(track: ServerTrack, s: ClientSeries, max_shift=0.6, step=0.01):
    """클라이언트 위치가 서버 경로를 몇 초 늦게 따라가는지 추정한다.

    클라이언트 시각 t의 위치를 서버 시각 t - shift의 위치와 비교해서 평균 거리가 가장 작은 shift를 찾는다.
    shift가 지연(RTT/2 정도)보다 훨씬 크면 시계(GetServerWorldTimeSeconds) 어긋남이나 프레임 저하를 의심한다.
    shift를 빼고도 남는 오차가 크면 샘플 간격(보간) 문제나 실제 보정 오차다.
    """
    best = None
    n = int(round(max_shift / step))
    for k in range(-n, n + 1):
        shift = k * step
        total, count = 0.0, 0
        for time, cap in zip(s.t, s.cap):
            srv = track.at(time - shift)
            if srv is not None:
                total += dist(cap, srv)
                count += 1
        if count >= max(3, len(s.t) // 2):
            mean = total / count
            if best is None or mean < best[1]:
                best = (shift, mean)
    return best


def trial_metrics(trial: Trial, args) -> list[dict]:
    rows = []
    for client_id, actors in trial.clients.items():
        for actor, s in actors.items():
            span = s.t_last - s.t_first
            intervals = [b - a for a, b in zip(s.receipts, s.receipts[1:])]
            shift = best_time_shift(trial.server[actor], s) if s.cap else None
            rows.append({
                "RunId": trial.run_id,
                "TrialId": trial.trial_id,
                "Mode": trial.mode,
                "ClientId": client_id,
                "NetProfile": net_profile(trial, client_id),
                "ActorId": actor,
                "DurationSec": r(span),
                "Samples": len(s.t),
                "Updates": int(s.update_total),
                "UpdatesPerSec": r(s.update_total / span) if span > 0 else None,
                "MeanUpdateInterval": r(statistics.fmean(intervals), 4) if intervals else None,
                "MaxUpdateInterval": r(max(intervals), 4) if intervals else None,
                "CapMean": r(time_mean(s.t, s.err_cap)),
                "CapP95": r(percentile(s.err_cap, 0.95)),
                "CapMax": r(max(s.err_cap)),
                "MeshMean": r(time_mean(s.t, s.err_mesh)),
                "MeshP95": r(percentile(s.err_mesh, 0.95)),
                "MeshMax": r(max(s.err_mesh)),
                "SnapCount": sum(1 for c in s.correction if c > args.snap_threshold),
                "BestShiftSec": r(shift[0], 3) if shift else None,
                "CapMeanAfterShift": r(shift[1]) if shift else None,
                "AvgFrameMs": r(statistics.fmean(s.frame_ms), 1) if s.frame_ms else None,
                "MaxFrameMs": r(max(s.frame_ms), 1) if s.frame_ms else None,
            })
    return rows


def detector_metrics(trial: Trial, args) -> tuple[dict | None, list[dict]]:
    """Ground Truth(정답)와 Detector 진입 로그를 맞춰 탐지 지연·오탐·미탐을 계산한다 (M4 5번)."""
    if not trial.detections:
        return None, []
    dets = []
    for d in trial.detections:
        state = d.get("RiskState", "Enter")
        t = fnum(d, "ObservedTime")
        if t is not None and state in ("", "Enter"):
            dets.append({"actor": d.get("ActorId", "0"), "t": t, "reason": d.get("Reason", ""), "used": False})
    dets.sort(key=lambda d: d["t"])

    def ref_time(e):
        return fnum(e, "ImpulseCallTime") if fnum(e, "ImpulseCallTime") is not None else event_time(e)

    matches = []
    tp = fn = fp_control = 0
    events = sorted((e for e in trial.ground_truth if ref_time(e) is not None), key=ref_time)
    for e in events:
        t_ref = ref_time(e)
        hit = next((d for d in dets if not d["used"] and d["actor"] == e.get("ActorId", "0")
                    and t_ref - args.match_before <= d["t"] <= t_ref + args.match_window), None)
        if hit:
            hit["used"] = True
        sharp = is_sharp(e)
        if sharp and hit:
            tp += 1
        elif sharp:
            fn += 1
        elif hit:
            fp_control += 1
        actual = fnum(e, "ActualVelocityChangeTime")
        matches.append({
            "RunId": trial.run_id, "TrialId": trial.trial_id, "Mode": trial.mode,
            "ActorId": e.get("ActorId", "0"), "EventId": e.get("EventId", ""), "Kind": e.get("Kind", ""),
            "IsSharp": int(sharp),
            "Result": ("TP" if hit else "FN") if sharp else ("FP" if hit else "TN"),
            "LatencyFromImpulse": r(hit["t"] - t_ref, 4) if hit else None,
            "LatencyFromActual": r(hit["t"] - actual, 4) if hit and actual is not None else None,
        })
    fp_other = sum(1 for d in dets if not d["used"])
    fp = fp_control + fp_other
    latencies = [m["LatencyFromImpulse"] for m in matches if m["Result"] == "TP"]
    minutes = trial.duration / 60 if trial.duration > 0 else None
    summary = {
        "RunId": trial.run_id, "TrialId": trial.trial_id, "Mode": trial.mode,
        "SharpEvents": tp + fn, "TP": tp, "FN": fn, "FP": fp,
        "FP_OnControlEvent": fp_control, "FP_Other": fp_other,
        "Precision": r(tp / (tp + fp)) if tp + fp else None,
        "Recall": r(tp / (tp + fn)) if tp + fn else None,
        "FPPerMinute": r(fp / minutes) if minutes else None,
        "LatencyMean": r(statistics.fmean(latencies), 4) if latencies else None,
        "LatencyMax": r(max(latencies), 4) if latencies else None,
    }
    return summary, matches


# --------------------------------------------------------------------------- 집계

def stats(values: list) -> dict:
    v = [x for x in values if x is not None]
    if not v:
        return {"mean": None, "std": None, "max": None}
    return {
        "mean": r(statistics.fmean(v)),
        "std": r(statistics.stdev(v)) if len(v) > 1 else 0.0,
        "max": r(max(v)),
    }


SUMMARY_EVENT_KEYS = ["CapPeak", "MeshPeak", "CapArea", "CapTimeAbove", "CapRecovery", "MeshRecovery",
                      "FirstUpdateDelay", "CapPeakAfterFirst", "MaxCorrection", "SnapCount"]
SUMMARY_TRIAL_KEYS = ["UpdatesPerSec", "CapMean", "CapP95", "CapMax", "MeshMean", "MeshP95", "MeshMax",
                      "BestShiftSec", "CapMeanAfterShift", "AvgFrameMs", "MaxFrameMs"]


def summarize(event_rows: list[dict], trial_rows: list[dict]) -> list[dict]:
    groups: dict[tuple, dict] = {}
    for row in trial_rows:
        groups.setdefault((row["Mode"], row["NetProfile"]), {"trials": [], "events": []})["trials"].append(row)
    for row in event_rows:
        if row["IsSharp"]:
            groups.setdefault((row["Mode"], row["NetProfile"]), {"trials": [], "events": []})["events"].append(row)
    out = []
    for (mode, profile), g in sorted(groups.items()):
        row = {
            "Mode": mode,
            "NetProfile": profile,
            "Trials": len({(t["RunId"], t["TrialId"]) for t in g["trials"]}),
            "SharpEvents": len(g["events"]),
            "UnrecoveredRate": r(sum(1 for e in g["events"] if e["CapRecovery"] is None) / len(g["events"]))
            if g["events"] else None,
        }
        for key in SUMMARY_TRIAL_KEYS:
            for stat, value in stats([t[key] for t in g["trials"]]).items():
                row[f"{key}_{stat}"] = value
        for key in SUMMARY_EVENT_KEYS:
            for stat, value in stats([e[key] for e in g["events"]]).items():
                row[f"{key}_{stat}"] = value
        out.append(row)
    return out


def summarize_detector(rows: list[dict]) -> list[dict]:
    groups: dict[str, list[dict]] = {}
    for row in rows:
        groups.setdefault(row["Mode"], []).append(row)
    out = []
    for mode, g in sorted(groups.items()):
        tp, fn, fp = (sum(x[k] for x in g) for k in ("TP", "FN", "FP"))
        out.append({
            "Mode": mode, "Trials": len(g), "TP": tp, "FN": fn, "FP": fp,
            "Precision": r(tp / (tp + fp)) if tp + fp else None,
            "Recall": r(tp / (tp + fn)) if tp + fn else None,
            "LatencyMean": stats([x["LatencyMean"] for x in g])["mean"],
            "LatencyMax": stats([x["LatencyMax"] for x in g])["max"],
        })
    return out


# --------------------------------------------------------------------------- 출력

def write_csv(path: Path, rows: list[dict]):
    if not rows:
        return
    keys = list(rows[0].keys())
    for row in rows[1:]:
        keys += [k for k in row if k not in keys]
    with path.open("w", newline="", encoding="utf-8-sig") as f:  # BOM: 엑셀에서 한글 깨짐 방지
        w = csv.DictWriter(f, fieldnames=keys)
        w.writeheader()
        w.writerows(rows)


def fmt(x):
    return "-" if x is None else (f"{x:.3f}".rstrip("0").rstrip(".") if isinstance(x, float) else str(x))


def md_table(rows: list[dict], cols: list[tuple[str, str]]) -> str:
    lines = ["| " + " | ".join(h for _, h in cols) + " |", "|" + "---|" * len(cols)]
    for row in rows:
        lines.append("| " + " | ".join(fmt(row.get(k)) for k, _ in cols) + " |")
    return "\n".join(lines)


def write_report(path: Path, summary, det_summary, trials, args, warnings):
    lines = [
        "# Sharp Movement 분석 결과",
        "",
        f"- 분석한 Trial: {len(trials)}개",
        f"- 사건 구간: 사건 시각부터 {args.window}s (다음 사건이 먼저 오면 거기까지)",
        f"- 임계 오차 {args.error_threshold}cm 초과 시간 측정",
        f"- 회복: 오차가 사건 전 {args.pre}s 평균 + {args.recover_threshold}cm 미만으로 {args.recover_hold}s 유지되기 시작한 시점",
        f"- 스냅 기준: 한 프레임 보정 거리 {args.snap_threshold}cm 초과",
        "- 단위: cm, s. 오차는 같은 서버 시각으로 보간한 서버 위치와의 3D 거리",
    ]
    if args.min_speed > 0:
        lines.append(f"- 서버 속력 {args.min_speed}cm/s 이상인 샘플만 사용 (`--min-speed`)")
    lines += [
        "",
        "## 전체 구간 오차 (Trial 평균)",
        "",
        md_table(summary, [
            ("Mode", "모드"), ("NetProfile", "네트워크"), ("Trials", "Trial"),
            ("UpdatesPerSec_mean", "수신/s"), ("CapMean_mean", "Capsule 평균"), ("CapP95_mean", "Capsule p95"),
            ("CapMax_mean", "Capsule 최대"), ("MeshMean_mean", "Mesh 평균"), ("MeshP95_mean", "Mesh p95"),
            ("MeshMax_mean", "Mesh 최대"),
        ]),
        "",
        "## 시간 어긋남 진단",
        "",
        "클라이언트 위치가 서버 경로를 몇 초 늦게 따라가는지(추정 지연)와, 그만큼 당겨서 맞췄을 때 남는 오차다. "
        "추정 지연이 넣은 지연(N2면 약 0.05초)보다 훨씬 크면 시계 어긋남이나 프레임 저하를, "
        "당긴 뒤에도 오차가 크면 샘플 간격(보간) 문제나 실제 보정 오차를 의심한다.",
        "",
        md_table(summary, [
            ("Mode", "모드"), ("NetProfile", "네트워크"), ("CapMean_mean", "Capsule 평균"),
            ("BestShiftSec_mean", "추정 지연(s)"), ("CapMeanAfterShift_mean", "당긴 뒤 평균 오차"),
            ("AvgFrameMs_mean", "평균 프레임(ms)"), ("MaxFrameMs_max", "최대 프레임(ms)"),
        ]),
        "",
        "## 급변 사건 구간 (사건 평균)",
        "",
    ]
    if not any(row.get("SharpEvents") for row in summary):
        lines += ["급변 사건 기록(`ground_truth.csv`)이 없어 생략함.", ""]
    else:
        lines += [md_table(summary, [
            ("Mode", "모드"), ("NetProfile", "네트워크"), ("Trials", "Trial"), ("SharpEvents", "사건"),
            ("UpdatesPerSec_mean", "수신/s"), ("CapMean_mean", "평균 오차"),
            ("CapPeak_mean", "Capsule 최대"), ("MeshPeak_mean", "Mesh 최대"),
            ("CapRecovery_mean", "회복 시간"), ("FirstUpdateDelay_mean", "첫 수신"),
            ("CapPeakAfterFirst_mean", "첫 수신 후 최대"), ("MaxCorrection_mean", "최대 보정"),
            ("UnrecoveredRate", "미회복 비율"),
        ]), ""]
    lines += [
        "worst case와 표준편차는 `summary.csv`의 `_max`, `_std` 열 참고.",
        "",
    ]
    if det_summary:
        lines += [
            "## Detector 평가",
            "",
            md_table(det_summary, [
                ("Mode", "모드"), ("Trials", "Trial"), ("TP", "TP"), ("FN", "미탐"), ("FP", "오탐"),
                ("Precision", "Precision"), ("Recall", "Recall"),
                ("LatencyMean", "평균 지연"), ("LatencyMax", "최대 지연"),
            ]),
            "",
        ]
    lines += [
        "## 참고",
        "",
        "- 전송량(bytes/s)은 이 스크립트가 아니라 Networking Insights로 측정함. `수신/s`는 클라이언트가 받은 이동 상태 횟수",
        "- `첫 수신`은 사건 이후 상태가 클라이언트에 처음 도착하기까지의 시간. "
        "클라이언트 CSV에 `RxStateTime` 열이 없으면 사건 후 첫 수신으로 대신하며, 이때는 사건 전 상태일 수 있음 "
        "(`events.csv`의 `FirstUpdateExact`=0)",
        "- 지연이 있으면 Simulated Proxy는 평소에도 (속도 × 지연)만큼 뒤처짐. 그래서 회복은 0cm가 아니라 사건 전 평균 오차 기준으로 판정함",
    ]
    if warnings:
        lines += ["", "## 경고", ""] + [f"- {w}" for w in warnings]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("roots", nargs="+", type=Path, help="Trial 폴더 또는 그 상위 폴더 (하위를 모두 찾음)")
    ap.add_argument("--out", type=Path, help="결과 폴더 (기본: 첫 번째 입력 폴더/_analysis)")
    ap.add_argument("--window", type=float, default=1.0, help="사건 후 분석 구간 (s)")
    ap.add_argument("--pre", type=float, default=0.5, help="사건 전 기준 오차 구간 (s)")
    ap.add_argument("--error-threshold", type=float, default=50.0, help="임계 오차 (cm)")
    ap.add_argument("--recover-threshold", type=float, default=10.0, help="회복 판정 여유값, 사건 전 평균 오차에 더함 (cm)")
    ap.add_argument("--recover-hold", type=float, default=0.1, help="회복 판정 유지 시간 (s)")
    ap.add_argument("--snap-threshold", type=float, default=20.0, help="스냅 판정 프레임 보정 거리 (cm)")
    ap.add_argument("--match-window", type=float, default=0.3, help="Impulse 후 감지를 인정하는 최대 시간 (s)")
    ap.add_argument("--match-before", type=float, default=0.05, help="Impulse 직전 감지 허용 오차 (s)")
    ap.add_argument("--min-speed", type=float, default=0.0,
                    help="서버 속력이 이 값(cm/s) 이상인 샘플만 분석. 정지 구간을 빼고 이동 중 오차만 볼 때 사용")
    args = ap.parse_args(argv)

    paths = find_trials(args.roots)
    if not paths:
        print(f"{SERVER_FILE}가 있는 폴더를 찾지 못함: {', '.join(map(str, args.roots))}", file=sys.stderr)
        return 1
    out = args.out or (args.roots[0] / "_analysis")
    out.mkdir(parents=True, exist_ok=True)

    trials, event_rows, trial_rows, det_rows, det_matches, warnings = [], [], [], [], [], []
    for p in paths:
        if out.resolve() in p.parents:
            continue
        t = load_trial(p, args.min_speed)
        trials.append(t)
        if not t.clients:
            warnings.append(f"{p}: 클라이언트 CSV 없음")
        if not t.ground_truth:
            warnings.append(f"{p}: {GROUND_TRUTH_FILE} 없음, 사건별 지표 생략")
        if (p / "converted_from_logs.txt").exists():
            warnings.append(f"{p}: 로그에서 변환한 데이터 (0.5초 간격 샘플이라 최대 오차·회복 시간은 정확하지 않음)")
        if t.meta and not t.meta.get("Valid", True):
            warnings.append(f"{p}: run_meta.json에서 실행 검증 실패로 표시됨")
        event_rows += event_metrics(t, args)
        new_rows = trial_metrics(t, args)
        trial_rows += new_rows
        warned = set()
        for row in new_rows:
            if row["ClientId"] in warned:
                continue
            if row["AvgFrameMs"] and (row["AvgFrameMs"] > 25 or (row["MaxFrameMs"] or 0) > 200):
                warned.add(row["ClientId"])
                warnings.append(f"{p}: 클라이언트 {row['ClientId']} 프레임 저하 (평균 {row['AvgFrameMs']}ms, "
                                f"최대 {row['MaxFrameMs']}ms). 실행 중 창 클릭이나 컴퓨터 부하 확인")
        summary, matches = detector_metrics(t, args)
        if summary:
            det_rows.append(summary)
            det_matches += matches

    summary = summarize(event_rows, trial_rows)
    det_summary = summarize_detector(det_rows)
    write_csv(out / "events.csv", event_rows)
    write_csv(out / "trials.csv", trial_rows)
    write_csv(out / "summary.csv", summary)
    write_csv(out / "detector_trials.csv", det_rows)
    write_csv(out / "detector_events.csv", det_matches)
    write_report(out / "report.md", summary, det_summary, trials, args, warnings)
    print(f"Trial {len(trials)}개 분석 완료 → {out}")
    for w in warnings:
        print("경고:", w)
    return 0


if __name__ == "__main__":
    sys.exit(main())
