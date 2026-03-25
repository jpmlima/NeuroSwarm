"""
Metric formulas for NeuroSwarm evaluation.

M1-M10 as defined in the evaluation framework plan.
"""

import math
from collections import Counter
from typing import Optional

from .parsers import ResolvedGoal, Operator, CriticDecision, parse_ts


def m1_ldr(timeline: list[ResolvedGoal], window: int = 25) -> list[tuple[int, float]]:
    """M1: LLM Dependency Ratio over a sliding window.

    LDR(i, w) = |goals requiring LLM in [i-w+1, i]| / w

    Returns list of (goal_index, ldr_value).
    """
    points = []
    for i in range(len(timeline)):
        start = max(0, i - window + 1)
        chunk = timeline[start:i + 1]
        if not chunk:
            continue
        llm_count = sum(1 for rg in chunk if rg.tier == 3)
        ldr = llm_count / len(chunk)
        points.append((i, ldr))
    return points


def m1_ldr_multi_window(
    timeline: list[ResolvedGoal],
    windows: list[int] = [10, 25, 50],
) -> dict[int, list[tuple[int, float]]]:
    """M1 for multiple window sizes."""
    return {w: m1_ldr(timeline, w) for w in windows}


def m2_tier_distribution(
    timeline: list[ResolvedGoal],
    window: int = 25,
) -> list[tuple[int, float, float]]:
    """M2: Execution Tier Distribution over sliding window.

    Returns list of (goal_index, pct_tier1, pct_tier3).
    Tier1 = Planner/Genome (no LLM), Tier3 = LLM.
    """
    points = []
    for i in range(len(timeline)):
        start = max(0, i - window + 1)
        chunk = timeline[start:i + 1]
        n = len(chunk)
        if n == 0:
            continue
        t1 = sum(1 for rg in chunk if rg.tier == 1) / n
        t3 = sum(1 for rg in chunk if rg.tier == 3) / n
        points.append((i, t1, t3))
    return points


def m3_operator_learning_rate(
    operators: list[Operator],
) -> list[tuple[str, int, str]]:
    """M3: Cumulative operator count over time, by source.

    Returns list of (learned_at, cumulative_count, source).
    Sorted by learned_at timestamp.
    """
    # Sort by timestamp
    sorted_ops = sorted(operators, key=lambda o: o.learned_at)

    cumulative = 0
    by_source: dict[str, int] = Counter()
    points = []
    for op in sorted_ops:
        cumulative += 1
        by_source[op.learned_from] += 1
        points.append((op.learned_at, cumulative, op.learned_from))

    return points


def m3_operator_counts_by_source(operators: list[Operator]) -> dict[str, int]:
    """Breakdown of operators by learning source."""
    counts = Counter()
    for op in operators:
        counts[op.learned_from] += 1
    return dict(counts)


def m4_domain_coverage(
    timeline: list[ResolvedGoal],
    window: int = 50,
) -> list[tuple[int, int, float]]:
    """M4: Domain Coverage and Shannon entropy over sliding window.

    Returns list of (goal_index, num_domains, entropy).
    """
    points = []
    for i in range(len(timeline)):
        start = max(0, i - window + 1)
        chunk = timeline[start:i + 1]
        if not chunk:
            continue

        domain_counts = Counter(rg.goal.domain for rg in chunk)
        n_domains = len(domain_counts)
        total = sum(domain_counts.values())

        # Shannon entropy
        entropy = 0.0
        for count in domain_counts.values():
            p = count / total
            if p > 0:
                entropy -= p * math.log2(p)

        points.append((i, n_domains, entropy))
    return points


def m4_cumulative_domains(timeline: list[ResolvedGoal]) -> list[tuple[int, int]]:
    """Cumulative unique domains explored over goal index."""
    seen = set()
    points = []
    for i, rg in enumerate(timeline):
        seen.add(rg.goal.domain)
        points.append((i, len(seen)))
    return points


def m5_safety_rate(
    decisions: list[CriticDecision],
    window: int = 50,
) -> list[tuple[int, float]]:
    """M5: Critic approval rate over sliding window.

    Returns list of (decision_index, approval_rate).
    """
    points = []
    for i in range(len(decisions)):
        start = max(0, i - window + 1)
        chunk = decisions[start:i + 1]
        if not chunk:
            continue
        approved = sum(1 for d in chunk if d.approved)
        points.append((i, approved / len(chunk)))
    return points


def m6_success_rate_by_domain(
    timeline: list[ResolvedGoal],
) -> dict[str, dict]:
    """M6: Per-domain success rate.

    Returns {domain: {total, successes, failures, rate}}.
    """
    domains: dict[str, dict] = {}
    for rg in timeline:
        d = rg.goal.domain
        if d not in domains:
            domains[d] = {"total": 0, "successes": 0, "failures": 0}

        if rg.result:
            domains[d]["total"] += 1
            if rg.result.success:
                domains[d]["successes"] += 1
            else:
                domains[d]["failures"] += 1

    for d in domains:
        t = domains[d]["total"]
        domains[d]["rate"] = domains[d]["successes"] / t if t > 0 else 0.0

    return domains


