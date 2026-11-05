#!/usr/bin/env python3
"""Analyse HitWallProbe recordings (Saved/HitWallProbe).

Reads every hit_frames.csv / hit_events.csv below the given folder, computes one
row per reaction per peer, compares each client reaction with the matching server
reaction, groups reactions by wall distance (or by hitwall.Label), and states
whether each symptom was reproduced (>= 8 of 10 by default).

Standard library only.

    python Scripts/Analysis/hitwall_probe.py Saved/HitWallProbe
    python Scripts/Analysis/hitwall_probe.py Saved/HitWallProbe --group label --out Saved/HitWallProbe/report
"""
from __future__ import annotations

import argparse
import csv
import math
import statistics
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

# Display thresholds. These match the starting values in docs/RootMotion_피격_벽충돌_기준선/03
# (capsule rise 5 cm, large correction 20 cm). They flag, they do not prove.
RISE_CM = 5.0
PELVIS_PEN_CM = 5.0
CLIENT_ERR_CM = 20.0
JUMP_CM = 10.0          # one-frame mesh move that reads as a snap at 60 fps (= 600 cm/s)
REPRO_RATIO = 0.8

SERVER_ROLES = {"Standalone", "ListenServer", "DedicatedServer"}


def fnum(value: str, default: float = math.nan) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


@dataclass
class Reaction:
    key: tuple
    role: str
    actor: str
    reaction_id: int
    session: str
    folder: str
    start: dict = field(default_factory=dict)
    end: dict = field(default_factory=dict)
    frames: list = field(default_factory=list)
    metrics: dict = field(default_factory=dict)

    @property
    def is_server(self) -> bool:
        return self.role in SERVER_ROLES


def load(root: Path) -> list[Reaction]:
    reactions: dict[tuple, Reaction] = {}
    for events_path in sorted(root.rglob("hit_events.csv")):
        folder = str(events_path.parent)
        with events_path.open(newline="", encoding="utf-8") as f:
            for row in csv.DictReader(f):
                key = (folder, row["actor"], int(row["reaction_id"]))
                r = reactions.setdefault(key, Reaction(key, row["role"], row["actor"], int(row["reaction_id"]),
                                                       row["session"], folder))
                if row["event"] == "start" and not r.start:
                    r.start = row
                elif row["event"] == "end":
                    r.end = row
        frames_path = events_path.parent / "hit_frames.csv"
        if not frames_path.exists():
            continue
        with frames_path.open(newline="", encoding="utf-8") as f:
            for row in csv.DictReader(f):
                key = (folder, row["actor"], int(row["reaction_id"]))
                if key in reactions:
                    reactions[key].frames.append(row)
    return [r for r in reactions.values() if r.start and r.frames]


