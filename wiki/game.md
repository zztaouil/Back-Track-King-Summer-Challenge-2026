# Game: Back Track King (Summer Challenge 2026)

Full statement: [../ref/statement.md](../ref/statement.md). Input format: [../ref/stub.txt](../ref/stub.txt).
Referee source: https://github.com/CGjupoulton/SummerChallenge2026

## Rules in one screen

- Grid 21–30 × 14–20 (CodinGame maps use `W = round(1.5·H)`). Cells: plains (1 paint), river (2), mountain (3).
- Grid split into **regions**. 4–12 towns, one per region, never in adjacent regions.
- Each town has `desiredConnections` (one-way). Each desired pair with a track/town path → **active connection** = the shortest
  path, ties broken by N, E, S, W from the requesting town.
- End of each turn: every active connection pays each player **1 point per own track on that path**. Neutral tracks (both placed same cell same turn) pay nobody.
- Per turn: **3 paint** (no carryover) and **1 DISRUPT** (+1 instability to a region). Instability 4 → region **inked**: tracks washed, no more building. Town regions cannot be disrupted.
- 100 turns max; game also ends when no desired pair can ever be connected. Most points wins.
- Order: placements, then disrupts, then ink-out, then scoring.
- 50 ms per turn, 1000 ms first turn.

## Measured anatomy (self-play, from prior rounds)

- Games end ~turn 55: maps run out of routable pairs.
- An on-path cell sits on ~3.2 active connections, so trunk cells are worth ~3× a leaf cell.
- ~68% of placed track gets washed by inking.
- Ablation: PLAN (route completion) and DISRUPT carry almost all of the value; hazard/instability weights ~0.6%.

## Leagues

- Current: **Silver**. Promotion mode: `rank_above_boss` (Boss 4, agent 6767608).
- Stub mentions terrain type 3 = POI and the log metadata has `poisServed`: Gold probably adds points of interest. Read the statement again after promotion.