def m6_success_rate_over_time(
    timeline: list[ResolvedGoal],
    window: int = 25,
) -> list[tuple[int, float]]:
    """Overall success rate over sliding window."""
    points = []
    for i in range(len(timeline)):
        start = max(0, i - window + 1)
        chunk = timeline[start:i + 1]
        resolved = [rg for rg in chunk if rg.result]
        if not resolved:
            continue
        rate = sum(1 for rg in resolved if rg.result.success) / len(resolved)
        points.append((i, rate))
    return points


def m7_prediction_error(
    self_model: dict[str, dict],
) -> dict[str, float]:
    """M7: Prediction error per domain from self_model.json.

    |predicted_success_rate - actual_success_rate| per domain.
    """
    errors = {}
    for domain, stats in self_model.items():
        predicted = stats.get("predicted_success_rate", 0.0)
        actual = stats.get("actual_success_rate", 0.0)
        errors[domain] = abs(predicted - actual)
    return errors


def m7_mean_prediction_error(self_model: dict[str, dict]) -> float:
    """Mean absolute prediction error across all domains."""
    errors = m7_prediction_error(self_model)
    if not errors:
        return 0.0
    return sum(errors.values()) / len(errors)


def m8_resource_efficiency(
    timeline: list[ResolvedGoal],
    window: int = 25,
) -> list[tuple[int, float]]:
    """M8: LLM calls per successful goal over sliding window.

    Lower = more efficient (Planner resolving without LLM).
    """
    points = []
    for i in range(len(timeline)):
        start = max(0, i - window + 1)
        chunk = timeline[start:i + 1]
        successes = [rg for rg in chunk if rg.result and rg.result.success]
        if not successes:
            continue
        total_llm = sum(rg.llm_calls for rg in chunk)
        points.append((i, total_llm / len(successes)))
    return points


def m9_neurogenesis_count(
    operators: list[Operator],
) -> dict[str, int]:
    """M9: Count of operators generated by source (neurogenesis = mutation + llm)."""
    counts = Counter()
    for op in operators:
        counts[op.learned_from] += 1
    return dict(counts)


def m10_autonomy_index(
    timeline: list[ResolvedGoal],
    self_model: dict[str, dict],
    operators: list[Operator],
    decisions: list[CriticDecision],
    window: int = 50,
) -> Optional[float]:
    """M10: Composite Autonomy Index.

    AI = (1-LDR)*0.4 + DC*0.2 + mean(SRD)*0.2 + SR*0.1 + (OLR/1000)*0.1

    Where:
    - LDR = final LLM Dependency Ratio
    - DC = domain coverage fraction (observed / total known)
    - SRD = mean success rate across domains
    - SR = critic safety/approval rate
    - OLR = total operator count (capped at 1000)
    """
    if not timeline:
        return None

    # LDR: last window
    ldr_points = m1_ldr(timeline, window)
    ldr = ldr_points[-1][1] if ldr_points else 1.0

    # Domain coverage: fraction of known domains
    all_domains = set(rg.goal.domain for rg in timeline)
    known_domains = set(self_model.keys()) | all_domains
    dc = len(all_domains) / len(known_domains) if known_domains else 0.0

    # Mean success rate by domain
    sr_by_domain = m6_success_rate_by_domain(timeline)
    rates = [d["rate"] for d in sr_by_domain.values() if d["total"] > 0]
    mean_srd = sum(rates) / len(rates) if rates else 0.0

    # Critic approval rate (overall)
    if decisions:
        sr = sum(1 for d in decisions if d.approved) / len(decisions)
    else:
        sr = 0.0

    # Operator count
    olr = min(len(operators), 1000) / 1000.0

    ai = (1 - ldr) * 0.4 + dc * 0.2 + mean_srd * 0.2 + sr * 0.1 + olr * 0.1
    return ai


def m10_autonomy_index_over_time(
    timeline: list[ResolvedGoal],
    self_model: dict[str, dict],
    operators: list[Operator],
    decisions: list[CriticDecision],
    window: int = 50,
) -> list[tuple[int, float]]:
    """Autonomy index computed at each goal index using available data up to that point."""
    points = []
    all_known_domains = set(self_model.keys())

    for i in range(window, len(timeline)):
        chunk = timeline[:i + 1]
        tail = chunk[max(0, len(chunk) - window):]

        # LDR
        llm_count = sum(1 for rg in tail if rg.tier == 3)
        ldr = llm_count / len(tail)

        # Domain coverage
        seen_domains = set(rg.goal.domain for rg in chunk)
        known = all_known_domains | seen_domains
        dc = len(seen_domains) / len(known) if known else 0.0

        # Success rate
        resolved = [rg for rg in tail if rg.result]
        mean_sr = (sum(1 for rg in resolved if rg.result.success) / len(resolved)
                   if resolved else 0.0)

        # Critic rate (use all decisions up to timestamp)
        goal_ts = parse_ts(timeline[i].goal.timestamp)
        relevant_decisions = [d for d in decisions if parse_ts(d.timestamp) <= goal_ts]
        critic_rate = (sum(1 for d in relevant_decisions if d.approved) / len(relevant_decisions)
                       if relevant_decisions else 0.0)

        # Operators learned up to this time
        ops_count = sum(1 for op in operators if parse_ts(op.learned_at) <= goal_ts)
        olr = min(ops_count, 1000) / 1000.0

        ai = (1 - ldr) * 0.4 + dc * 0.2 + mean_sr * 0.2 + critic_rate * 0.1 + olr * 0.1
        points.append((i, ai))

    return points
