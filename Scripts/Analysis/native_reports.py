"""Recording quality, actual connection costs, bounded comparisons and standalone HTML.

Engine connection counters include all traffic on the connection. They are not
per-Enemy costs or NIC bytes; compare only matching scenarios and cost scopes.
"""
from __future__ import annotations

import csv
import html
import itertools
import json
import statistics
from collections import Counter, defaultdict


def rows_of(path):
    if not path.exists():
        return []
    with path.open(encoding="utf-8-sig", newline="") as source:
        return list(csv.DictReader(source))


def number(row, key, default=0.0):
    try:
        return float(row.get(key, default))
    except (ValueError, TypeError):
        return default


def save_csv(path, rows):
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    keys = list(dict.fromkeys(key for row in rows for key in row))
    with path.open("w", encoding="utf-8", newline="") as target:
        writer = csv.DictWriter(target, keys)
        writer.writeheader()
        writer.writerows(rows)


def coverage(trial):
    """Require time coverage for every server execution on every recorded client."""
    if not trial.clients or not trial.server:
        return 0.0
    results = []
    for client in trial.clients.values():
        for key, track in trial.server.items():
            series = client.get(key)
            if not series or len(track.t) < 2:
                results.append(0.0)
                continue
            total = track.t[-1] - track.t[0]
            covered = sum(b - a for a, b in zip(series.t, series.t[1:]) if 0 < b - a <= track.maximum_gap)
            results.append(min(1.0, covered / total) if total > 0 else 0.0)
    return min(results, default=0.0)


def connection_costs(trial):
    groups = defaultdict(list)
    for row in rows_of(trial.path / "connections.csv"):
        if not row.get("Role", "").startswith("Server_"):
            continue
        groups[row.get("Role"), row.get("ConnectionId"), row.get("SourcePart", "")].append(row)
    results = []
    for (role, connection, part), samples in groups.items():
        samples.sort(key=lambda row: number(row, "ServerTime"))
        totals = Counter()
        elapsed = 0.0
        skipped = 0
        for before, after in zip(samples, samples[1:]):
            dt = number(after, "ServerTime") - number(before, "ServerTime")
            if dt <= 0:
                continue
            deltas = {}
            for field in ("InBytes", "OutBytes", "InPackets", "OutPackets"):
                a, b = int(number(before, field)), int(number(after, field))
                # Native UE counters are int32; normalize their uint32 rollover explicitly.
                if b < a and a < 2**31:
                    skipped += 1
                    break
                deltas[field] = (b - a) % 2**32
            else:
                totals.update(deltas)
                elapsed += dt
        if elapsed:
            results.append({"RunId": trial.run_id, "TrialId": trial.trial_id, "Mode": trial.mode,
                            "Role": role, "ConnectionId": connection, "Part": part,
                            "Scope": "EngineConnectionCountersNotActorOrNIC", "DurationSec": elapsed,
                            **dict(totals), "OutBytesPerSec": totals["OutBytes"] / elapsed,
                            "InBytesPerSec": totals["InBytes"] / elapsed, "SkippedCounterResets": skipped})
    return results


def presentation_diagnostics(trial):
    groups = defaultdict(list)
    for row in rows_of(trial.path / "diagnostics.csv"):
        groups[row.get("Role", "Unknown"), row.get("Kind", "Unknown"), row.get("Reason", "")].append(row)
    return [{"RunId": trial.run_id, "TrialId": trial.trial_id, "Mode": trial.mode,
             "Role": role, "Kind": kind, "Reason": reason, "Count": len(rows),
             "ValueMean": statistics.fmean(number(row, "Value") for row in rows),
             "ValueMax": max(number(row, "Value") for row in rows)}
            for (role, kind, reason), rows in sorted(groups.items())]


def detector_costs(trial):
    groups = defaultdict(list)
    for row in rows_of(trial.path / "detector_samples.csv"):
        groups[row.get("ActorId", ""), row.get("Generation", ""), row.get("Segment", "")].append(number(row, "CostMicroseconds"))
    return [{"RunId": trial.run_id, "TrialId": trial.trial_id, "Mode": trial.mode,
             "ActorId": actor, "Generation": generation, "Segment": segment, "Samples": len(values),
             "MeanMicroseconds": statistics.fmean(values), "MaxMicroseconds": max(values), "Scope": "ClassifierOnly"}
            for (actor, generation, segment), values in sorted(groups.items()) if values]