def compute(r: Reaction) -> None:
    s = r.start
    yaw = math.radians(fnum(s["facing_yaw"], 0.0))
    back = (-math.cos(yaw), -math.sin(yaw))
    x0, y0, z0 = fnum(s["x"]), fnum(s["y"]), fnum(s["z"])
    f0 = r.frames[0]
    bottom0 = fnum(f0["cap_bottom_z"])
    pelvis0 = fnum(f0["pelvis_z"])

    wall = None
    if fnum(s["sweep_dist"], -1) >= 0 and s["wall_x"]:
        wall = ((fnum(s["wall_x"]), fnum(s["wall_y"]), fnum(s["wall_z"])),
                (fnum(s["wall_nx"]), fnum(s["wall_ny"]), fnum(s["wall_nz"])))

    retreat, rise, pelvis_rise, pelvis_pen = [], [], [], []
    steps, modes = [], set()
    prev_mesh = None
    for fr in r.frames:
        cx, cy = fnum(fr["cap_x"]), fnum(fr["cap_y"])
        retreat.append((cx - x0) * back[0] + (cy - y0) * back[1])
        rise.append(fnum(fr["cap_bottom_z"]) - bottom0)
        pelvis_rise.append(fnum(fr["pelvis_z"]) - pelvis0)
        modes.add(fr["move_mode"])
        mesh = (fnum(fr["mesh_x"]), fnum(fr["mesh_y"]), fnum(fr["mesh_z"]))
        if prev_mesh is not None:
            steps.append(math.dist(mesh, prev_mesh))
        prev_mesh = mesh
        if wall:
            (wx, wy, wz), (nx, ny, nz) = wall
            px, py, pz = fnum(fr["pelvis_x"]), fnum(fr["pelvis_y"]), fnum(fr["pelvis_z"])
            # Signed distance of the pelvis to the wall plane; negative = past the wall face.
            pelvis_pen.append((px - wx) * nx + (py - wy) * ny + (pz - wz) * nz)

    wall_frames = sum(1 for fr in r.frames if int(fnum(fr["wall_hits"], 0)) > 0)
    # Older recordings had no pawn columns; their "wall" hits may include the attacker.
    pawn_frames = sum(1 for fr in r.frames if int(fnum(fr.get("pawn_hits", "0"), 0)) > 0)
    overlap_frames = sum(1 for fr in r.frames if fr["overlap"] == "1")
    first_contact = next((fnum(fr["montage_pos"]) for fr in r.frames if int(fnum(fr["wall_hits"], 0)) > 0), math.nan)

    r.metrics = {
        "label": s["label"],
        "direction": s["direction"],
        "montage": next((fr["montage"] for fr in r.frames if fr["montage"]), ""),
        "sweep_dist_cm": fnum(s["sweep_dist"], -1),
        "wall_actor": s["wall_actor"],
        "wall_step_up": s["wall_step_up"],
        "max_step_height": fnum(s["max_step_height"]),
        "frames": len(r.frames),
        "max_retreat_cm": max(retreat),
        "final_retreat_cm": retreat[-1],
        "max_capsule_rise_cm": max(rise),
        "final_capsule_rise_cm": rise[-1],
        "max_pelvis_rise_cm": max(pelvis_rise),
        "min_pelvis_wall_cm": min(pelvis_pen) if pelvis_pen else math.nan,
        "wall_hit_frames": wall_frames,
        "first_contact_montage_s": first_contact,
        "overlap_frames": overlap_frames,
        "pawn_hit_frames": pawn_frames,
        "max_mesh_step_cm": max(steps) if steps else 0.0,
        "median_mesh_step_cm": statistics.median(steps) if steps else 0.0,
        "snap_frames": sum(1 for d in steps if d > JUMP_CM),
        "move_modes": "|".join(sorted(m for m in modes if m)),
    }


def series(r: Reaction, column: str) -> list[tuple[float, float, float]]:
    """(montage_pos, x, y) or similar, only while the montage plays, sorted by position."""
    pts = []
    for fr in r.frames:
        pos = fnum(fr["montage_pos"])
        if pos >= 0:
            pts.append((pos, fnum(fr[f"{column}_x"]), fnum(fr[f"{column}_y"]), fnum(fr[f"{column}_z"])))
    pts.sort()
    return pts


def interp(pts, pos):
    if not pts or pos < pts[0][0] or pos > pts[-1][0]:
        return None
    lo, hi = 0, len(pts) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if pts[mid][0] <= pos:
            lo = mid
        else:
            hi = mid
    a, b = pts[lo], pts[hi]
    t = 0.0 if b[0] == a[0] else (pos - a[0]) / (b[0] - a[0])
    return tuple(a[i] + (b[i] - a[i]) * t for i in (1, 2, 3))


