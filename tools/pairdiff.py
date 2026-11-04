#!/usr/bin/env python3
"""Compare the groups two (or more) games finally played, from their link traces. Standard library only.
Copyright (c) 2026 MatyMeatBoy. MIT License.

    python3 pairdiff.py host.log guest.log [--pattern REGEX] [--show 10]

Each game logs one line every time it plays a link frame's group, e.g.

    netlink: pair 1234  0acd 0010 0000 0000 | 0acd 0020 0000 0000

With rollback a frame is played several times (a guess first, then again with what really came), so only the
LAST line of each frame number counts: that is the group the game kept. The tool compares those final groups
frame by frame and prints where they differ and which word columns differ. A lasting desync shows up as
differences in a few columns only (e.g. the words that carry relayed data), long before a state hash says more
than "different".

--pattern must have two groups: the frame number and the rest of the line (words separated by spaces or '|').
Exit status: 0 when every frame logged by all games is equal, 1 when some differ, 2 on bad input.
"""
import argparse
import collections
import re
import sys

PATTERN = r'pair (\d+)\s+(.*?)\s*$'


def final_groups(lines, pattern=PATTERN):
    """frame -> tuple of words, the last line of each frame winning"""
    rx = re.compile(pattern)
    out = {}
    for line in lines:
        m = rx.search(line)
        if m:
            words = tuple(w.lower() for w in re.split(r'[\s|]+', m.group(2)) if w)
            out[int(m.group(1))] = words
    return out


def compare(logs):
    """logs: list of {frame: words}. Returns (common frames, [(frame, [words per log])], Counter of columns)."""
    common = sorted(set.intersection(*(set(g) for g in logs))) if logs else []
    diffs, columns = [], collections.Counter()
    for f in common:
        rows = [g[f] for g in logs]
        if any(r != rows[0] for r in rows[1:]):
            diffs.append((f, rows))
            width = max(len(r) for r in rows)
            for c in range(width):
                if len({r[c] if c < len(r) else None for r in rows}) > 1:
                    columns[c] += 1
    return common, diffs, columns


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('logs', nargs='+', help='two or more trace logs')
    ap.add_argument('--pattern', default=PATTERN)
    ap.add_argument('--show', type=int, default=10, help='differing frames to print')
    a = ap.parse_args(argv)
    if len(a.logs) < 2:
        ap.error('give at least two logs')
    try:
        groups = []
        for path in a.logs:
            with open(path, encoding='utf-8', errors='replace') as f:
                groups.append(final_groups(f, a.pattern))
    except (OSError, re.error, IndexError) as e:
        print(f'pairdiff: {e}', file=sys.stderr)
        return 2
    common, diffs, columns = compare(groups)
    for path, g in zip(a.logs, groups):
        print(f'{path}: {len(g)} frames')
    print(f'{len(common)} frames in all logs, {len(diffs)} differ')
    if diffs:
        print('differing word columns (0-based): ' + ', '.join(f'{c}: {n}' for c, n in sorted(columns.items())))
        print(f'first difference at frame {diffs[0][0]}')
        for f, rows in diffs[:a.show]:
            print(f'  {f}: ' + '  /  '.join(' '.join(r) for r in rows))
    return 1 if diffs else 0


if __name__ == '__main__':
    sys.exit(main())
