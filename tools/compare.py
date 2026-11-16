#!/usr/bin/env python3
"""Paired comparison of bench runs on shared seeds.

usage: tools/compare.py data/results.jsonl BOT_A BOT_B
Per seed: margin = (me - boss) / (me + boss). Prints mean margin of each, mean paired diff B-A, and t.
"""
import json, statistics as st, sys

rows = [json.loads(l) for l in open(sys.argv[1])]
a, b = sys.argv[2], sys.argv[3]
m = lambda r: (r["me"] - r["boss"]) / max(1, r["me"] + r["boss"])
A = {r["seed"]: m(r) for r in rows if r["bot"] == a}
B = {r["seed"]: m(r) for r in rows if r["bot"] == b}
seeds = [s for s in A if s in B]
d = [B[s] - A[s] for s in seeds]
se = st.stdev(d) / len(d) ** 0.5 if len(d) > 1 else float("nan")
print("n=%d  %s %.3f  %s %.3f  diff %+.3f +-%.3f  t %+.2f  better on %d/%d" % (
    len(d), a, st.mean(A[s] for s in seeds), b, st.mean(B[s] for s in seeds),
    st.mean(d), se, st.mean(d) / se if se else 0, sum(x > 0 for x in d), len(d)))
