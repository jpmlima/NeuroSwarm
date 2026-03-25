"""
TSV/CSV output formatters for pgfplots \addplot table compatibility.
"""

import csv
import io
from pathlib import Path


def write_tsv(path: str, headers: list[str], rows: list[tuple], delimiter="\t"):
    """Write a TSV/CSV file compatible with pgfplots \\addplot table."""
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", newline="") as f:
        w = csv.writer(f, delimiter=delimiter)
        w.writerow(headers)
        for row in rows:
            w.writerow(row)


def format_float(v, decimals=4):
    """Format float for TSV output."""
    if isinstance(v, float):
        return f"{v:.{decimals}f}"
    return str(v)
