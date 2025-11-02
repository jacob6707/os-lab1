#!/usr/bin/env python3
import os
import subprocess
import time
import statistics
import sys
from shutil import which
import concurrent.futures
import threading

"""
measure.py

Measure copy times for three programs (./unixcopy, ./unixcopy-stdlib, cp)
over several source files and buffer sizes. Produces simple text tables
(grouped by buffer size) with 20 measurements each.

Usage: run from the repository root where source files and ./unixcopy exist:
  python3 measure.py
"""


# Configuration
PROGRAMS = ["./unixcopy", "./unixcopy-stdlib", "cp"]
FILES = ["file-1B.bin", "file-100MB.bin", "file-1GB.bin"]
BUFFERS = [1, 512, 1024]        # bytes to pass as -b to unixcopy variants
MEASUREMENTS = 20
TMP_DIR = "/tmp"

# Verify sources and programs
missing = [f for f in FILES if not os.path.exists(f)]
if missing:
  print("Missing source files:", ", ".join(missing), file=sys.stderr)
  sys.exit(1)

# Ensure cp exists
if which("cp") is None:
  print("cp command not found on PATH", file=sys.stderr)
  sys.exit(1)

# Data structure: results[buffer][program][file] = list of floats (seconds)
results = {b: {p: {f: [] for f in FILES} for p in PROGRAMS} for b in BUFFERS}

def _make_dst(src, prog, b, run_index):
  base = os.path.basename(src)
  prog_tag = prog.replace("./", "").replace("/", "_")
  return os.path.join(TMP_DIR, f"{base}.{prog_tag}.b{b}.run{run_index}")

def _run_command(cmd):
  start = time.perf_counter()
  subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
  end = time.perf_counter()
  return end - start

print("Starting measurements (multithreaded)...")

# Worker: run one copy and return (buffer, program, file, run_index, time_sec)
def _measure_task(b, prog, src, i):
  dst = _make_dst(src, prog, b, i)
  # Build command
  if prog in ("./unixcopy", "./unixcopy-stdlib"):
    cmd = [prog, "-b", str(b), src, dst]
  else:  # cp
    cmd = ["cp", src, dst]
  try:
    t = _run_command(cmd)
  except subprocess.CalledProcessError:
    t = float("nan")
  finally:
    # Cleanup: remove dst if exists
    try:
      if os.path.exists(dst):
        os.remove(dst)
    except Exception:
      pass
  return (b, prog, src, i, t)

# Submit tasks in parallel and collect results as they finish
max_workers = min(32, (os.cpu_count() or 1) * 4)
futures = []
lock = threading.Lock()
with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as ex:
  for b in BUFFERS:
    for prog in PROGRAMS:
      for src in FILES:
        for i in range(MEASUREMENTS):
          futures.append(ex.submit(_measure_task, b, prog, src, i))

  # As futures complete, append to results
  for fut in concurrent.futures.as_completed(futures):
    try:
      b, prog, src, i, t = fut.result()
    except Exception:
      # Shouldn't happen, but record a nan if it does
      continue
    # Append into results (list append is thread-safe in CPython, but use lock for clarity)
    with lock:
      results[b][prog][src].append(t)

# Print results grouped by buffer size
def _human_file_label(filename):
  # Map repository filenames to concise labels
  if "1B" in filename:
    return "1B"
  if "100MB" in filename:
    return "100MiB"
  if "1GB" in filename:
    return "1GiB"
  return filename

def _prog_label(prog):
  if prog == "./unixcopy":
    return "sistemski pozivi"
  if prog == "./unixcopy-stdlib":
    return "standardna biblioteka"
  if prog == "cp":
    return "cp komanda"
  return prog

def _format_series(vals):
  # vals: list of floats, may contain nan. Remove nan's for calculations.
  valid = [v for v in vals if v == v]
  if not valid:
    return {
      "excluded_min": float("nan"),
      "excluded_max": float("nan"),
      "mean": float("nan"),
      "rem_min": float("nan"),
      "rem_max": float("nan"),
      "delta_plus": float("nan"),
      "delta_minus": float("nan"),
    }
  # If at least 3 values, remove one global min and one global max as "izbačeni"
  if len(valid) >= 3:
    sorted_vals = sorted(valid)
    excluded_min = sorted_vals[0]
    excluded_max = sorted_vals[-1]
    remaining = sorted_vals[1:-1]
  else:
    # Not enough values to exclude, treat excluded as nan and compute on all
    excluded_min = float("nan")
    excluded_max = float("nan")
    remaining = valid
  if remaining:
    mean = statistics.mean(remaining)
    rem_min = min(remaining)
    rem_max = max(remaining)
    delta_plus = rem_max - mean
    delta_minus = mean - rem_min
  else:
    mean = float("nan")
    rem_min = float("nan")
    rem_max = float("nan")
    delta_plus = float("nan")
    delta_minus = float("nan")
  return {
    "excluded_min": excluded_min,
    "excluded_max": excluded_max,
    "mean": mean,
    "rem_min": rem_min,
    "rem_max": rem_max,
    "delta_plus": delta_plus,
    "delta_minus": delta_minus,
  }

def _format_python_list(vals):
  # Return a string that is a valid Python list literal.
  parts = []
  for v in vals:
    if v != v:  # nan
      parts.append("float('nan')")
    else:
      # Use 6 decimals for compactness
      parts.append(f"{v:.6f}")
  return "[" + ", ".join(parts) + "]"

for b in BUFFERS:
  print("\n" + "="*80)
  print(f"Buffer size: {b}B")
  print("="*80)
  for prog in PROGRAMS:
    print(f"\n{_prog_label(prog)}: ")
    for src in FILES:
      label = _human_file_label(src)
      series = results[b][prog][src]
      stats = _format_series(series)
      ex_min = stats["excluded_min"]
      ex_max = stats["excluded_max"]
      mean = stats["mean"]
      rem_min = stats["rem_min"]
      rem_max = stats["rem_max"]
      dp = stats["delta_plus"]
      dm = stats["delta_minus"]
      # Print the raw runs as a Python list literal
      print("")
      print(f"runs: {_format_python_list(series)}")
      print("")
      print(f"{label}:")
      if ex_min == ex_min and ex_max == ex_max:
        print(f"Izbačeni brojevi: min - {ex_min:.6f}s, max - {ex_max:.6f}s")
      else:
        print(f"Izbačeni brojevi: min - nan, max - nan")
      if mean == mean:
        # mean: show 7 decimals if possible
        print(f"Aritmetička sredina: {mean:.7f}s")
      else:
        print("Aritmetička sredina: nan")
      print("Maksimalne devijacije:")
      if dp == dp and dm == dm:
        # Show the subtraction expressions similar to sample
        print(f"Δ+ = {rem_max:.6f} - {mean:.7f} = {dp:.7f}")
        print(f"Δ− = {mean:.7f} - {rem_min:.6f} = {dm:.7f}")
      else:
        print("Δ+ = nan")
        print("Δ− = nan")
      print("-"*40)

print("\nMeasurements complete.")