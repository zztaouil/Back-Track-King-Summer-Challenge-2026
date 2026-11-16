#!/usr/bin/env python3
"""Replay one agent's logged stdout into the local referee. usage: replay.py LOG AGENT"""
import sys
log, agent = sys.argv[1], sys.argv[2]
outs, cur = [], None
for l in open(log):
    p = l.split()
    if l.startswith("frame "):
        cur = p[3] if len(p) > 3 else None
        if cur == agent: outs.append("WAIT")
    elif l.strip().startswith("stdout:") and cur == agent:
        outs[-1] = l.split("stdout:", 1)[1].strip()
rd = sys.stdin.readline
rd(); w = int(rd()); h = int(rd())
for _ in range(w * h): rd()
for _ in range(int(rd())): rd()
for o in outs:
    for _ in range(2 + w * h): rd()
    print(o, flush=True)
while rd(): print("WAIT", flush=True)