def pair_clients(reactions: list[Reaction]) -> None:
    """Match each client reaction to the server one with the same id and actor (or nearest start time)."""
    servers = [r for r in reactions if r.is_server]
    for c in (r for r in reactions if not r.is_server):
        t = fnum(c.start["utc_ms"])
        candidates = [s for s in servers if s.reaction_id == c.reaction_id
                      and abs(fnum(s.start["utc_ms"]) - t) < 3000]
        same_name = [s for s in candidates if s.actor == c.actor]
        candidates = same_name or candidates
        if not candidates:
            continue
        s = min(candidates, key=lambda x: abs(fnum(x.start["utc_ms"]) - t))
        # Compare at equal montage position, which removes clock offset and start delay.
        server_cap = series(s, "cap")
        errs_cap, errs_mesh = [], []
        for pos, x, y, z in series(c, "cap"):
            ref = interp(server_cap, pos)
            if ref:
                errs_cap.append(math.dist((x, y, z), ref))
        for (pos, x, y, z) in series(c, "mesh"):
            ref = interp(server_cap, pos)
            if ref:
                errs_mesh.append(math.dist((x, y), ref[:2]))
        c.metrics["paired_server_folder"] = s.folder
        c.metrics["client_cap_err_max_cm"] = max(errs_cap) if errs_cap else math.nan
        c.metrics["client_cap_err_p95_cm"] = percentile(errs_cap, 95)
        c.metrics["client_mesh_xy_err_max_cm"] = max(errs_mesh) if errs_mesh else math.nan
        c.metrics["final_pos_diff_cm"] = math.dist(
            (fnum(c.frames[-1]["cap_x"]), fnum(c.frames[-1]["cap_y"])),
            (fnum(s.frames[-1]["cap_x"]), fnum(s.frames[-1]["cap_y"])))


def percentile(values, p):
    if not values:
        return math.nan
    v = sorted(values)
    k = (len(v) - 1) * p / 100
    lo, hi = math.floor(k), math.ceil(k)
    return v[lo] + (v[hi] - v[lo]) * (k - lo)


