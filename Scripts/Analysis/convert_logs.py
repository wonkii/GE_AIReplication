#!/usr/bin/env python3
"""SharpEnemyCharacter(PR #3) 관찰 로그를 분석용 CSV로 변환한다.

Recorder가 생기기 전까지 쓰는 임시 연결용 도구다. 서버·클라이언트 로그에서 아래 줄을 읽는다.
    서버      [M2][CMC]            0.5초마다, 큰 속도 변화가 있으면 추가로 → server_samples.csv
    클라이언트 [M3][ClientVisual]   0.5초마다                          → client_<N>_samples.csv
    클라이언트 [M3][BaselineSummary] 이동이 끝난 뒤 한 번                → baseline_summary.csv

로그가 0.5초 간격이라 직진 구간의 평균 오차 정도만 의미가 있다.
급변 순간의 최대 오차나 회복 시간을 재려면 매 틱 기록하는 Recorder가 필요하다.

사용 예:
    python Scripts/Analysis/convert_logs.py Saved/Experiments/<BatchId>
    python Scripts/Analysis/sharp_metrics.py Saved/Experiments/<BatchId>
"""
from __future__ import annotations

import argparse
import csv
import json
import re
import statistics
import sys
from pathlib import Path

MARKER = "converted_from_logs.txt"
LINE_RE = re.compile(r"^\[[^\]]*\]\[\s*\d+\](?P<cat>\w+):\s*(?:(?:Display|Warning|Error|Log|Verbose):\s*)?(?P<msg>.*)$")
VEC_AXIS_RE = re.compile(r"([XYZ])=(-?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)")
KV_RE = re.compile(r"(\w+)=(V\([^)]*\)|\S+)")

SERVER_HEADER = ["RunId", "TrialId", "Mode", "ActorId", "ServerTime", "PosX", "PosY", "PosZ",
                 "VelX", "VelY", "VelZ", "MovementMode", "LogReason"]
CLIENT_HEADER = ["RunId", "TrialId", "Mode", "ClientId", "ActorId", "ServerTime",
                 "CapsuleX", "CapsuleY", "CapsuleZ", "MeshX", "MeshY", "MeshZ", "UpdateCount"]


def parse_vec(text: str) -> tuple[float, float, float] | None:
    """FVector::ToCompactString 형식 'V(X=1.00, Z=2.00)' 또는 'V(0)'을 읽는다. 생략된 축은 0이다."""
    text = text.strip()
    if not text.startswith("V("):
        return None
    axes = {"X": 0.0, "Y": 0.0, "Z": 0.0}
    for axis, value in VEC_AXIS_RE.findall(text):
        axes[axis] = float(value)
    return axes["X"], axes["Y"], axes["Z"]


def parse_fields(msg: str) -> dict[str, str]:
    return {k: v for k, v in KV_RE.findall(msg)}


def iter_tagged(path: Path, tag: str):
    """로그에서 tag로 시작하는 메시지의 필드를 차례로 돌려준다."""
    with path.open(encoding="utf-8-sig", errors="replace") as f:
        for line in f:
            if tag not in line:
                continue
            m = LINE_RE.match(line.rstrip("\r\n"))
            msg = m.group("msg") if m else line
            if not msg.startswith(tag):
                idx = msg.find(tag)
                if idx < 0:
                    continue
                msg = msg[idx:]
            yield parse_fields(msg[len(tag):])


def actor_key(fields: dict[str, str]) -> str:
    """ExperimentActorId가 지정돼 있으면 그 값을, 0이면 Actor 이름을 쓴다."""
    actor_id = fields.get("ActorId", "0")
    return actor_id if actor_id not in ("", "0") else fields.get("Actor", "unknown")


def fmt(x: float) -> str:
    return f"{x:.4f}"


