# Experiments vs Boss 4

Every row: same 20 seeds (`data/seeds.txt`), our bot in seat 0 vs the Silver boss, through the
CodinGame test-game API (`tools/bench.py`). Raw rows in `data/results.jsonl`.
**margin** = (our points − boss points) / all points. Higher is better; > 0 means we outscore the boss.

| # | date | variant | change | wins | margin | verdict |
|---|---|---|---|---|---|---|
| 0 | 2026-11-16 | `baseline` (`src/bot.cpp` as submitted) | — | 3/20 | −0.102 | reference |
| 1 | 2026-11-16 | `variants/stick.cpp` | keep DISRUPTing last region until inked, while it still holds enemy value | 3/20 | −0.093 | **null**: paired diff −0.012 ±0.042, t −0.28 |
| 2 | 2026-11-16 | `variants/selfish.cpp` | paint value = myGain − 0.5·foeGain instead of margin (build even when it also pays the boss) | … | … | running |

## Notes

- 20 games ≈ 20 minutes (test games are rate-limited to 10/min; each takes ~60 s).
- Seat is always 0; boss in seat 1.
- **Seeds replay exactly**: same bot + same seed gave the same final score twice. So compare
  variants per seed with `tools/compare.py` (paired diff + t), not by win count.
- Noise: paired-diff SE ≈ 0.04 at n=20. An effect needs |diff| ≳ 0.08 to show at 20 games.

## Diagnostics (from the 20 stick logs, per-turn trace + referee metadata)

| | us | boss |
|---|---|---|
| income/turn, turns 0–5 | 3.0 | 2.4 |
| 5–10 | 22.1 | 24.2 |
| 10–20 | 47.5 | 59.6 |
| 20–30 | 60.7 | 65.6 |
| 30–45 | 46.7 | 57.1 |
| 45+ | 13.8 | 21.9 |
| tracks placed / game | 96.8 | 108.7 |
| ownership share of active paths | 0.40 | 0.50 |
| turns with 0 placements (20 games), turns 20–60 | 231 | 131 |

Reading: we **idle paint** in mid game while the boss keeps building. Likely cause: PLAN/SPOT only
take moves whose *margin* gain is > 0, so completions that also pay the boss are skipped, but the
boss would complete them anyway. Experiment 2 tests that.
