#!/usr/bin/env python3
"""Summarize manually recorded Networking Insights packet metrics, without inventing data.

Usage: python Tools/summarize_network_cost.py Tools/network_cost_inputs.csv
The input file may contain a header only; the tool then emits no measurement output.
"""

import csv
import math
import sys
from pathlib import Path

COLUMNS = [
    "RunId", "TrialId", "Mode", "ActorCount", "Connection", "EventSource",
    "LagMs", "LossPct", "WindowLabel", "WindowStartSec", "WindowEndSec",
    "PacketCount", "TracedPacketBits", "EnemyExclusiveBits", "SnapshotExclusiveBits",
    "Notes",
]
OUTPUT_METRICS = [
    "WindowDurationSec", "PacketsPerSec", "TracedPreCompressionBytesPerSec",
    "EnemyExclusiveBytesPerSec", "SnapshotExclusiveBytesPerSec",
]


def number(record, name, integer=False, optional=False):
    raw = record.get(name, "").strip()
    if optional and not raw:
        return None
    if not raw:
        raise ValueError(f"{name} is required")
    try:
        value = int(raw) if integer else float(raw)
    except ValueError as exc:
        raise ValueError(f"{name} is not numeric: {raw!r}") from exc
    if not math.isfinite(value) or value < 0:
        raise ValueError(f"{name} must be finite and >= 0")
    return value


def format_rate(total, duration, scale=1):
    return "" if total is None else f"{total / scale / duration:.3f}"


def summarize(source):
    with source.open("r", newline="", encoding="utf-8-sig") as infile:
        reader = csv.DictReader(infile)
        if reader.fieldnames is None or set(COLUMNS) - set(reader.fieldnames):
            raise ValueError("Missing required columns: " +
                             ", ".join(sorted(set(COLUMNS) - set(reader.fieldnames or []))))
        rows = list(reader)
    if not rows:
        print("No measurement rows: record real Networking Insights values first.")
        return None

    computed = []
    for index, row in enumerate(rows, start=2):
        try:
            begin = number(row, "WindowStartSec")
            end = number(row, "WindowEndSec")
            if end <= begin:
                raise ValueError("WindowEndSec must be greater than WindowStartSec")
            duration = end - begin
            packet_count = number(row, "PacketCount", integer=True, optional=True)
            traced = number(row, "TracedPacketBits", optional=True)
            enemy = number(row, "EnemyExclusiveBits", optional=True)
            snapshot = number(row, "SnapshotExclusiveBits", optional=True)
            number(row, "ActorCount", integer=True)
            number(row, "LagMs")
            loss = number(row, "LossPct")
            if loss > 100:
                raise ValueError("LossPct must be <= 100")
            if not row["RunId"].strip() or not row["Mode"].strip() or not row["Connection"].strip():
                raise ValueError("RunId, Mode, and Connection are required")
            if row["EventSource"].strip() not in ("None", "Synthetic", "RealDetector"):
                raise ValueError("EventSource must be None, Synthetic, or RealDetector")
        except ValueError as error:
            raise ValueError(f"CSV row {index}: {error}") from error

        computed.append({
            **row,
            "WindowDurationSec": f"{duration:.3f}",
            "PacketsPerSec": format_rate(packet_count, duration),
            "TracedPreCompressionBytesPerSec": format_rate(traced, duration, 8),
            "EnemyExclusiveBytesPerSec": format_rate(enemy, duration, 8),
            "SnapshotExclusiveBytesPerSec": format_rate(snapshot, duration, 8),
        })

    output = source.with_name("network_cost_summary.csv")
    with output.open("w", newline="", encoding="utf-8-sig") as outfile:
        writer = csv.DictWriter(outfile, fieldnames=COLUMNS + OUTPUT_METRICS)
        writer.writeheader()
        writer.writerows(computed)
    print(f"Saved {len(computed)} measured rows to {output}")
    print("Caution: Networking Insights sizes are pre-compression trace sizes, NOT wire bytes.")
    return output


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python summarize_network_cost.py network_cost_inputs.csv")
        sys.exit(2)
    try:
        summarize(Path(sys.argv[1]))
    except (ValueError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
