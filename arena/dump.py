#!/usr/bin/env python3
"""Bot that writes its init input to $DUMP and then WAITs."""
import os, sys
rd = sys.stdin.readline
lines = [rd(), rd(), rd()]
w, h = int(lines[1]), int(lines[2])
lines += [rd() for _ in range(w * h)]
n = rd(); lines.append(n); lines += [rd() for _ in range(int(n))]
open(os.environ["DUMP"], "w").write("".join(lines))
while rd():
    for _ in range(1 + w * h): rd()
    print("WAIT", flush=True)
