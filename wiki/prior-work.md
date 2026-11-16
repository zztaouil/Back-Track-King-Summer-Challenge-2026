# Prior work (before this repo)

The bot in `src/bot.cpp` ("Wormhole") came from an earlier local project at
`~/Workspace/summer-challenge-2026` (not in this repo): C++ arena + port of the referee's map
generator (`code/arena.cpp`, `code/sim.cpp`), sparring bots (`code/zoo.cpp`), SA tuner, a notes file
`docs/ideas.md` with 3 rounds of findings.

## What it established

- **Exact scorer**: `scoreBoard` reproduces the referee's BFS + N,E,S,W tie-break (self-test in `bot.cpp`, `-DPONY_SELFTEST`).
- **Harness lessons**: compare on paired seeds, both seatings, with point margin rather than win rate; fix wall-clock bail-outs (`W_TIME`) when benchmarking locally.
- **Only held-out win**: confluence routing (`W_TRUNK=8`, live map): +0.0058 margin, t +3.56 over 6000 games.
- **Measured null or worse** (self-play): sever horizon, length tax, corridor-theft flavour, observed hazard,
  unified DISRUPT ranking, fallback amortisation, rate exponent, per-region lifetime, jitter,
  depth-2 placement search, DISRUPT beam search (both under a biased harness, never re-tested).

## The blind spot

Every number was **self-play** or against weak zoo bots. Nothing was measured against the Silver boss,
which is the only opponent that matters for promotion. The current submission sits at Silver rank 10 of 727,
score 27.98 vs boss 29.75. This repo measures against the boss directly: see [experiments.md](experiments.md).
