#!/usr/bin/env python3
"""Local A/B on the real Java referee: both seatings per seed, parallel.

usage: match.py BOT_A BOT_B GAMES [JOBS] [SEED0]
BOT_* are executables (or shell commands). Prints A's wins and point margin.
Env A_ENV / B_ENV: extra env assignments for each bot, e.g. "W_X=3 W_Y=1".
"""
import os, random, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CP = ":".join(os.path.join(ROOT, "referee", p) for p in
              ("target/classes", "target/test-classes", "target/dependency/*"))
a, b, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
jobs = int(sys.argv[4]) if len(sys.argv) > 4 else 6
rng = random.Random(int(sys.argv[5]) if len(sys.argv) > 5 else 1)
seeds = [rng.randrange(-2**63, 2**63) for _ in range(n)]
# the runner splits commands on spaces: no empty tokens
cmdA = " ".join(["env"] + os.environ.get("A_ENV", "").split() + [a])
cmdB = " ".join(["env"] + os.environ.get("B_ENV", "").split() + [b])
TURN = os.environ.get("TURN_MS", "100")   # total allocated time must stay < 30 s


def game(seed, swap):
    p0, p1 = (cmdB, cmdA) if swap else (cmdA, cmdB)
    out = subprocess.run(["java", "-DturnMs=" + TURN, "-DfirstMs=2000", "-cp", CP, "Run", str(seed), p0, p1],
                         capture_output=True, text=True, cwd=ROOT).stdout.split()
    num = lambda v: int(v) if v.lstrip('-').isdigit() else -1   # 'null' = crashed
    s0, s1 = num(out[-2]), num(out[-1])
    return (s1, s0) if swap else (s0, s1)


tasks = [(s, w) for s in seeds for w in (0, 1)]
wins = pa = pb = dq = 0
with ThreadPoolExecutor(jobs) as ex:
    for (s, w), (x, y) in zip(tasks, ex.map(lambda t: game(*t), tasks)):
        wins += (x > y) + 0.5 * (x == y); pa += max(x, 0); pb += max(y, 0); dq += (x < 0) + (y < 0)
        print(s, w, x, y, flush=True) if os.environ.get("V") else None
g = len(tasks)
print("A %s vs B %s: wins %.1f/%d (%.1f%%)  pts %d-%d  margin %+.4f  dq %d" %
      (a, b, wins, g, 100 * wins / g, pa, pb, (pa - pb) / max(1, pa + pb), dq))