def convert_trial(trial: Path, force: bool) -> str:
    server_log = trial / "server.log"
    if not server_log.exists():
        return "건너뜀: server.log 없음"
    if (trial / "server_samples.csv").exists() and not (trial / MARKER).exists() and not force:
        return "건너뜀: Recorder CSV가 이미 있음 (--force로 덮어쓰기)"

    meta = {}
    if (trial / "run_meta.json").exists():
        meta = json.loads((trial / "run_meta.json").read_text(encoding="utf-8-sig"))
    run_id, trial_id, mode = str(meta.get("RunId", trial.parent.name)), str(meta.get("TrialId", "")), str(meta.get("Mode", ""))

    server_rows = []
    for f in iter_tagged(server_log, "[M2][CMC]"):
        if "ServerTime" not in f:
            continue  # ObserverBound 같은 안내 줄
        pos, vel = parse_vec(f.get("PostPosition", "")), parse_vec(f.get("PostVelocity", ""))
        if pos is None or vel is None:
            continue
        server_rows.append([run_id, trial_id, mode, actor_key(f), f["ServerTime"],
                            *map(fmt, pos), *map(fmt, vel), f.get("MovementMode", ""), f.get("Reason", "")])

    client_counts, summaries = {}, []
    for log in sorted(trial.glob("client_*.log")):
        client_id = log.stem.split("_", 1)[1]
        raw = []
        for f in iter_tagged(log, "[M3][ClientVisual]"):
            cap, mesh = parse_vec(f.get("CapsulePosition", "")), parse_vec(f.get("MeshPosition", ""))
            offset = parse_vec(f.get("CapsuleToMesh", ""))
            if cap is None or mesh is None or offset is None or "ServerTime" not in f:
                continue
            raw.append((actor_key(f), f["ServerTime"], cap, mesh, offset, f.get("ReceiveCount", "")))
        # 화면 위치 = Mesh 위치 - (Mesh의 평소 오프셋). 평소 오프셋은 실행 전체의 중앙값으로 추정한다.
        # smoothing 중에는 오프셋이 잠깐 바뀌므로, 그 차이가 곧 Capsule 대비 화면상 어긋남이다.
        base = {}
        for actor in {r[0] for r in raw}:
            offs = [r[4] for r in raw if r[0] == actor]
            base[actor] = tuple(statistics.median(o[i] for o in offs) for i in range(3))
        rows = []
        for actor, t, cap, mesh, _offset, count in raw:
            b = base[actor]
            visual = (mesh[0] - b[0], mesh[1] - b[1], mesh[2] - b[2])
            rows.append([run_id, trial_id, mode, client_id, actor, t, *map(fmt, cap), *map(fmt, visual), count])
        write_csv(trial / f"client_{client_id}_samples.csv", CLIENT_HEADER, rows)
        client_counts[client_id] = len(rows)

        for f in iter_tagged(log, "[M3][BaselineSummary]"):
            summaries.append({"ClientId": client_id, **{k: v for k, v in f.items() if k != "World"}})

    write_csv(trial / "server_samples.csv", SERVER_HEADER, server_rows)
    if summaries:
        keys = list(dict.fromkeys(k for s in summaries for k in s))
        write_csv(trial / "baseline_summary.csv", keys, [[s.get(k, "") for k in keys] for s in summaries])
    (trial / MARKER).write_text(
        "이 폴더의 CSV는 convert_logs.py가 로그에서 만든 것이다 (0.5초 간격 샘플).\n", encoding="utf-8")
    counts = ", ".join(f"클라이언트 {k} {v}줄" for k, v in client_counts.items())
    return f"서버 {len(server_rows)}줄, {counts or '클라이언트 로그 없음'}, 요약 {len(summaries)}건"


def write_csv(path: Path, header: list[str], rows: list[list]):
    with path.open("w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("roots", nargs="+", type=Path, help="Trial 폴더 또는 그 상위 폴더")
    ap.add_argument("--force", action="store_true", help="Recorder가 만든 CSV가 있어도 덮어쓰기")
    args = ap.parse_args(argv)

    trials = sorted({p.parent for root in args.roots for p in root.rglob("server.log")}
                    | {root for root in args.roots if (root / "server.log").exists()})
    if not trials:
        print("server.log가 있는 폴더를 찾지 못함", file=sys.stderr)
        return 1
    for trial in trials:
        print(f"{trial}: {convert_trial(trial, args.force)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
