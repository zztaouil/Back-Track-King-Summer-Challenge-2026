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
- Testing experiment 1 (sticky disrupt).