def assign_groups(reactions: list[Reaction], mode: str) -> float:
    """W0..W3 from the measured wall distance, using L = open-space retreat (doc 02 §4)."""
    open_retreats = [r.metrics["max_retreat_cm"] for r in reactions
                     if r.is_server and r.metrics["sweep_dist_cm"] < 0]
    L = statistics.median(open_retreats) if open_retreats else math.nan
    for r in reactions:
        if mode == "label" and r.metrics["label"]:
            r.metrics["group"] = r.metrics["label"]
            continue
        d = r.metrics["sweep_dist_cm"]
        if d < 0:
            r.metrics["group"] = "W0 no wall"
        elif math.isnan(L):
            # No open-space run yet: fall back to 25 cm distance bins.
            lo = int(d // 25) * 25
            r.metrics["group"] = f"wall {lo}-{lo + 25}cm"
        elif d >= 1.2 * L:
            r.metrics["group"] = "W1 >=1.2L"
        elif d >= 0.3 * L:
            r.metrics["group"] = "W2 0.3L-1.2L"
        else:
            r.metrics["group"] = "W3 <0.3L"
    return L


def symptoms(m: dict) -> dict[str, bool]:
    out = {
        "capsule_rise": m["max_capsule_rise_cm"] > RISE_CM,
        "capsule_overlap": m["overlap_frames"] > 0,
        "pelvis_past_wall": (not math.isnan(m["min_pelvis_wall_cm"])) and m["min_pelvis_wall_cm"] < -PELVIS_PEN_CM,
        "left_ground": any(x in m["move_modes"] for x in ("Falling", "Flying")),
        "mesh_snap": m["snap_frames"] > 0,
    }
    if "client_cap_err_max_cm" in m and not math.isnan(m["client_cap_err_max_cm"]):
        out["client_server_gap"] = m["client_cap_err_max_cm"] > CLIENT_ERR_CM
    return out


def fmt(v) -> str:
    if isinstance(v, float):
        return "" if math.isnan(v) else f"{v:.1f}"
    return str(v)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root", type=Path, help="Saved/HitWallProbe or any folder above the recordings")
    ap.add_argument("--group", choices=["wall", "label"], default="wall",
                    help="wall: W0~W3 from measured wall distance (default). label: hitwall.Label text.")
    ap.add_argument("--out", type=Path, help="folder for reactions.csv, summary.csv, report.md")
    args = ap.parse_args()

    reactions = load(args.root)
    if not reactions:
        print(f"No recordings with frames under {args.root}. Was hitwall.Probe 1 set before the hit?")
        return 1
    for r in reactions:
        compute(r)
    pair_clients(reactions)
    L = assign_groups(reactions, args.group)

    groups: dict[tuple, list[Reaction]] = defaultdict(list)
    for r in reactions:
        groups[(r.metrics["group"], "server" if r.is_server else "client")].append(r)

    names = ["capsule_rise", "capsule_overlap", "pelvis_past_wall", "left_ground", "mesh_snap", "client_server_gap"]
    summary_rows = []
    for (group, side), rs in sorted(groups.items()):
        counts = defaultdict(int)
        for r in rs:
            for k, hit in symptoms(r.metrics).items():
                counts[k] += hit
        n = len(rs)
        row = {"group": group, "side": side, "n": n,
               "pawn_contact": sum(1 for r in rs if r.metrics["pawn_hit_frames"] > 0),
               "median_retreat_cm": statistics.median(r.metrics["max_retreat_cm"] for r in rs),
               "max_capsule_rise_cm": max(r.metrics["max_capsule_rise_cm"] for r in rs)}
        for k in names:
            row[k] = counts.get(k, 0) if (k != "client_server_gap" or side == "client") else ""
        reproduced = [k for k in names if isinstance(row[k], int) and n >= 1 and row[k] / n >= REPRO_RATIO]
        row["reproduced"] = " ".join(reproduced) if n >= 10 else (" ".join(reproduced) + " (n<10)" if reproduced else "n<10")
        summary_rows.append(row)

    print(f"Reactions: {len(reactions)} (server {sum(r.is_server for r in reactions)}, "
          f"client {sum(not r.is_server for r in reactions)})")
    print(f"L (open-space max retreat, server median): {fmt(L)} cm\n")
    header = ["group", "side", "n", "pawn_contact", "median_retreat_cm", "max_capsule_rise_cm"] + names + ["reproduced"]
    widths = [max(len(h), *(len(fmt(r[h])) for r in summary_rows)) for h in header]
    print("  ".join(h.ljust(w) for h, w in zip(header, widths)))
    for r in summary_rows:
        print("  ".join(fmt(r[h]).ljust(w) for h, w in zip(header, widths)))
    print(f"\nSymptom = per reaction: rise>{RISE_CM}cm, overlap>0 frames, pelvis>{PELVIS_PEN_CM}cm past wall, "
          f"Falling/Flying, mesh step>{JUMP_CM}cm/frame, client-server>{CLIENT_ERR_CM}cm. "
          f"Reproduced = >= {int(REPRO_RATIO * 100)}% of reactions in the group (needs n>=10).\n"
          "pawn_contact = reactions where another character (e.g. the attacker) touched the enemy; "
          "use hitwall.Hit so the player does not follow.")

    if args.out:
        args.out.mkdir(parents=True, exist_ok=True)
        cols = ["group", "role", "actor", "reaction_id", "folder"] + sorted({k for r in reactions for k in r.metrics} - {"group"})
        with (args.out / "reactions.csv").open("w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(cols)
            for r in sorted(reactions, key=lambda x: (x.metrics["group"], x.role, x.reaction_id)):
                base = {"group": r.metrics["group"], "role": r.role, "actor": r.actor,
                        "reaction_id": r.reaction_id, "folder": r.folder}
                w.writerow([fmt(base.get(c, r.metrics.get(c, ""))) for c in cols])
        with (args.out / "summary.csv").open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=header)
            w.writeheader()
            for r in summary_rows:
                w.writerow({k: fmt(v) for k, v in r.items()})
        print(f"\nWrote {args.out / 'reactions.csv'} and {args.out / 'summary.csv'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
