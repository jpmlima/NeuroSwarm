#!/usr/bin/env python3
"""Analyse self-model and generate cognitive performance report."""
import json, os

MODEL_PATH = "data/self_model.json"
REPORT_PATH = "data/cognitive_report.txt"

if not os.path.exists(MODEL_PATH):
    print("[REPORT] No self-model found.")
    exit(1)

with open(MODEL_PATH) as f:
    d = json.load(f)

lines = ["=== NeuroSwarm Cognitive Performance Report ===\n"]

# Sort by total attempts
ranked = sorted(d.items(), key=lambda x: x[1].get("success", 0) + x[1].get("failure", 0), reverse=True)

for domain, stats in ranked:
    s = stats.get("success", 0)
    fail = stats.get("failure", 0)
    total = s + fail
    rate = f"{s/total*100:.0f}%" if total > 0 else "N/A"
    pred = f"{stats.get('predicted_success_rate', 0)*100:.0f}%"
    lines.append(f"  {domain:25s}  attempts={total:3d}  success={s:3d}  fail={fail:3d}  rate={rate:>4s}  predicted={pred}")

best = max(d.items(), key=lambda x: x[1].get("success", 0))
worst = max(d.items(), key=lambda x: x[1].get("failure", 0))
total_s = sum(v.get("success", 0) for v in d.values())
total_f = sum(v.get("failure", 0) for v in d.values())

lines.append(f"\nBest domain:    {best[0]} ({best[1].get('success', 0)} successes)")
lines.append(f"Weakest domain: {worst[0]} ({worst[1].get('failure', 0)} failures)")
lines.append(f"Overall: {total_s} successes, {total_f} failures across {len(d)} domains")
lines.append("=== END ===")

report = "\n".join(lines)
os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
with open(REPORT_PATH, "w") as f:
    f.write(report)

print(report)
