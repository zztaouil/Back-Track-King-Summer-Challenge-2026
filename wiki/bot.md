# Bot architecture (`src/bot.cpp`)

C++17, one file, ~4–10 ms per turn of the 50 ms budget.

1. **Exact scorer** `scoreBoard`: per directed desired pair, BFS from the requesting town (N,E,S,W,
   visited-on-enqueue) and count each player's cells on the path = per-turn income. Every move is
   valued by simulating it on a board copy and rescoring.
2. **SPOT**: every empty cell with ≥2 passable neighbours, simulated; catches one-cell completions and
   tie-break steals.
3. **PLAN**: per directed pair, cheapest completion (Dijkstra; existing track free) in 3 flavours:
   through anything, avoiding enemy track, shortest-length avoiding enemy track. Routing step cost is
   discounted by **confluence** `CONFW` (share of pairs whose cheapest terrain corridor crosses the cell),
   inflated in regions with instability 2/3. Ranked by `gain · min(life, PAINT_LIFE) − eta / cost`.
4. Paint spent one cell at a time on the better of SPOT and PLAN rates. Plan cells bought from our end.
5. **DISRUPT**: simulate inking each legal region; best `swing·(T−need)/need` among regions that hurt the
   enemy's margin now; otherwise fallback to the region with the most enemy track.

Weights are compiled-in defaults overridable by env vars (`W_*`, `ABL` ablation bitmask) for local
tuning; CodinGame uses the defaults.

Debug: stderr line per turn `T<turn> me<score> foe<score> inc<my>/<foe>` (test-game logs only).
