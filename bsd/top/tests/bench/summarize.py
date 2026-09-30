#!/usr/bin/env python3
"""Summarize captured native topbench output; no target or network access.

Usage: python3 summarize.py normal-interval.log short-interval.log
Each input file is a separate benchmark configuration, grouped by binary
path. Never silently pool different refresh intervals or workloads.
"""
import collections
import csv
import pathlib
import re
import statistics
import sys


def main(paths):
    groups = collections.defaultdict(list)
    invalid = []
    for name in paths:
        text = pathlib.Path(name).read_text(errors="replace")
        for line in text.splitlines():
            if not line.startswith("path="):
                continue
            row = dict(re.findall(r"(\w+)=([^ ]+)", line))
            path = row.pop("path")
            try:
                row = {key: int(value) for key, value in row.items()}
                required = ("status", "startup_us", "wall_us", "user_us",
                            "system_us", "rendered_frames", "output_bytes",
                            "data_stack_bytes")
                for key in required:
                    row[key]
                if row["status"] or row["startup_us"] < 0 or row["wall_us"] <= 0:
                    raise ValueError("failed run or no complete screen")
                if row.get("deadline_hit") or row.get("observer_failed"):
                    raise ValueError("benchmark deadline or observer failure")
                requested = row.get("forced_requested", 0)
                if requested and row.get("forced_refreshes") != requested:
                    raise ValueError("not all forced refreshes completed")
            except (ValueError, KeyError) as exc:
                invalid.append((name, path, str(exc)))
                continue
            row["startup_ms"] = row["startup_us"] / 1000
            row["cpu_ms"] = (row["user_us"] + row["system_us"]) / 1000
            row["cpu_percent"] = row["cpu_ms"] * 100000 / row["wall_us"]
            # Includes startup and teardown CPU. Visible output bursts cannot
            # serve as a sampling count: unchanged displays may emit nothing.
            if requested:
                row["cpu_ms_per_forced_refresh"] = row["cpu_ms"] / requested
            groups[name, path, requested].append(row)

    out = csv.writer(sys.stdout, lineterminator="\n")
    out.writerow(("configuration", "binary", "forced_requested", "runs", "startup_ms_min",
                  "startup_ms_median", "startup_ms_max", "cpu_ms_median",
                  "cpu_percent_median", "observed_output_bursts_median",
                  "data_stack_bytes_max",
                  "output_bytes_median", "cpu_ms_per_forced_refresh_median"))
    def med(rows, key):
        return round(statistics.median(r[key] for r in rows), 3)
    for (name, path, requested), rows in sorted(groups.items()):
        out.writerow((name, path, requested, len(rows),
                      min(r["startup_ms"] for r in rows),
                      med(rows, "startup_ms"),
                      max(r["startup_ms"] for r in rows),
                      med(rows, "cpu_ms"), med(rows, "cpu_percent"),
                      med(rows, "rendered_frames"),
                      max(r["data_stack_bytes"] for r in rows),
                      med(rows, "output_bytes"),
                      med(rows, "cpu_ms_per_forced_refresh") if requested else ""))
    for name, path, reason in invalid:
        print(f"Excluded failed measurement: {name}: {path}: {reason}",
              file=sys.stderr)
    if not groups:
        print("No successful native measurements found.", file=sys.stderr)
        return 1
    return bool(invalid)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("usage: summarize.py captured-log [captured-log ...]")
    sys.exit(main(sys.argv[1:]))
