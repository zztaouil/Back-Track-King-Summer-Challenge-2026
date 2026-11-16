# Progress log

Newest first.

## 2026-11-16

- Goal set: **reach Gold** (rank above Boss 4 in Silver; promotion mode `rank_above_boss`).
- Start state: Silver rank 10/727, score 27.98; Boss 4 rank 1, 29.75. Submission 41338219 (C++).
- Imported the submitted bot as `src/bot.cpp`; rules in `ref/`.
- Built `tools/bench.py` (fixed-seed games vs the boss via API).
- Baseline vs boss: **3/20 wins, margin −0.102**. The boss is clearly stronger head to head.
- Observed in a boss game log: boss prints `MESSAGE build a>b` / `steal a>b` — one pair at a time,
  plus explicit steals; it hits one region 4 turns straight until inked, while we alternate
  between regions. Boss also had fewer own tracks inked (14 vs 24) and higher ownership share of
  active paths (49% vs 40%).
- Added per-turn stderr trace to the bot (`T<turn> me foe inc a/f`).
- Exp 1 sticky disrupt: null (t −0.28).
- Verified seeds replay deterministically → paired per-seed comparison (`tools/compare.py`).
- Diagnostics: boss out-earns us mainly turns 10–20 and 30+; we place 12 fewer tracks/game and
  idle ~100 more turns across 20 games. See experiments.md "Diagnostics".
- Testing exp 2 (selfish paint value, λ=0.5).
