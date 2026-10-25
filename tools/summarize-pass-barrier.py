#!/usr/bin/env python3
"""Summarize one renderer's coarse pass-barrier A/B, never mixed launches."""
import argparse
from datetime import datetime
import json
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--experiment", choices=["pass-barrier", "unused-depth"], default="pass-barrier")
parser.add_argument("log", type=Path, help="captured performance.log (include rotations if needed)")
parser.add_argument("--tid", required=True, type=int, help="renderer TID from this launch")
parser.add_argument("--since", required=True, type=datetime.fromisoformat, help="launch time, phone local ISO time")
parser.add_argument("--sample-after", required=True, type=datetime.fromisoformat, help="time when Options/Camera is stable, phone local ISO time")
parser.add_argument("--until", type=datetime.fromisoformat, help="optional end of this launch")
parser.add_argument("--warmup", type=float, default=15, help="exclude report ends within this many seconds of each mode marker")
args = parser.parse_args()
if args.warmup < 10:
    parser.error("--warmup must be >=10 seconds to exclude mixed five-second windows")
if args.sample_after < args.since or (args.until and args.sample_after > args.until):
    parser.error("--sample-after must be inside the launch interval")

line_re = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+) .*?\[tid:(\d+)\].*?\[(GPU5|VKBARRIER|VKDEPTH)\] (.*)$")
field_re = re.compile(r"(\w+)=([\d.]+)")
mode_tag, mode_key = ("VKBARRIER", "scoped_pass") if args.experiment == "pass-barrier" else ("VKDEPTH", "omit_unused")
segments = []
segment = None
last_time = None
for line in args.log.read_text().splitlines():
    match = line_re.match(line)
    if not match or int(match[2]) != args.tid:
        continue
    when = datetime.fromisoformat(match[1])
    if when < args.since or (args.until and when > args.until):
        continue
    if last_time is not None and when < last_time:
        parser.error("input must be chronological; order rotated logs before the current file")
    last_time = when
    values = {k: float(v) for k, v in field_re.findall(match[4])}
    if match[3] != "GPU5" and match[3] != mode_tag:
        continue
    if match[3] == mode_tag:
        segment = {"mode": int(values[mode_key]), "marker": when,
                   "windows": 0, "frames": 0, "weighted": {}, "skipped": 0}
        segments.append(segment)
        continue
    if segment is None or (when - segment["marker"]).total_seconds() < args.warmup:
        continue
    # GPU5 ends a five-second aggregate. Drop the first such interval after
    # scene warmup, so loading/menu navigation cannot enter the baseline.
    if (when - args.sample_after).total_seconds() < 5:
        continue
    required = {"frames", "game_ms", "present_ms", "passes", "full_barriers",
                "pass_barriers", "blits", "guest_draws", "guest_indices"}
    if args.experiment == "unused-depth":
        required |= {"depth_passes", "depth_load_mpix", "depth_store_mpix"}
    if not required <= values.keys():
        parser.error("GPU5 is from an older build: missing barrier/draw counters")
    # Exclude mixed transition reports even when their mode marker was missed.
    if args.experiment == "pass-barrier" and ((segment["mode"] == 0 and values["pass_barriers"] != 0) or (
        segment["mode"] == 1 and values["pass_barriers"] == 0
    )):
        segment["skipped"] += 1
        continue
    frames = int(values["frames"])
    if not frames:
        continue
    segment["windows"] += 1
    segment["frames"] += frames
    for key in required - {"frames"}:
        segment["weighted"][key] = segment["weighted"].get(key, 0) + values[key] * frames

result = []
for item in segments:
    row = {key: item[key] for key in ("mode", "windows", "frames", "skipped")}
    row["marker"] = item["marker"].isoformat()
    if item["frames"]:
        row.update({k: round(v / item["frames"], 4) for k, v in sorted(item["weighted"].items())})
        row["total_ms"] = round(row["game_ms"] + row["present_ms"], 4)
        row["barriers_total"] = round(row["full_barriers"] + row["pass_barriers"], 4)
    result.append(row)
if not result:
    parser.error("no mode markers for the requested renderer and launch interval")
print(json.dumps({"experiment": args.experiment, "renderer_tid": args.tid, "since": args.since.isoformat(),
                  "sample_after": args.sample_after.isoformat(),
                  "warmup_seconds": args.warmup, "segments": result}, indent=2))
