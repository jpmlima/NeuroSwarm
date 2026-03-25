"""
Statistical tests for NeuroSwarm evaluation.

Non-parametric tests appropriate for small sample sizes (n=5 trials).
"""

import math
import random
from typing import Callable, Optional


def bootstrap_ci(
    data: list[float],
    statistic: Callable[[list[float]], float] = None,
    n_bootstrap: int = 10000,
    alpha: float = 0.05,
    seed: int = 42,
) -> tuple[float, float, float]:
    """Bootstrap confidence interval.

    Args:
        data: observed values
        statistic: function to compute statistic (default: mean)
        n_bootstrap: number of bootstrap resamples
        alpha: significance level (0.05 = 95% CI)
        seed: random seed for reproducibility

    Returns:
        (point_estimate, ci_lower, ci_upper)
    """
    if statistic is None:
        statistic = lambda x: sum(x) / len(x) if x else 0.0

    rng = random.Random(seed)
    n = len(data)
    if n == 0:
        return (0.0, 0.0, 0.0)

    point = statistic(data)
    boot_stats = []
    for _ in range(n_bootstrap):
        sample = [rng.choice(data) for _ in range(n)]
        boot_stats.append(statistic(sample))

    boot_stats.sort()
    lo_idx = int(n_bootstrap * alpha / 2)
    hi_idx = int(n_bootstrap * (1 - alpha / 2))

    return (point, boot_stats[lo_idx], boot_stats[hi_idx])


def wilcoxon_signed_rank(a: list[float], b: list[float]) -> tuple[float, float]:
    """Wilcoxon signed-rank test for paired samples.

    Args:
        a, b: paired measurements (same length)

    Returns:
        (W_statistic, approximate_p_value)

    Uses normal approximation for p-value (valid for n >= 5).
    """
    assert len(a) == len(b), "Paired samples must have same length"
    n = len(a)

    diffs = [(a[i] - b[i]) for i in range(n)]
    # Remove zero differences
    diffs = [(abs(d), 1 if d > 0 else -1) for d in diffs if d != 0]

    if not diffs:
        return (0.0, 1.0)

    # Rank by absolute value
    diffs.sort(key=lambda x: x[0])
    ranks = []
    i = 0
    while i < len(diffs):
        j = i
        while j < len(diffs) and diffs[j][0] == diffs[i][0]:
            j += 1
        avg_rank = sum(range(i + 1, j + 1)) / (j - i)
        for k in range(i, j):
            ranks.append((avg_rank, diffs[k][1]))
        i = j

    # W+ = sum of ranks of positive differences
    w_plus = sum(r for r, s in ranks if s > 0)
    w_minus = sum(r for r, s in ranks if s < 0)
    w = min(w_plus, w_minus)

    nr = len(ranks)
    if nr < 5:
        # Too few for normal approximation
        return (w, float('nan'))

    # Normal approximation
    mean_w = nr * (nr + 1) / 4
    std_w = math.sqrt(nr * (nr + 1) * (2 * nr + 1) / 24)
    if std_w == 0:
        return (w, 1.0)

    z = (w - mean_w) / std_w
    # Two-tailed p-value (approximate using standard normal CDF)
    p = 2 * _norm_cdf(-abs(z))

    return (w, p)


def mann_whitney_u(a: list[float], b: list[float]) -> tuple[float, float]:
    """Mann-Whitney U test for unpaired samples.

    Returns:
        (U_statistic, approximate_p_value)
    """
    na, nb = len(a), len(b)
    if na == 0 or nb == 0:
        return (0.0, 1.0)

    # Combine and rank
    combined = [(v, 'a') for v in a] + [(v, 'b') for v in b]
    combined.sort(key=lambda x: x[0])

    ranks = {}
    i = 0
    while i < len(combined):
        j = i
        while j < len(combined) and combined[j][0] == combined[i][0]:
            j += 1
        avg_rank = sum(range(i + 1, j + 1)) / (j - i)
        for k in range(i, j):
            if k not in ranks:
                ranks[k] = []
            ranks[k] = avg_rank
        i = j

    rank_sum_a = sum(ranks[i] for i in range(len(combined)) if combined[i][1] == 'a')
    u_a = rank_sum_a - na * (na + 1) / 2
    u_b = na * nb - u_a
    u = min(u_a, u_b)

    # Normal approximation
    mean_u = na * nb / 2
    std_u = math.sqrt(na * nb * (na + nb + 1) / 12)
    if std_u == 0:
        return (u, 1.0)

    z = (u - mean_u) / std_u
    p = 2 * _norm_cdf(-abs(z))

    return (u, p)


def cohens_d(a: list[float], b: list[float]) -> float:
    """Cohen's d effect size for two groups."""
    na, nb = len(a), len(b)
    if na < 2 or nb < 2:
        return 0.0

    mean_a = sum(a) / na
    mean_b = sum(b) / nb
    var_a = sum((x - mean_a) ** 2 for x in a) / (na - 1)
    var_b = sum((x - mean_b) ** 2 for x in b) / (nb - 1)

    pooled_std = math.sqrt(((na - 1) * var_a + (nb - 1) * var_b) / (na + nb - 2))
    if pooled_std == 0:
        return 0.0

    return (mean_a - mean_b) / pooled_std


def holm_bonferroni(p_values: list[tuple[str, float]]) -> list[tuple[str, float, bool]]:
    """Holm-Bonferroni correction for multiple comparisons.

    Args:
        p_values: list of (label, p_value)

    Returns:
        list of (label, adjusted_p, significant_at_0.05)
    """
    n = len(p_values)
    if n == 0:
        return []

    sorted_pv = sorted(p_values, key=lambda x: x[1])
    results = []
    for i, (label, p) in enumerate(sorted_pv):
        adjusted = p * (n - i)
        adjusted = min(adjusted, 1.0)
        results.append((label, adjusted, adjusted < 0.05))

    # Enforce monotonicity: each adjusted p >= previous
    for i in range(1, len(results)):
        prev_p = results[i - 1][1]
        if results[i][1] < prev_p:
            results[i] = (results[i][0], prev_p, prev_p < 0.05)

    return results


def _norm_cdf(z: float) -> float:
    """Standard normal CDF approximation (Abramowitz & Stegun)."""
    if z < -8:
        return 0.0
    if z > 8:
        return 1.0
    a1, a2, a3, a4, a5 = 0.254829592, -0.284496736, 1.421413741, -1.453152027, 1.061405429
    p = 0.3275911
    sign = 1 if z >= 0 else -1
    z_abs = abs(z)
    t = 1.0 / (1.0 + p * z_abs)
    y = 1.0 - (((((a5 * t + a4) * t) + a3) * t + a2) * t + a1) * t * math.exp(-z_abs * z_abs / 2)
    return 0.5 * (1 + sign * y)
