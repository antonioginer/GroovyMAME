#!/usr/bin/env python3
import argparse
import math
import re
import numpy as np
import matplotlib.pyplot as plt

transformation = False

BLOCK_PATTERN = re.compile(r"\[(?P<body>[^\]]+)\]")
ITEM_PATTERN = re.compile(
    r"(?P<func>[a-zA-Z_]\w*)"
    r"(?:\("
        r"(?P<start>-?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)"
        r":"
        r"(?P<end>-?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)"
    r"\))?"
)

HEADER_RE = re.compile(r"EMUSYNC LOG \((\d+) items\): (.+)")
PAIR_RE = re.compile(r"([+-]?\d+\.\d+),([+-]?\d+\.\d+)")

def extract_block(text: str) -> str | None:
    """Extract content from [...]."""
    m = BLOCK_PATTERN.search(text)
    return m.group("body") if m else None

def parse_operations(body: str) -> list[dict]:
    """Convert operation strings into structured dicts."""
    ops = []
    for m in ITEM_PATTERN.finditer(body):
        func = m.group("func")
        if not func:
            continue

        start = m.group("start")
        end = m.group("end")

        ops.append(
            {
                "function": func,
                "range": None if start is None else (float(start), float(end)),
            }
        )

    return ops

def parse_text(text: str) -> list[dict] | None:
    """Full extract-and-parse pipeline."""
    body = extract_block(text)
    return parse_operations(body) if body else None

def parse_pairs(lines):
    """Parse x,y numeric pairs."""
    xs, ys = [], []
    for line in lines:
        m = PAIR_RE.search(line)
        if m:
            x, y = m.groups()
            xs.append(float(x))
            ys.append(float(y))
    return np.array(xs), np.array(ys)

def parse_emusync_sections(text: str) -> dict[str, tuple[np.ndarray, np.ndarray]]:
    """Parse EMUSYNC data sections from log text."""
    sections = {}
    current_name = None
    data_lines = []

    for line in text.splitlines():
        line = line.rstrip()

        # Start of section?
        m = HEADER_RE.match(line)
        if m:
            # Save previous section
            if current_name is not None:
                xs, ys = parse_pairs(data_lines)
                if xs.size:
                    sections[current_name] = (xs, ys)

            # Begin new section
            _, name = m.groups()
            current_name = name.strip()
            data_lines = []
            continue

        if current_name and line.strip():
            data_lines.append(line)

    # Save final section
    if current_name is not None:
        xs, ys = parse_pairs(data_lines)
        if xs.size:
            sections[current_name] = (xs, ys)

    return sections

def plot_sections(sections: dict[str, tuple[np.ndarray, np.ndarray]]):
    if not sections:
        print("No sections with data found.")
        return

    n = len(sections)
    max_rows = 3
    cols = math.ceil(n / max_rows)
    rows = min(n, max_rows)

    fig, axes = plt.subplots(rows, cols, figsize=(6 * cols, 3 * rows), squeeze=False)
    axes_flat = [ax for row in axes for ax in row]

    # Remove unused axes
    for ax in axes_flat[n:]:
        ax.remove()

    for i, (name, (xs, ys)) in enumerate(sections.items()):
        ax = axes_flat[i]
        parsed = parse_text(name)

        if parsed and transformation:
            for p in parsed:
                if p["function"] == "diff":
                    xs = xs[1:]
                    ys = np.diff(ys)

        ax.plot(xs, ys, marker=".", linestyle="-", linewidth=1)

        if parsed and transformation:
            for p in parsed:
                if p["function"] == "median" and p["range"]:
                    md = np.median(ys)
                    lo, hi = p["range"]
                    ax.set_ylim(md + lo, md + hi)
                if p["function"] == "ylim" and p["range"]:
                    lo, hi = p["range"]
                    ax.set_ylim(lo, hi)

        ax.set_title(name)
        ax.set_xlabel("time")
        ax.set_ylabel("value")
        ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.show()

def main():
    global transformation
    parser = argparse.ArgumentParser(description="Parse and plot EMUSYNC logs.")
    parser.add_argument(
        "-f", "--file", default="log.txt",
        help="Input log filename (default: log.txt)"
    )
    parser.add_argument("-n", "--notr", action="store_true")

    args = parser.parse_args()
    transformation = not args.notr

    try:
        with open(args.file, "r") as f:
            text = f.read()
    except OSError as e:
        print(f"Error: Cannot open file '{args.file}': {e}")
        return

    sections = parse_emusync_sections(text)
    plot_sections(sections)

if __name__ == "__main__":
    main()