def table(rows, keys):
    if not rows:
        return "<p>해당 기록 없음</p>"
    def cell(value):
        if isinstance(value, float):
            value = f"{value:.4f}"
        return html.escape(str(value if value is not None else "—"))
    return ("<div class='scroll'><table><thead><tr>" + "".join(f"<th>{cell(key)}</th>" for key in keys) +
            "</tr></thead><tbody>" + "".join("<tr>" + "".join(f"<td>{cell(row.get(key))}</td>" for key in keys) +
                                            "</tr>" for row in rows) + "</tbody></table></div>")


def write_extended_reports(out, trials, trial_rows, event_rows, detector_rows, warnings, args):
    costs, diagnostics, quality, candidates, compute_costs = [], [], [], [], []
    for trial in trials:
        own_costs = connection_costs(trial)
        costs.extend(own_costs)
        diagnostics.extend(presentation_diagnostics(trial))
        compute_costs.extend(detector_costs(trial))
        cover = coverage(trial)
        matching = [row for row in trial_rows if row["RunId"] == trial.run_id and str(row["TrialId"]) == trial.trial_id]
        reasons = []
        if not trial.meta.get("NativeRecorder"):
            reasons.append("NoNativeManifest")
        if not trial.meta.get("Valid", False):
            reasons.append("IncompleteOrInvalidTrial")
        if cover < args.minimum_coverage:
            reasons.append("InsufficientCoverage")
        if not trial.meta.get("ComparisonKey"):
            reasons.append("MissingScenarioIdentity")
        if not own_costs:
            reasons.append("NoMeasuredConnectionCost")
        if any(row["SkippedCounterResets"] for row in own_costs):
            reasons.append("ConnectionCounterReset")
        row = {"RunId": trial.run_id, "TrialId": trial.trial_id, "Mode": trial.mode,
               "Coverage": cover, "Eligible": not reasons, "Reasons": ";".join(reasons)}
        quality.append(row)
        if not reasons:
            # Sum connection rates, combining separate parts of the same connection by duration.
            by_connection = defaultdict(list)
            for cost in own_costs:
                by_connection[cost["Role"], cost["ConnectionId"]].append(cost)
            rate = sum(sum(part["OutBytes"] for part in parts) / sum(part["DurationSec"] for part in parts)
                       for parts in by_connection.values())
            mesh = [item["MeshMean"] for item in matching if item.get("MeshMean") is not None]
            candidates.append({**row, "ComparisonKey": trial.meta["ComparisonKey"], "Clock": trial.meta.get("Clock"),
                               "Scope": "EngineConnectionCountersNotActorOrNIC", "OutBytesPerSec": rate,
                               "MeshMean": statistics.fmean(mesh) if mesh else None})
    comparisons = []
    for left, right in itertools.combinations(candidates, 2):
        if left["Mode"] == right["Mode"] or left["ComparisonKey"] != right["ComparisonKey"] or left["Clock"] != right["Clock"]:
            continue
        denominator = max(left["OutBytesPerSec"], right["OutBytesPerSec"])
        gap = abs(left["OutBytesPerSec"] - right["OutBytesPerSec"]) / denominator if denominator else 0
        comparisons.append({"RunA": left["RunId"], "TrialA": left["TrialId"], "ModeA": left["Mode"],
                            "RunB": right["RunId"], "TrialB": right["TrialId"], "ModeB": right["Mode"],
                            "Scope": left["Scope"], "BytesPerSecA": left["OutBytesPerSec"], "BytesPerSecB": right["OutBytesPerSec"],
                            "RelativeBudgetGap": gap, "WithinBudget": gap <= args.budget_tolerance,
                            "MeshMeanA": left["MeshMean"], "MeshMeanB": right["MeshMean"]})
    errors = []
    for trial in trials:
        for client, actors in trial.clients.items():
            for key, series in actors.items():
                errors.extend({"RunId": trial.run_id, "TrialId": trial.trial_id, "Mode": trial.mode, "ClientId": client,
                               "ExecutionKey": key, "ServerTime": time, "CapsuleError": cap, "MeshError": mesh}
                              for time, cap, mesh in zip(series.t, series.err_cap, series.err_mesh))
    save_csv(out / "errors.csv", errors)
    save_csv(out / "connection_costs.csv", costs)
    save_csv(out / "presentation_diagnostics.csv", diagnostics)
    save_csv(out / "detector_costs.csv", compute_costs)
    save_csv(out / "recording_quality.csv", quality)
    save_csv(out / "comparison.csv", comparisons)
    report = {"SchemaVersion": 2, "Trials": trial_rows, "Events": event_rows, "Detector": detector_rows,
              "ConnectionCosts": costs, "RecordingQuality": quality, "Comparisons": comparisons,
              "PresentationDiagnostics": diagnostics, "DetectorComputeCosts": compute_costs, "Warnings": warnings,
              "DetectorReferenceScope": "AnnotatedGroundTruthOnly",
              "BudgetTolerance": args.budget_tolerance, "MinimumCoverage": args.minimum_coverage}
    (out / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False), encoding="utf-8")
    body = ["<h1>Sharp Movement 실험 결과</h1>",
            "<p>시간·위치 단위: s, cm. 비용은 연결 전체의 엔진 카운터이며 Enemy별 전송량 또는 NIC 전송량이 아닙니다.</p>",
            "<p>측정 품질과 시나리오가 일치한 실행만 비용 비교에 포함합니다. WithinBudget=false인 쌍은 같은 비용에서의 품질 비교로 해석할 수 없습니다.</p>",
            "<label>표 검색 <input id='filter' placeholder='Enemy, 모드, RunId 등'></label>",
            "<h2>실행별 품질</h2>", table(quality, ["RunId", "TrialId", "Mode", "Coverage", "Eligible", "Reasons"]),
            "<h2>화면 오차</h2>", table(trial_rows, ["RunId", "TrialId", "Mode", "ClientId", "ActorId", "Generation", "Segment", "CapMean", "CapP95", "MeshMean", "MeshP95", "MeshMax", "UpdatesPerSec"]),
            "<h2>급변 사건</h2>", table(event_rows, ["RunId", "TrialId", "Mode", "ActorId", "EventId", "CapPeak", "MeshPeak", "MeshRecovery", "FirstUpdateDelay"]),
            "<h2>Detector</h2><p>Precision/FP는 Ground Truth 라벨과의 대응 기준입니다. 착지·충돌 등 라벨 없는 실제 급변도 UnmatchedDetections에 포함될 수 있습니다.</p>",
            table(detector_rows, ["RunId", "TrialId", "Mode", "TP", "FN", "FP", "UnmatchedDetections", "Precision", "Recall", "LatencyMean"]),
            "<h2>Detector 계산 비용</h2>", table(compute_costs, ["RunId", "TrialId", "Mode", "ActorId", "Samples", "MeanMicroseconds", "MaxMicroseconds", "Scope"]),
            "<h2>연결 비용</h2>", table(costs, ["RunId", "TrialId", "Mode", "ConnectionId", "DurationSec", "OutBytesPerSec", "InBytesPerSec", "Scope"]),
            "<h2>조건·비용 비교</h2>", table(comparisons, ["ModeA", "ModeB", "BytesPerSecA", "BytesPerSecB", "RelativeBudgetGap", "WithinBudget", "MeshMeanA", "MeshMeanB"]),
            "<h2>화면 보정·Snapshot 진단</h2>", table(diagnostics, ["RunId", "TrialId", "Mode", "Role", "Kind", "Reason", "Count", "ValueMean", "ValueMax"]),
            "<h2>확인 사항</h2><ul>" + "".join(f"<li>{html.escape(w)}</li>" for w in warnings) + "</ul>"]
    page = ("<!doctype html><html lang='ko'><meta charset='utf-8'><meta name='viewport' content='width=device-width'>"
            "<title>Sharp Movement 결과</title><style>body{font:15px system-ui;margin:32px;color:#182433}"
            ".scroll{overflow:auto}table{border-collapse:collapse;width:100%}td,th{padding:9px;border-bottom:1px solid #ddd;text-align:left;white-space:nowrap}"
            "th{background:#edf3f8;position:sticky;top:0}h2{margin-top:36px}input{padding:8px}</style>" + "".join(body) +
            "<script>document.getElementById('filter').addEventListener('input',e=>{const q=e.target.value.toLowerCase();"
            "document.querySelectorAll('tbody tr').forEach(r=>r.hidden=!r.textContent.toLowerCase().includes(q));});</script></html>")
    (out / "report.html").write_text(page, encoding="utf-8")
