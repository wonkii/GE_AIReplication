#!/usr/bin/env python3
"""분석 스크립트 검증용 가짜 실험 데이터 생성기.

언리얼 없이 sharp_metrics.py를 돌려보기 위한 것이며, 결과 수치는 실제 실험값이 아니다.
README.md의 CSV 형식대로 server/client/ground_truth/detector CSV와 run_meta.json을 만든다.

단순화한 모델:
- 서버: 120Hz 틱, Enemy가 400cm/s 직선 이동, Impulse는 속도에 즉시 더함(bVelocityChange=true)
- 전송: 모드별 고정 빈도 (Default 10Hz, AlwaysHigh 60Hz, SingleShot 감지 시 1회 추가, Burst 감지 후 0.3s 60Hz)
- 클라이언트: 60fps, 받은 위치에서 받은 속도로 계속 이동(Simulated Proxy), Mesh는 지수 smoothing

사용 예:
    python make_synthetic_run.py --out _synthetic
    python sharp_metrics.py _synthetic
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import random
from pathlib import Path

G = 980.0
SERVER_HZ = 120
CLIENT_FPS = 60
BASE_SPEED = 400.0
DETECT_DV = 300.0
COOLDOWN = 0.3
PROFILES = {
    "N0": dict(LagMs=0, JitterMs=0, LossPct=0.0),
    "N2": dict(LagMs=50, JitterMs=10, LossPct=1.0),
}
SHARP_EVENTS = [  # (시각, 종류, Impulse). 전송 주기와 겹치지 않게 어긋난 시각 사용
    (4.03, "Side", (0.0, 700.0, 250.0)),
    (10.07, "Back", (-600.0, 0.0, 0.0)),
    (16.05, "Side", (0.0, -700.0, 0.0)),
    (22.02, "Forward", (700.0, 0.0, 0.0)),
]
GENTLE_TURN = (13.0, 0.3, 60.0)  # 정상 회전 대조군: 시작 시각, 걸리는 시간, 각도


def simulate_server(duration, actor_offset_y):
    dt = 1.0 / SERVER_HZ
    pos = [0.0, actor_offset_y, 0.0]
    heading = 0.0
    vel = [BASE_SPEED, 0.0, 0.0]
    samples, gt, detections = [], [], []
    pending = list(SHARP_EVENTS)
    queued = None  # AddImpulse는 호출 다음 이동 업데이트에서 반영됨
    prev_v = list(vel)
    last_detect = -1e9
    seq = 0
    n = int(duration * SERVER_HZ)
    for k in range(n + 1):
        t = k * dt
        turn_start, turn_len, turn_deg = GENTLE_TURN
        if turn_start <= t < turn_start + turn_len:
            heading += math.radians(turn_deg) * dt / turn_len
        base = (BASE_SPEED * math.cos(heading), BASE_SPEED * math.sin(heading))
        if queued:
            imp = queued
            vel = [vel[0] + imp[0], vel[1] + imp[1], vel[2] + imp[2]]
            queued = None
            apply_now = True
        else:
            apply_now = False
        if pending and t >= pending[0][0]:
            et, kind, imp = pending.pop(0)
            queued = imp
            gt.append(dict(t_call=t, kind=kind, imp=imp, sharp=1))
        # AI 경로 추종 + 마찰: 평소 속도로 되돌아감
        decay = math.exp(-dt / 0.35)
        vel[0] = base[0] + (vel[0] - base[0]) * decay
        vel[1] = base[1] + (vel[1] - base[1]) * decay
        if pos[2] > 0 or vel[2] > 0:
            vel[2] -= G * dt
        pos = [pos[0] + vel[0] * dt, pos[1] + vel[1] * dt, pos[2] + vel[2] * dt]
        if pos[2] <= 0:
            pos[2], vel[2] = 0.0, 0.0
        mode = "Falling" if pos[2] > 0 else "Walking"
        samples.append((t, tuple(pos), tuple(vel), mode))
        if apply_now:
            gt[-1]["t_actual"] = t  # 이동 업데이트 후 실제 속도 변화가 관측된 틱
        dv = math.dist(vel, prev_v)
        sp_now, sp_prev = math.hypot(vel[0], vel[1]), math.hypot(prev_v[0], prev_v[1])
        dtheta = 0.0
        if sp_now > 50 and sp_prev > 50:
            dtheta = math.degrees(abs(math.atan2(vel[1], vel[0]) - math.atan2(prev_v[1], prev_v[0])))
            dtheta = min(dtheta, 360 - dtheta)
        if dv > DETECT_DV and t - last_detect > COOLDOWN:
            seq += 1
            last_detect = t
            detections.append(dict(seq=seq, t=t, pos=tuple(pos), vel=tuple(vel), mode=mode, dv=dv,
                                   dtheta=dtheta, reason="DeltaV"))
        prev_v = list(vel)
    gt.append(dict(t_call=GENTLE_TURN[0], t_actual=GENTLE_TURN[0], kind="GentleTurn", imp=(0, 0, 0), sharp=0))
    return samples, gt, detections


def send_times(samples, detections, mode):
    hz_normal, hz_high, burst = 10.0, 60.0, 0.3
    det_times = [d["t"] for d in detections]
    sends, last = [], -1e9
    for i, (t, *_rest) in enumerate(samples):
        forced = mode == "SingleShot" and any(abs(t - dt_) < 1e-9 for dt_ in det_times)
        high = mode == "AlwaysHigh" or (mode == "Burst" and any(0 <= t - d < burst for d in det_times))
        hz = hz_high if high else hz_normal
        if forced or t - last >= 1.0 / hz - 1e-9:
            sends.append(i)
            last = t
    return sends


def simulate_client(samples, sends, profile, rng):
    lag, jitter, loss = profile["LagMs"] / 1000, profile["JitterMs"] / 1000, profile["LossPct"] / 100
    packets = []
    for i in sends:
        if rng.random() < loss:
            continue
        arrive = samples[i][0] + max(0.0, lag + rng.uniform(-jitter, jitter))
        packets.append((arrive, i))
    packets.sort()
    rows, pk = [], 0
    rx = None  # (수신 시각, 서버 샘플)
    newest_sent = -1
    offset = [0.0, 0.0, 0.0]
    count = 0
    capsule = None
    t_end = samples[-1][0]
    dt = 1.0 / CLIENT_FPS
    k = 0
    while k * dt <= t_end:
        t = k * dt
        k += 1
        while pk < len(packets) and packets[pk][0] <= t:
            _, i = packets[pk]
            pk += 1
            if i <= newest_sent:
                continue  # 늦게 온 옛 상태는 버림
            newest_sent = i
            old = capsule
            rx = (t, samples[i])
            count += 1
            new = samples[i][1]
            if old is not None:
                offset = [offset[j] + old[j] - new[j] for j in range(3)]
            capsule = new
        if rx is None:
            continue
        t_rx, (_, p, v, _m) = rx
        tau = t - t_rx
        z = p[2] + v[2] * tau - (0.5 * G * tau * tau if (p[2] > 0 or v[2] > 0) else 0.0)
        capsule = (p[0] + v[0] * tau, p[1] + v[1] * tau, max(0.0, z))
        decay = math.exp(-dt / 0.04)
        offset = [o * decay for o in offset]
        mesh = tuple(capsule[j] + offset[j] for j in range(3))
        rows.append((t, capsule, mesh, count, rx[1][0]))
    return rows


def write(path, header, rows):
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)


def make_trial(root, mode, trial, duration, seed):
    rng = random.Random(seed)
    run_id = f"SYN-{mode}"
    out = root / run_id / f"T{trial}"
    out.mkdir(parents=True, exist_ok=True)
    clients = {"1": dict(Profile="N0", **PROFILES["N0"]), "2": dict(Profile="N2", **PROFILES["N2"])}
    meta = dict(RunId=run_id, TrialId=trial, Mode=mode, Synthetic=True, MeasureSec=duration, Clients=clients,
                Valid=True)
    (out / "run_meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")

    server_rows, gt_rows, det_rows = [], [], []
    client_rows = {cid: [] for cid in clients}
    event_id = 0
    for actor, y in (("Enemy_0", 0.0), ("Enemy_1", 500.0)):
        samples, gt, detections = simulate_server(duration, y)
        for t, p, v, m in samples:
            server_rows.append([run_id, trial, mode, actor, f"{t:.5f}", *(f"{x:.3f}" for x in p),
                                *(f"{x:.3f}" for x in v), m])
        for e in gt:
            event_id += 1
            gt_rows.append([run_id, trial, event_id, actor, e["kind"], e["sharp"], f"{e['t_call']:.5f}",
                            f"{e['t_call']:.5f}", f"{e['t_actual']:.5f}", *e["imp"]])
        for d in detections:
            det_rows.append([run_id, trial, actor, d["seq"], f"{d['t']:.5f}", *(f"{x:.3f}" for x in d["pos"]),
                             *(f"{x:.3f}" for x in d["vel"]), d["mode"], f"{d['dv']:.2f}", f"{d['dtheta']:.2f}",
                             d["reason"], "Enter"])
        # 감지기 오탐 경로 확인용 잡음 1건
        noise_t = 7.0 + rng.random()
        det_rows.append([run_id, trial, actor, 999, f"{noise_t:.5f}", 0, 0, 0, 0, 0, 0, "Walking", 0, 0,
                         "SyntheticNoise", "Enter"])
        sends = send_times(samples, detections, mode)
        for cid, prof in clients.items():
            for t, cap, mesh, count, rx_t in simulate_client(samples, sends, prof, rng):
                client_rows[cid].append([run_id, trial, mode, cid, actor, f"{t:.5f}",
                                         *(f"{x:.3f}" for x in cap), *(f"{x:.3f}" for x in mesh), count,
                                         f"{rx_t:.5f}"])

    write(out / "server_samples.csv",
          ["RunId", "TrialId", "Mode", "ActorId", "ServerTime", "PosX", "PosY", "PosZ", "VelX", "VelY", "VelZ",
           "MovementMode"], server_rows)
    for cid, rows in client_rows.items():
        write(out / f"client_{cid}_samples.csv",
              ["RunId", "TrialId", "Mode", "ClientId", "ActorId", "ServerTime", "CapsuleX", "CapsuleY", "CapsuleZ",
               "MeshX", "MeshY", "MeshZ", "UpdateCount", "RxStateTime"], rows)
    write(out / "ground_truth.csv",
          ["RunId", "TrialId", "EventId", "ActorId", "Kind", "IsSharp", "VolumeOverlapTime", "ImpulseCallTime",
           "ActualVelocityChangeTime", "ImpulseX", "ImpulseY", "ImpulseZ"], gt_rows)
    write(out / "detector.csv",
          ["RunId", "TrialId", "ActorId", "SequenceId", "ObservedTime", "PosX", "PosY", "PosZ", "PostVelX",
           "PostVelY", "PostVelZ", "MovementMode", "DeltaV", "DeltaThetaDeg", "Reason", "RiskState"], det_rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=Path("_synthetic"))
    ap.add_argument("--modes", nargs="+", default=["Default", "SingleShot", "Burst", "AlwaysHigh"])
    ap.add_argument("--trials", type=int, default=3)
    ap.add_argument("--duration", type=float, default=26.0)
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()
    for mi, mode in enumerate(args.modes):
        for trial in range(1, args.trials + 1):
            make_trial(args.out, mode, trial, args.duration, args.seed * 1000 + mi * 100 + trial)
    print(f"가짜 데이터 생성: {args.out} ({len(args.modes)}개 모드 × {args.trials}회)")


if __name__ == "__main__":
    main()
