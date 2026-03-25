#!/usr/bin/env python3
"""
Generate pgfplots-compatible CSV/TSV files from NeuroSwarm telemetry.

Output files go to eval/results/ for inclusion in the thesis.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))

from eval.lib.parsers import (
    parse_intrinsic_goals,
    parse_inference_events,
    parse_operators,
    parse_critic_decisions,
    parse_self_model,
    build_goal_timeline,
)
from eval.lib.metrics import (
    m1_ldr, m1_ldr_multi_window,
    m2_tier_distribution,
    m3_operator_learning_rate, m3_operator_counts_by_source,
    m4_domain_coverage, m4_cumulative_domains,
    m5_safety_rate,
    m6_success_rate_over_time,
    m8_resource_efficiency,
    m10_autonomy_index_over_time,
)
from eval.lib.pgfplots import write_tsv


DATA_DIR = Path(__file__).parent.parent / "data"
OUT_DIR = Path(__file__).parent / "results"


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    print("Parsing telemetry...")
    goals, results = parse_intrinsic_goals(str(DATA_DIR / "metrics" / "intrinsic_goals.jsonl"))
    inferences = parse_inference_events(str(DATA_DIR / "metrics" / "inference_events.jsonl"))
    operators = parse_operators(str(DATA_DIR / "operators.jsonl"))
    decisions = parse_critic_decisions(str(DATA_DIR / "metrics" / "critic_decisions.jsonl"))
    self_model = parse_self_model(str(DATA_DIR / "self_model.json"))

    print(f"  Goals: {len(goals)}, Results: {len(results)}, Inferences: {len(inferences)}")
    print(f"  Operators: {len(operators)}, Critic decisions: {len(decisions)}")

    print("Building goal timeline...")
    timeline = build_goal_timeline(goals, results, inferences)
    print(f"  Resolved goals: {len(timeline)}")
    print(f"  Tier 1 (no LLM): {sum(1 for rg in timeline if rg.tier == 1)}")
    print(f"  Tier 3 (LLM):    {sum(1 for rg in timeline if rg.tier == 3)}")

    # 1. LDR decay curves (main thesis figure)
    print("Generating ldr_decay.csv...")
    for w in [10, 25, 50]:
        points = m1_ldr(timeline, w)
        write_tsv(
            str(OUT_DIR / f"ldr_decay_w{w}.csv"),
            ["goal_index", "ldr"],
            [(i, f"{v:.4f}") for i, v in points],
        )

    # 2. Tier distribution
    print("Generating tier_distribution.csv...")
    tier_pts = m2_tier_distribution(timeline, window=25)
    write_tsv(
        str(OUT_DIR / "tier_distribution.csv"),
        ["goal_index", "pct_planner", "pct_llm"],
        [(i, f"{t1:.4f}", f"{t3:.4f}") for i, t1, t3 in tier_pts],
    )

    # 3. Operator growth by source
    print("Generating operator_growth.csv...")
    op_pts = m3_operator_learning_rate(operators)
    write_tsv(
        str(OUT_DIR / "operator_growth.csv"),
        ["timestamp", "cumulative", "source"],
        [(ts, cum, src) for ts, cum, src in op_pts],
    )

    # Operator growth cumulative by source (for stacked area)
    from collections import Counter
    sorted_ops = sorted(operators, key=lambda o: o.learned_at)
    source_cum = Counter()
    stacked_rows = []
    for i, op in enumerate(sorted_ops):
        source_cum[op.learned_from] += 1
        stacked_rows.append((
            i,
            source_cum.get("bootstrap", 0),
            source_cum.get("mutation", 0),
            source_cum.get("llm", 0),
        ))
    write_tsv(
        str(OUT_DIR / "operator_growth_stacked.csv"),
        ["index", "bootstrap", "mutation", "llm"],
        stacked_rows,
    )

    # 4. Domain coverage
    print("Generating domain_coverage.csv...")
    dc_pts = m4_cumulative_domains(timeline)
    write_tsv(
        str(OUT_DIR / "domain_coverage.csv"),
        ["goal_index", "domains"],
        dc_pts,
    )

    dc_entropy = m4_domain_coverage(timeline, window=50)
    write_tsv(
        str(OUT_DIR / "domain_entropy.csv"),
        ["goal_index", "domains", "entropy"],
        [(i, d, f"{e:.4f}") for i, d, e in dc_entropy],
    )

    # 5. Safety rate
    print("Generating safety_rate.csv...")
    sr_pts = m5_safety_rate(decisions, window=50)
    write_tsv(
        str(OUT_DIR / "safety_rate.csv"),
        ["decision_index", "approval_rate"],
        [(i, f"{v:.4f}") for i, v in sr_pts],
    )

    # 6. Resource efficiency
    print("Generating resource_efficiency.csv...")
    re_pts = m8_resource_efficiency(timeline, window=25)
    write_tsv(
        str(OUT_DIR / "resource_efficiency.csv"),
        ["goal_index", "llm_per_success"],
        [(i, f"{v:.4f}") for i, v in re_pts],
    )

    # 7. Success rate over time
    print("Generating success_rate.csv...")
    success_pts = m6_success_rate_over_time(timeline, window=25)
    write_tsv(
        str(OUT_DIR / "success_rate.csv"),
        ["goal_index", "success_rate"],
        [(i, f"{v:.4f}") for i, v in success_pts],
    )

    # 8. Autonomy index
    print("Generating autonomy_index.csv...")
    ai_pts = m10_autonomy_index_over_time(timeline, self_model, operators, decisions, window=50)
    write_tsv(
        str(OUT_DIR / "autonomy_index.csv"),
        ["goal_index", "autonomy_index"],
        [(i, f"{v:.4f}") for i, v in ai_pts],
    )

    print(f"\nAll plots written to {OUT_DIR}/")
    print("Files:")
    for f in sorted(OUT_DIR.glob("*.csv")):
        lines = sum(1 for _ in open(f)) - 1  # subtract header
        print(f"  {f.name}: {lines} data points")


if __name__ == "__main__":
    main()
