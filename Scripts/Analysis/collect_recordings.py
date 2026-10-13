#!/usr/bin/env python3
"""Collect independent per-Enemy native recordings into the existing analysis CSV schema.

Wall clock alignment is for processes on the same host (or synchronized hosts).
Use --clock server for separate hosts; GameState clock estimation can affect error metrics.
"""
from __future__ import annotations

import argparse
import bisect
import csv
import hashlib
import json
from collections import defaultdict
from pathlib import Path
from recording_paths import recording_path


def read_rows(path):
    if not path.exists():
        return []
    with path.open(encoding="utf-8-sig", newline="") as source:
        return list(csv.DictReader(source))


def write_rows(path, rows):
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fields = list(dict.fromkeys(key for row in rows for key in row))
    with path.open("w", encoding="utf-8", newline="") as target:
        writer = csv.DictWriter(target, fields)
        writer.writeheader()
        writer.writerows(rows)


def numeric(row, field, default=-1.0):
    try:
        return float(row.get(field, default))
    except (ValueError, TypeError):
        return default


def collect(roots: list[Path], output: Path, clock="wall") -> list[Path]:
    roots = [recording_path(root) for root in roots]
    output = recording_path(output)
    manifests = {}
    for root in roots:
        for path in root.rglob("manifest.json"):
            if output.resolve() in path.resolve().parents:
                continue
            meta = json.loads(path.read_text(encoding="utf-8-sig"))
            if meta.get("SchemaVersion") == 2 and "Generation" in meta:
                manifests[path.resolve()] = meta
    groups = defaultdict(list)
    for path, meta in manifests.items():
        groups[str(meta["RunId"]), str(meta["TrialId"])].append((path, meta))
    collected = []
    for (run, trial), records in sorted(groups.items()):
        server_records = [(p, m) for p, m in records if m["Role"].startswith("Server_")]
        if not server_records:
            continue
        if len({meta["Role"] for _, meta in server_records}) != 1:
            raise ValueError(f"Multiple server worlds use {run}/T{trial}; collect each run separately")
        if len({meta["Mode"] for _, meta in records}) != 1:
            raise ValueError(f"Modes disagree in {run}/T{trial}; the recording identity is inconsistent")
        servers = [row for p, _ in server_records for row in read_rows(p.parent / "server_samples.csv")]
        if not servers:
            continue
        # Multiple actors in the same server/world are expected; multiple servers for the same key are ambiguous.
        seen = set()
        for _, meta in server_records:
            key = (meta["ActorId"], meta["Generation"], meta["Segment"])
            if key in seen:
                raise ValueError(f"Duplicate server execution {run}/T{trial}/{key}; choose one recording root")
            seen.add(key)
        origin = min(numeric(row, "WallTime") for row in servers)
        # Map native server timestamps to the common host wall clock for receipt-state timestamps.
        time_map = sorted({(numeric(row, "ServerTime"), numeric(row, "WallTime") - origin) for row in servers})
        native_times = [value[0] for value in time_map]

        def server_to_common(value):
            if value < 0 or clock == "server":
                return value
            index = bisect.bisect_left(native_times, value)
            if index == 0:
                return time_map[0][1] + value - time_map[0][0]
            if index == len(time_map):
                return time_map[-1][1] + value - time_map[-1][0]
            left, right = time_map[index - 1], time_map[index]
            alpha = (value - left[0]) / (right[0] - left[0]) if right[0] > left[0] else 0
            return left[1] + alpha * (right[1] - left[1])

        def normalize(row, is_client=False):
            row = dict(row)
            row["NativeServerTime"] = row.get("ServerTime", "")
            if clock == "wall":
                row["ServerTime"] = f"{numeric(row, 'WallTime') - origin:.9f}"
            for field in ("ImpulseCallTime", "CallCompleteTime", "AppliedTime", "FirstSampleTime", "ActualVelocityChangeTime", "ObservedTime", "RxStateTime"):
                if field in row:
                    row[field] = f"{server_to_common(numeric(row, field)):.9f}"
            # SourceTime in client diagnostics is a native server timestamp; server diagnostics use world time too.
            if "SourceTime" in row:
                row["SourceTime"] = f"{server_to_common(numeric(row, 'SourceTime')):.9f}"
            return row

        digest = hashlib.sha256(run.encode()).hexdigest()[:12]
        target = output / digest / f"T{trial}"
        target.mkdir(parents=True, exist_ok=True)
        write_rows(target / "server_samples.csv", [normalize(row) for row in servers])
        for name in ("ground_truth.csv", "detector.csv", "detector_samples.csv"):
            rows = [normalize(row) for path, _ in server_records for row in read_rows(path.parent / name)]
            write_rows(target / name, rows)
        diagnostics = []
        client_rows = defaultdict(list)
        for path, meta in records:
            for row in read_rows(path.parent / "diagnostics.csv"):
                row["Role"] = meta["Role"]
                diagnostics.append(normalize(row, meta["Role"].startswith("Client_")))
            if not meta["Role"].startswith("Client_"):
                continue
            for sample_file in path.parent.glob("client_*_samples.csv"):
                for row in read_rows(sample_file):
                    # The role includes world/PID: two clients with the same default ID must remain distinct.
                    client_rows[meta["Role"]].append(normalize(row, True))
        for index, (role, rows) in enumerate(sorted(client_rows.items()), 1):
            for row in rows:
                row["OriginalClientId"] = row.get("ClientId", "0")
                row["ClientId"] = role
            write_rows(target / f"client_{index}_samples.csv", rows)
        write_rows(target / "diagnostics.csv", diagnostics)
        connection_paths = set()
        for path, _ in records:
            # manifest's parent structure is role/segment/generation/actor/trial.
            trial_dir = path.parent.parents[3]
            connection_paths.update(trial_dir.glob("_connections/**/connections.csv"))
        connections = []
        for path in sorted(connection_paths):
            for row in read_rows(path):
                row["SourcePart"] = path.parent.name
                connections.append(normalize(row))
        write_rows(target / "connections.csv", connections)
        # Copy external runner conditions when they exist above Native/.
        runner_meta = {}
        for parent in server_records[0][0].parents:
            candidate = parent / "run_meta.json"
            if candidate.exists():
                runner_meta = json.loads(candidate.read_text(encoding="utf-8-sig"))
                break
        profiles = {}
        native_conditions = {}
        original_profiles = runner_meta.get("Clients", {})
        for role, rows in client_rows.items():
            original_id = rows[0].get("OriginalClientId", "0") if rows else "0"
            profiles[role] = original_profiles.get(original_id, {})
        for _, meta in records:
            if meta["Role"].startswith("Client_"):
                native_conditions[meta["Role"]] = {"Emulation": meta.get("NetworkEmulation"),
                                                  "MaxFPS": meta.get("NetworkCVars", {}).get("t.MaxFPS")}
        scenarios = []
        for _, meta in server_records:
            scenario = {key: meta.get(key) for key in ("ActorId", "PathId", "PathRevision", "PathGeometry", "PathTransform", "Follow", "ImpulseVolumes", "Settings", "PreparedNetUpdateFrequency", "PreparedMinNetUpdateFrequency")}
            cmc = dict(meta.get("CharacterMovement", {}))
            for key in ("NetUpdateFrequency", "MinNetUpdateFrequency", "NetworkSmoothingMode"):
                cmc.pop(key, None)
            scenario["CharacterMovement"] = cmc
            scenario["ServerNetworkEmulation"] = meta.get("NetworkEmulation")
            scenario["ServerMaxFPS"] = meta.get("NetworkCVars", {}).get("t.MaxFPS")
            scenario["Map"] = meta.get("Map")
            scenario["Engine"] = meta.get("Engine")
            scenarios.append(scenario)
        scenarios.sort(key=lambda entry: (entry["ActorId"], str(entry["PathId"])))
        # Remove repetition-specific duplicates of the same scenario.
        scenario_text = json.dumps(sorted({json.dumps(entry, sort_keys=True) for entry in scenarios}), sort_keys=True)
        run_meta = dict(runner_meta)
        client_conditions = sorted(json.dumps({"Runner": info, "Native": native_conditions.get(role)}, sort_keys=True)
                                   for role, info in profiles.items())
        scenario_text += json.dumps(client_conditions)
        run_meta.update({"RunId": run, "TrialId": trial, "Mode": server_records[0][1]["Mode"], "Clients": profiles,
                         "NativeRecorder": True, "Clock": clock,
                         "ComparisonKey": hashlib.sha256(scenario_text.encode()).hexdigest(),
                         "Valid": all(m.get("Healthy", False) and str(m.get("State", "")).endswith("Completed") for _, m in records)
                         and bool(client_rows) and runner_meta.get("Valid", True),
                         "Manifests": [meta for _, meta in records]})
        (target / "run_meta.json").write_text(json.dumps(run_meta, ensure_ascii=False, indent=2), encoding="utf-8")
        collected.append(target)
    return collected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("roots", nargs="+", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--clock", choices=("wall", "server"), default="wall")
    args = parser.parse_args()
    result = collect(args.roots, args.out, args.clock)
    print(f"Collected {len(result)} trials into {args.out}")
    return 0 if result else 1


if __name__ == "__main__":
    raise SystemExit(main())
