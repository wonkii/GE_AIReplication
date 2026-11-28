#!/usr/bin/env python3
"""Independent local-player baseline summary. Standard library only.
No remote metrics are imported. Raw target distance is NOT treated as a screen jump.
Frame deltas are sampled movement including ordinary locomotion/camera motion.
"""
import argparse
import csv
import math
from pathlib import Path


def rows(path):
    with path.open(encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f))


def keyvalues(path):
    return dict(line.split("=", 1) for line in path.read_text(encoding="utf-8-sig").splitlines() if "=" in line)


def vec(row, prefix):
    return tuple(float(row[prefix + axis]) for axis in "XYZ")


def quantile(values, q=.95):
    if not values:
        return ""
    seq = sorted(values)
    return round(seq[math.ceil(q * len(seq))-1], 4)


def summarize(folder):
    meta = keyvalues(folder / "condition.txt")
    local_status = keyvalues(folder / "local_status.txt")
    server_status = keyvalues(folder / "server_status.txt")
    samples = rows(folder / "local_samples.csv")
    server = rows(folder / "server_samples.csv")
    corrections = rows(folder / "local_corrections.csv")
    replays = rows(folder / "local_replays.csv")
    script = rows(folder / "local_script.csv")
    complete = all(s.get("Reason") == "Completed" and s.get("WriterHealthy") == "1" for s in (local_status, server_status))
    # Treat truncated/missing data or aborted input scripts as incomplete.
    complete = complete and len(samples) > 1 and len(server) > 1 and any(r["Event"] == "ScriptStop:Completed" for r in script)
    complete = complete and int(local_status["CorrectionCallbacks"]) == len(corrections)
    complete = complete and all(r["PlayerId"] == meta["PlayerId"] for r in samples + server)
    shifts = [float(r["NetShiftCm"]) for r in replays]
    if not all(math.isfinite(x) for x in shifts):
        raise ValueError("Nonfinite replay shift")
    ids = [int(r["EventId"]) for r in corrections]
    grouped = {i for r in replays for i in range(int(r["FirstEvent"]), int(r["LastEvent"]) + 1)}
    out = {"Run": str(folder), **{k: meta.get(k, "") for k in ("Condition", "Direction", "Loops", "SyntheticDesyncCm", "Variant")},
           "Complete": int(complete), "Samples": len(samples), "ServerSamples": len(server),
           "CorrectionCallbacks": len(corrections), "ReplayGroups": len(replays), "UnpairedCallbacks": len(set(ids)-grouped),
           "ReplayShiftP95Cm": quantile(shifts), "ReplayShiftMaxCm": max(shifts, default=""),
           "SyntheticInjectionEvents": sum(r["Event"].startswith("SyntheticDesync:") for r in script)}
    # Use adjacent distinct-frame samples only; don't span invalid camera rows or gaps.
    # Mark these as total sampled frame step, not exclusively rubber-band displacement.
    ordered = sorted(samples, key=lambda r: float(r["HostTime"]))
    for prefix in ("Capsule", "Visual", "Camera"):
        ds = []
        for a, b in zip(ordered, ordered[1:]):
            dt = float(b["HostTime"]) - float(a["HostTime"])
            if int(b["Frame"]) - int(a["Frame"]) != 1 or not 0 < dt <= .25:
                continue
            if prefix == "Camera" and (a["CameraValid"] != "1" or b["CameraValid"] != "1"):
                continue
            d = math.dist(vec(a, prefix), vec(b, prefix))
            if math.isfinite(d):
                ds.append(d)
        out[prefix+"FrameStepP95Cm"] = quantile(ds)
        out[prefix+"FrameStepMaxCm"] = max(ds, default="")
        out[prefix+"FramePairs"] = len(ds)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--output", type=Path, default=Path("local_rubberband_summary.csv"))
    args = parser.parse_args()
    root = args.root.resolve()
    if not root.is_dir():
        parser.error(f"Not a directory: {root}")
    found = sorted(root.rglob("condition.txt"))
    if not found:
        parser.error("No local baseline condition.txt found")
    results = []
    for path in found:
        try:
            results.append(summarize(path.parent))
        except (OSError, ValueError, KeyError, OverflowError) as exc:
            print(f"INCOMPLETE {path.parent}: {exc}")
            results.append({"Run": str(path.parent), "Complete": 0, "Error": str(exc)})
    keys = list(dict.fromkeys(k for row in results for k in row))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8-sig", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=keys)
        writer.writeheader()
        writer.writerows(results)
    print(f"{len(results)} runs; {sum(r.get('Complete', 0) for r in results)} complete; {args.output}")


if __name__ == "__main__":
    main()
