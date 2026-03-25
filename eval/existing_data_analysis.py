#!/usr/bin/env python3
"""
Analyze existing NeuroSwarm telemetry data (~68 hours of autonomous operation).

Produces summary statistics, validates tier attribution, and runs all metrics.
This is the Phase 1 sanity check — no experiments needed, just existing data.
"""

import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))

from eval.lib.parsers import (
    parse_intrinsic_goals,
    parse_inference_events,
    parse_operators,
    parse_critic_decisions,
    parse_self_model,
    parse_dopamine_signals,
    build_goal_timeline,
    parse_ts,
)
from eval.lib.metrics import (
    m1_ldr, m1_ldr_multi_window,
    m2_tier_distribution,
    m3_operator_counts_by_source,
    m4_domain_coverage, m4_cumulative_domains,
    m5_safety_rate,
    m6_success_rate_by_domain, m6_success_rate_over_time,
    m7_prediction_error, m7_mean_prediction_error,
    m8_resource_efficiency,
    m9_neurogenesis_count,
    m10_autonomy_index,
)
from eval.lib.stats import bootstrap_ci


DATA_DIR = Path(__file__).parent.parent / "data"


def section(title):
    print(f"\n{'='*60}")
    print(f"  {title}")
    print(f"{'='*60}")


def main():
    section("NEUROSWARM TELEMETRY ANALYSIS")
    print(f"Data directory: {DATA_DIR}")

    # --- Parse all data ---
    section("1. PARSING")

    goals_path = DATA_DIR / "metrics" / "intrinsic_goals.jsonl"
    inf_path = DATA_DIR / "metrics" / "inference_events.jsonl"
    ops_path = DATA_DIR / "operators.jsonl"
    critic_path = DATA_DIR / "metrics" / "critic_decisions.jsonl"
    sm_path = DATA_DIR / "self_model.json"

    goals, results = parse_intrinsic_goals(str(goals_path))
    inferences = parse_inference_events(str(inf_path))
    operators = parse_operators(str(ops_path))
    decisions = parse_critic_decisions(str(critic_path))
    self_model = parse_self_model(str(sm_path))
    dopamine = parse_dopamine_signals(str(goals_path))

    print(f"Goals started:    {len(goals)}")
    print(f"Goal results:     {len(results)}")
    print(f"Inference events: {len(inferences)}")
    print(f"Operators:        {len(operators)}")
    print(f"Critic decisions: {len(decisions)}")
    print(f"Dopamine signals: {len(dopamine)}")
    print(f"Self-model domains: {len(self_model)}")

    # Time span
    if goals:
        ts_first = parse_ts(goals[0].timestamp)
        ts_last = parse_ts(goals[-1].timestamp)
        hours = (ts_last - ts_first) / 3600
        print(f"\nTime span: {goals[0].timestamp} → {goals[-1].timestamp}")
        print(f"Duration:  {hours:.1f} hours")

    # --- Build timeline ---
    section("2. GOAL TIMELINE & TIER ATTRIBUTION")

    timeline = build_goal_timeline(goals, results, inferences)
    n = len(timeline)
    t1 = sum(1 for rg in timeline if rg.tier == 1)
    t3 = sum(1 for rg in timeline if rg.tier == 3)

    print(f"Total resolved goals: {n}")
    print(f"Tier 1 (Planner/Genome — no LLM): {t1} ({t1/n*100:.1f}%)")
    print(f"Tier 3 (LLM):                     {t3} ({t3/n*100:.1f}%)")

    # CID correlation audit
    matched = sum(1 for rg in timeline if rg.result is not None)
    print(f"\nCID correlation audit:")
    print(f"  Goals with matched results: {matched}/{n} ({matched/n*100:.1f}%)")

    # Adapter breakdown for LLM-tier goals
    adapter_counts = Counter()
    for rg in timeline:
        for a in rg.adapters_used:
            adapter_counts[a] += 1
    print(f"  Adapter usage: {dict(adapter_counts)}")

    # --- M1: LDR ---
    section("3. M1: LLM DEPENDENCY RATIO")

    for w in [10, 25, 50]:
        ldr = m1_ldr(timeline, w)
        if ldr:
            first_val = ldr[w-1][1] if len(ldr) > w-1 else ldr[0][1]
            mid_val = ldr[len(ldr)//2][1]
            final_val = ldr[-1][1]

            # Bootstrap CI on final quarter
            quarter = [v for _, v in ldr[3*len(ldr)//4:]]
            if quarter:
                pt, lo, hi = bootstrap_ci(quarter)
                print(f"Window={w}: first={first_val:.3f}, mid={mid_val:.3f}, "
                      f"final={final_val:.3f} [95% CI: {lo:.3f}–{hi:.3f}]")
            else:
                print(f"Window={w}: first={first_val:.3f}, mid={mid_val:.3f}, final={final_val:.3f}")

    # LDR trend: is it monotonically decreasing?
    ldr_25 = m1_ldr(timeline, 25)
    if len(ldr_25) > 100:
        q1 = [v for _, v in ldr_25[:len(ldr_25)//4]]
        q4 = [v for _, v in ldr_25[3*len(ldr_25)//4:]]
        mean_q1 = sum(q1) / len(q1)
        mean_q4 = sum(q4) / len(q4)
        delta = mean_q1 - mean_q4
        print(f"\nLDR trend (w=25): Q1 mean={mean_q1:.3f}, Q4 mean={mean_q4:.3f}, delta={delta:.3f}")
        if delta > 0:
            print(f"  ✓ LDR decreased by {delta:.3f} — thesis hypothesis supported")
        else:
            print(f"  ✗ LDR did not decrease — hypothesis not supported")

    # --- M2: Tier Distribution ---
    section("4. M2: EXECUTION TIER DISTRIBUTION")

    tier_dist = m2_tier_distribution(timeline, window=25)
    if tier_dist:
        first_t1 = tier_dist[24][1] if len(tier_dist) > 24 else tier_dist[0][1]
        final_t1 = tier_dist[-1][1]
        print(f"Planner resolution: first={first_t1*100:.1f}%, final={final_t1*100:.1f}%")

    # --- M3: Operator Learning ---
    section("5. M3: OPERATOR LEARNING RATE")

    op_sources = m3_operator_counts_by_source(operators)
    print(f"Total operators: {len(operators)}")
    for src, count in sorted(op_sources.items(), key=lambda x: -x[1]):
        print(f"  {src}: {count} ({count/len(operators)*100:.1f}%)")

    # --- M4: Domain Coverage ---
    section("6. M4: DOMAIN COVERAGE")

    all_domains = set(rg.goal.domain for rg in timeline)
    print(f"Domains explored: {len(all_domains)}")
    for d in sorted(all_domains):
        count = sum(1 for rg in timeline if rg.goal.domain == d)
        print(f"  {d}: {count} goals")

    dc_entropy = m4_domain_coverage(timeline, window=50)
    if dc_entropy:
        final_entropy = dc_entropy[-1][2]
        print(f"\nShannon entropy (w=50, final): {final_entropy:.3f}")

    # --- M5: Safety Rate ---
    section("7. M5: SAFETY RATE")

    if decisions:
        approved = sum(1 for d in decisions if d.approved)
        print(f"Total decisions: {len(decisions)}")
        print(f"Approved: {approved} ({approved/len(decisions)*100:.1f}%)")
        print(f"Rejected: {len(decisions)-approved} ({(len(decisions)-approved)/len(decisions)*100:.1f}%)")

    # --- M6: Success Rate by Domain ---
    section("8. M6: SUCCESS RATE BY DOMAIN")

    sr_by_domain = m6_success_rate_by_domain(timeline)
    for domain in sorted(sr_by_domain.keys(), key=lambda d: -sr_by_domain[d]["total"]):
        d = sr_by_domain[domain]
        print(f"  {domain:25s}  {d['successes']:4d}/{d['total']:4d}  ({d['rate']*100:5.1f}%)")

    # --- M7: Prediction Error ---
    section("9. M7: PREDICTION ERROR")

    pe = m7_prediction_error(self_model)
    for domain in sorted(pe.keys(), key=lambda d: -pe[d]):
        print(f"  {domain:25s}  {pe[domain]:.4f}")
    print(f"\n  Mean prediction error: {m7_mean_prediction_error(self_model):.4f}")

    # --- M8: Resource Efficiency ---
    section("10. M8: RESOURCE EFFICIENCY")

    re_pts = m8_resource_efficiency(timeline, window=25)
    if re_pts:
        final_re = re_pts[-1][1]
        first_re = re_pts[24][1] if len(re_pts) > 24 else re_pts[0][1]
        print(f"LLM calls per successful goal: first={first_re:.2f}, final={final_re:.2f}")

    # --- M9: Neurogenesis ---
    section("11. M9: NEUROGENESIS COUNT")

    neuro = m9_neurogenesis_count(operators)
    print(f"Neurogenesis (mutation + llm): {neuro.get('mutation', 0) + neuro.get('llm', 0)}")
    for src, count in sorted(neuro.items()):
        print(f"  {src}: {count}")

    # --- M10: Autonomy Index ---
    section("12. M10: AUTONOMY INDEX")

    ai = m10_autonomy_index(timeline, self_model, operators, decisions, window=50)
    if ai is not None:
        print(f"Autonomy Index: {ai:.4f}")
        print(f"  (1.0 = fully autonomous, 0.0 = fully LLM-dependent)")

    # --- Summary ---
    section("SUMMARY")

    print(f"Duration:              {hours:.1f} hours" if goals else "Duration: unknown")
    print(f"Goals:                 {n}")
    print(f"LDR decay:             {mean_q1:.3f} → {mean_q4:.3f}" if len(ldr_25) > 100 else "LDR: insufficient data")
    print(f"Planner resolution:    {final_t1*100:.1f}%" if tier_dist else "")
    print(f"Operators:             {len(operators)} ({op_sources})")
    print(f"Domains:               {len(all_domains)}")
    print(f"Safety rate:           {approved/len(decisions)*100:.1f}%" if decisions else "")
    print(f"Autonomy Index:        {ai:.4f}" if ai else "")
    print(f"\nAnalysis complete.")


if __name__ == "__main__":
    main()
