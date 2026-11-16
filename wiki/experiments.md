# Experiments vs Boss 4

Every row: same 20 seeds (`data/seeds.txt`), our bot in seat 0 vs the Silver boss, through the
CodinGame test-game API (`tools/bench.py`). Raw rows in `data/results.jsonl`.
**margin** = (our points − boss points) / all points. Higher is better; > 0 means we outscore the boss.

| # | date | variant | change | wins | margin | verdict |
|---|---|---|---|---|---|---|
| 0 | 2026-11-16 | `baseline` (`src/bot.cpp` as submitted) | — | 3/20 | −0.102 | reference |
| 1 | 2026-11-16 | `variants/stick.cpp` | keep DISRUPTing last region until inked, while it still holds enemy value | … | … | running |

## Notes

- 20 games ≈ 20 minutes (test games are rate-limited to 10/min; each takes ~60 s).
- Seat is always 0; boss in seat 1.
