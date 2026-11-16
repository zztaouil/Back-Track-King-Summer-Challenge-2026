# Back-Track-King-Summer-Challenge-2026

C++ bot for CodinGame's [Back Track King — Summer Challenge 2026](https://www.codingame.com/multiplayer/bot-programming/summer-challenge-2026-back-track-king).

## Goal & status

**Goal: reach Gold.** Currently Silver, rank 10/727 (score 27.98 vs Boss 4 at 29.75).
Head to head vs the boss: baseline wins 3/20 (margin −0.102).

## Wiki (progress notes, read these first)

- [wiki/log.md](wiki/log.md) — dated progress log, newest first
- [wiki/experiments.md](wiki/experiments.md) — every variant tried vs the boss, with results
- [wiki/game.md](wiki/game.md) — rules summary and measured game anatomy
- [wiki/bot.md](wiki/bot.md) — how the bot works
- [wiki/prior-work.md](wiki/prior-work.md) — findings from earlier local self-play rounds

## Layout

| path | what |
|---|---|
| `src/bot.cpp` | the bot that is (or will be) submitted |
| `variants/` | candidate bots under test, one idea each |
| `tools/bench.py` | play a bot vs the boss on fixed seeds through the CodinGame API |
| `data/seeds.txt`, `data/results.jsonl` | bench seeds and one row per game played |
| `ref/` | game statement and input stub |

```bash
g++ -std=c++17 -O2 -o /tmp/bot src/bot.cpp                          # build
g++ -std=c++17 -DPONY_SELFTEST -o /tmp/st src/bot.cpp && /tmp/st     # scorer self-test
LOGDIR=data/logs/x python3 tools/bench.py variants/x.cpp 20 data/results.jsonl
```

Needs a CodinGame API token in `~/.config/codingame/token` (never committed).
