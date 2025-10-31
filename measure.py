#!/usr/bin/env python3
import os
import subprocess
import time
import statistics
import sys
from shutil import which

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

print("Starting measurements...")
for b in BUFFERS:
  for prog in PROGRAMS:
    for src in FILES:
      for i in range(MEASUREMENTS):
        dst = _make_dst(src, prog, b, i)
        # Build command
        if prog in ("./unixcopy", "./unixcopy-stdlib"):
          cmd = [prog, "-b", str(b), src, dst]
        else:  # cp
          cmd = ["cp", src, dst]
        try:
          t = _run_command(cmd)
        except subprocess.CalledProcessError as e:
          print(f"Command failed: {' '.join(cmd)} (run {i})", file=sys.stderr)
          results[b][prog][src].append(float("nan"))
        else:
          results[b][prog][src].append(t)
        # Cleanup: remove dst if exists
        try:
          if os.path.exists(dst):
            os.remove(dst)
        except Exception:
          pass

# Print results grouped by buffer size
def fmt(vals):
  # Format list of floats to 6 decimals, keep 'nan' as is
  return ", ".join(("nan" if (v != v) else f"{v:.6f}") for v in vals)

for b in BUFFERS:
  print("\n" + "="*80)
  print(f"Buffer size: {b} bytes")
  print("="*80)
  # Header: Program | File -> then measurements
  for prog in PROGRAMS:
    print(f"\nProgram: {prog}")
    for src in FILES:
      vals = results[b][prog][src]
      mean = statistics.mean([v for v in vals if v == v]) if any(v == v for v in vals) else float("nan")
      stdev = statistics.pstdev([v for v in vals if v == v]) if any(v == v for v in vals) else float("nan")
      print(f"  File: {src}")
      print(f"    runs: [{fmt(vals)}]")
      print(f"    mean: {mean:.6f} s    pstdev: {stdev:.6f} s")
print("\nMeasurements complete.")