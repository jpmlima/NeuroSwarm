#!/usr/bin/env python3
"""
Generate LaTeX tables from NeuroSwarm telemetry for thesis Chapter 12.
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
    m1_ldr,
    m3_operator_counts_by_source,
    m6_success_rate_by_domain,
    m7_prediction_error, m7_mean_prediction_error,
    m10_autonomy_index,
)
from eval.lib.stats import bootstrap_ci


DATA_DIR = Path(__file__).parent.parent / "data"
OUT_DIR = Path(__file__).parent / "results"


def latex_table(caption, label, columns, rows, notes=None):
    """Generate a LaTeX table string."""
    col_spec = "l" + "r" * (len(columns) - 1)
    lines = [
        r"\begin{table}[htbp]",
        r"\centering",
        f"\\caption{{{caption}}}",
        f"\\label{{{label}}}",
        f"\\begin{{tabular}}{{{col_spec}}}",
        r"\toprule",
        " & ".join(f"\\textbf{{{c}}}" for c in columns) + r" \\",
        r"\midrule",
    ]
    for row in rows:
        lines.append(" & ".join(str(v) for v in row) + r" \\")
    lines.append(r"\bottomrule")
    lines.append(r"\end{tabular}")
    if notes:
        lines.append(f"\\par\\smallskip\\footnotesize {notes}")
    lines.append(r"\end{table}")
    return "\n".join(lines)


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    print("Parsing telemetry...")
    goals, results = parse_intrinsic_goals(str(DATA_DIR / "metrics" / "intrinsic_goals.jsonl"))
    inferences = parse_inference_events(str(DATA_DIR / "metrics" / "inference_events.jsonl"))
    operators = parse_operators(str(DATA_DIR / "operators.jsonl"))
    decisions = parse_critic_decisions(str(DATA_DIR / "metrics" / "critic_decisions.jsonl"))
    self_model = parse_self_model(str(DATA_DIR / "self_model.json"))

    timeline = build_goal_timeline(goals, results, inferences)

    tables = []

    # Table 1: Summary Statistics
    print("Generating summary statistics table...")
    n_goals = len(timeline)
    n_tier1 = sum(1 for rg in timeline if rg.tier == 1)
    n_tier3 = sum(1 for rg in timeline if rg.tier == 3)
    n_success = sum(1 for rg in timeline if rg.result and rg.result.success)
    n_resolved = sum(1 for rg in timeline if rg.result)
    n_ops = len(operators)
    op_sources = m3_operator_counts_by_source(operators)
    n_domains = len(set(rg.goal.domain for rg in timeline))
    n_decisions = len(decisions)
    n_approved = sum(1 for d in decisions if d.approved)

    # LDR at different points
    ldr_25 = m1_ldr(timeline, 25)
    ldr_first = ldr_25[24][1] if len(ldr_25) > 24 else None  # First full window
    ldr_mid = ldr_25[len(ldr_25)//2][1] if ldr_25 else None
    ldr_final = ldr_25[-1][1] if ldr_25 else None

    # Autonomy index
    ai = m10_autonomy_index(timeline, self_model, operators, decisions, window=50)

    # Mean prediction error
    mpe = m7_mean_prediction_error(self_model)

    rows = [
        ("Total goals", n_goals, ""),
        ("Goals resolved", n_resolved, f"{n_resolved/n_goals*100:.1f}\\%"),
        ("Tier 1 (Planner)", n_tier1, f"{n_tier1/n_goals*100:.1f}\\%"),
        ("Tier 3 (LLM)", n_tier3, f"{n_tier3/n_goals*100:.1f}\\%"),
        ("Success rate", n_success, f"{n_success/n_resolved*100:.1f}\\%" if n_resolved else "---"),
        ("Operators learned", n_ops, ""),
        ("\\quad bootstrap", op_sources.get("bootstrap", 0), ""),
        ("\\quad mutation", op_sources.get("mutation", 0), ""),
        ("\\quad llm", op_sources.get("llm", 0), ""),
        ("Domains explored", n_domains, ""),
        ("Critic decisions", n_decisions, f"{n_approved/n_decisions*100:.1f}\\% approved" if n_decisions else ""),
        ("LDR (first window)", "", f"{ldr_first:.3f}" if ldr_first is not None else "---"),
        ("LDR (midpoint)", "", f"{ldr_mid:.3f}" if ldr_mid is not None else "---"),
        ("LDR (final)", "", f"{ldr_final:.3f}" if ldr_final is not None else "---"),
        ("Autonomy Index", "", f"{ai:.3f}" if ai is not None else "---"),
        ("Mean prediction error", "", f"{mpe:.4f}"),
    ]

    tables.append(latex_table(
        "Summary statistics from 68 hours of autonomous operation",
        "tab:summary-stats",
        ["Metric", "Count", "Value"],
        rows,
    ))

    # Table 2: Per-domain success rates
    print("Generating per-domain success rate table...")
    sr_by_domain = m6_success_rate_by_domain(timeline)
    pe_by_domain = m7_prediction_error(self_model)

    domain_rows = []
    for domain in sorted(sr_by_domain.keys()):
        d = sr_by_domain[domain]
        pe = pe_by_domain.get(domain, float('nan'))
        domain_rows.append((
            domain.replace("_", "\\_"),
            d["total"],
            d["successes"],
            f"{d['rate']*100:.1f}\\%",
            f"{pe:.4f}" if pe == pe else "---",  # NaN check
        ))

    tables.append(latex_table(
        "Per-domain success rates and prediction errors",
        "tab:domain-success",
        ["Domain", "Attempts", "Successes", "Rate", "Pred.~Error"],
        sorted(domain_rows, key=lambda r: -int(r[1])),
    ))

    # Table 3: Operator learning breakdown
    print("Generating operator learning table...")
    # Group operators by source and compute stats
    from collections import defaultdict
    by_source = defaultdict(list)
    for op in operators:
        by_source[op.learned_from].append(op)

    op_rows = []
    for source in ["bootstrap", "mutation", "llm"]:
        ops = by_source.get(source, [])
        if not ops:
            op_rows.append((source, 0, "---", "---", "---"))
            continue
        rates = [op.success_rate for op in ops]
        mean_sr = sum(rates) / len(rates)
        mean_used = sum(op.times_used for op in ops) / len(ops)
        mean_dur = sum(op.avg_duration_ms for op in ops if op.avg_duration_ms > 0)
        n_dur = sum(1 for op in ops if op.avg_duration_ms > 0)
        op_rows.append((
            source,
            len(ops),
            f"{mean_sr*100:.1f}\\%",
            f"{mean_used:.1f}",
            f"{mean_dur/n_dur:.1f} ms" if n_dur > 0 else "---",
        ))

    tables.append(latex_table(
        "Operator learning breakdown by source",
        "tab:operator-sources",
        ["Source", "Count", "Mean SR", "Mean Uses", "Mean Duration"],
        op_rows,
    ))

    # Write all tables to a single .tex file
    output = "% Auto-generated by eval/generate_tables.py\n"
    output += "% Include in thesis with: \\input{eval/results/tables.tex}\n\n"
    output += "\n\n".join(tables) + "\n"

    out_path = OUT_DIR / "tables.tex"
    with open(out_path, "w") as f:
        f.write(output)

    print(f"\nTables written to {out_path}")


if __name__ == "__main__":
    main()
