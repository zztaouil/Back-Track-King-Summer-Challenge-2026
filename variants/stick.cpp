// Summer Challenge 2026 -- "Wormhole" rails bot.
//
// Everything hangs off one exact reimplementation of the referee's scorer:
// for each DIRECTED desired pair, BFS the shortest track/town path from the
// requesting town (neighbours in N,E,S,W order, visited-on-enqueue) and count
// the cells each player owns on it. That is literally the per-turn income, so
// the bot never guesses what a move is worth -- it plays the move on a copy of
// the board and reads the new income off the same function the referee uses.
//
// Per turn:
//   1. score the real board                              -> my/foe points per turn
//   2. SPOT moves: every empty cell with >=2 passable neighbours, simulated.
//      Catches "one cell completes a connection" and the corner-steals where a
//      new equal-length route wins the N/E/S/W tie-break off the enemy's track.
//   3. PLAN moves: per directed pair, cheapest completion (Dijkstra, existing
//      track/town free) in two flavours -- route-through-anything and
//      avoid-enemy-track -- each simulated as if finished. Routing is biased
//      toward CONFLUENCE: a cell is paid once per active connection it carries
//      and on-path cells carry 3.18 of them, so corridors several pairs want are
//      discounted and the first build is laid where the next ones reuse it.
//   Spend the 3 paint one cell at a time on whichever has the better rate,
//   points-turns per paint, so an immediate +1/turn can outbid a long build.
//   4. DISRUPT: simulate the ink-out of every legal region, rank by the swing
//      it denies the enemy, net of what it costs us and of their sunk paint we
//      wash away, amortised over the turns of instability still to buy.
//
// Three things shape where the paint lands. Routing is risk-weighted: roughly
// three quarters of all regions get inked over a game, so cells in regions the
// enemy is already destabilising are priced up. It is confluence-weighted, per
// above, against a map recomputed each turn over non-inked terrain -- 21 of 28
// pairs are unroutable by turn 40, and a startup-only map keeps steering paint
// down their dead corridors (per-turn refresh: +0.0065 +-0.0019 over the
// startup-only one, t +3.4). And plan cells are bought from OUR end of the
// corridor -- two players running similar planners would otherwise buy the same
// cell on the same turn, which makes it neutral and scores for nobody.
//
// ponytail: no tree search, and that is measured, not assumed. The bot uses
// ~4 ms of its 50 ms, so depth is affordable -- it just does not pay. Exact
// depth-2 placement search over adjacent pairs: 61-59. Beam search over
// DISRUPT sequences (depth 3 and 5, exact scorer at the leaves): 52-68 ranked
// by total value, 57-63 ranked by value per charge, and identical at both
// depths -- depth never changed the first move, so the kills decompose and
// 1-ply on rate is already right. What this game rewards is a correct value
// function, not lookahead; PAINT_LIFE is where that paid.
// Also measured against this baseline over paired-seed self-play (each map
// played both seatings) and REJECTED as no better: per-region expected
// lifetime replacing the PAINT_LIFE scalar (built, kept, shipped neutral --
// 475-524 over 999 held-out games once the model actually binds), asymmetric
// map-shrinking by option value (53-47, 51-49, 47-53, 38-62 as the weight
// rises -- code removed), anti-coordination jitter on contested cells (flat,
// kept as W_JITTER=0), two-step plan lookahead,
// pre-building bypasses around doomed regions, ranking plans by total value or
// cost-averse instead of by rate, valuing a sever by the enemy's rebuild time,
// spending idle disruption on map-shrinking, and discounting paint cost in
// town regions because they can never be inked (32-28). Re-measure before
// adding any of them back.
// Measured and rejected under the CORRECTED harness (paired point margin,
// GridMaker-matched maps, W_TIME, held-out blocks -- see docs/ideas.md):
// confluence applied to WHICH cell of a corridor to buy first (+0.0019 +-0.0022
// over 4 held-out blocks, 2/4 positive), and protecting our own planned corridor
// from our own DISRUPT -- measured first and dropped unbuilt, because the bot
// only inks a region its chosen route crosses on 1.8% of its disrupts.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <queue>
#include <sstream>
#include <string>
#include <vector>
#include <climits>
using namespace std;

static const int MAXTURNS = 100;
static const int PAINT = 3;          // paint points per turn, no carryover
static const int INK_AT = 4;         // instability that inks a region out
// How long a finished connection is actually worth paying for. NOT the turns
// left: measured over paired-seed self-play, 68% of all track placed is washed
// by inking and ~35 of ~40 desired pairs are unroutable by turn 40, so a build
// pays for a handful of turns, not eighty. Charging paint the full remaining
// horizon overpays slow plans against immediate steals. Tuned: 81-39 on
// held-out seeds, flat either side (12 -> 72-48, 8 -> 66-53).
static const int INF = 1e9;
// Weights the offline tuner sweeps (tools/tune.py). Compiled-in defaults are the
// shipping values; the env override exists so a search can try a vector without
// a rebuild. ponytail: getenv once at startup, not a config system.
static double Wt(const char* k, double d) { const char* v = getenv(k); return v ? atof(v) : d; }
static double W_PAINT_LIFE, W_HAZ_RATE, W_HAZ_BASE, W_HAZ_PATH, W_INST3, W_INST2, W_EDGE, W_JITTER;
static double W_SEVER_LIFE, W_LEN, W_STEAL, W_OBS, W_OBSDECAY, W_TIME;
// Ablation bitmask (env ABL), for measuring which subsystem carries the bot's
// strength rather than guessing. 0 = the shipping bot.
static double W_UNIFY, W_FBNEED, W_RATEPOW, W_TRUNK, W_TRUNKLIVE;
static int ABL;
enum { A_SPOT = 1, A_PLAN = 2, A_DISRUPT = 4, A_NOFOE = 8, A_RISK = 16,
       A_FALLBACK = 32, A_PAINTLIFE = 64, A_OWNEND = 128 };
static void loadWeights() {
    W_PAINT_LIFE = Wt("W_PAINT_LIFE", 10.67737407);
    // Charges/turn assumed to arrive board-wide. At 2 the implied lifetimes are
    // ~100 turns, so W_PAINT_LIFE binds and the bot behaves as it did before the
    // per-region model existed. That is deliberate: the model is CORRECT and it
    // does not pay. Raising it to 8 (lifetimes short enough to bind) measured
    // 475-524 over 999 held-out games -- see docs/ideas.md. Left in as the
    // tuner's most interesting axis, shipped neutral.
    W_HAZ_RATE   = Wt("W_HAZ_RATE", 4.180569863);
    W_HAZ_BASE   = Wt("W_HAZ_BASE", 0.1667293188);   // hazard share every live region gets
    W_HAZ_PATH   = Wt("W_HAZ_PATH", 2.684415126);   // extra share per on-path track cell
    W_INST3      = Wt("W_INST3", 12.13246367);  // route penalty at instability 3
    W_INST2      = Wt("W_INST2", 2.451089717);   // ... and at 2
    W_EDGE       = Wt("W_EDGE", 0.2010377853);   // enemy sunk paint washed by a sever
    // Anti-coordination, OFF by default: measured flat (49-51, 48-52, 50-50 at
    // 0.05/0.15/0.35) and worse at 0.7. Buying plan cells from our own end
    // already covers the collision. Left as a tunable, not as a default.
    W_JITTER     = Wt("W_JITTER", 0.01674951304);
    // How many more turns a sever is actually worth denying them. PAINT_LIFE is
    // this same correction for the other resource, and it is the only change
    // that ever paid. 100 = amortise over every turn to the horizon, which is
    // what the bot did before this weight existed and reproduces it exactly.
    W_SEVER_LIFE = Wt("W_SEVER_LIFE", 100);
    // Per-STEP tax in the routing Dijkstra, charged even on cells that are
    // already track and therefore free in paint. The referee pays the SHORTEST
    // path, not the cheapest one, so a cheap detour is undercuttable: finish it
    // and the enemy can still take the whole connection later by building
    // something shorter. This buys length back at a price. 0 = pure cheapest.
    W_LEN        = Wt("W_LEN", 1.774375742);
    // Enables a third routing flavour: minimum LENGTH avoiding enemy track,
    // rather than minimum paint. The referee pays whoever owns cells on the
    // SHORTEST path, so this is the only flavour that can take a connection the
    // enemy already holds -- the cheapest enemy-free route is normally longer
    // than theirs, scores nothing, and gets discarded before it is ever costed.
    // 0 = flavour off, which reproduces the two-flavour bot exactly.
    W_STEAL      = Wt("W_STEAL", 0.1706920764);
    // Observed hazard. The bot knows each region's instability LEVEL but has
    // never looked at its DERIVATIVE -- yet the enemy's charge is visible every
    // turn (the rise we did not cause), and a bot that spent a charge on a
    // region last turn is about to spend another. That is a direct observation
    // of the thing regLife() was estimating from a heat prior. Added to the
    // hazard, so 0 reproduces the prior-only bot exactly.
    W_OBS        = Wt("W_OBS", 1.002295638);
    W_OBSDECAY   = Wt("W_OBSDECAY", 0.5421640421);   // EWMA memory on that observation
    // Scales the search's wall-clock bail-outs. 1 is the real 50 ms budget. The
    // arena sets it huge, because otherwise the bot plays differently depending
    // on machine load: two runs of the SAME binary diverged on ~2% of maps, and
    // any variant that does more work per turn bails more often and loses for
    // that reason rather than on its merits.
    // Bail-outs stay at 30/35/40 ms of the 50 ms wall. Measured on an idle
    // machine, the worst turn is 9.6 ms WITH W_STEAL's third routing Dijkstra,
    // so they never bind here -- and tightening them would only start binding on
    // CodinGame's slower hardware, where the search is wanted most. (Turn times
    // read off a loaded machine are scheduling latency, not compute: the same
    // build measured 9.6 ms idle and 254 ms with the tuner on all cores.)
    W_TIME       = Wt("W_TIME",       1.0);
    // Merges the two-tier DISRUPT choice into one ranking. The old code picked
    // the best region whose ink strictly improves this turn's margin, and only
    // if there was none fell back to "most enemy paint in it" -- a tier that
    // ignores `need` entirely, so it charges a fresh region needing 4 hits over
    // one sitting at 3 needing 1. Ablation says that crude tier is worth 0.053
    // margin, nine times the whole risk-routing apparatus, so it is worth
    // getting right. 0 = the two-tier bot exactly.
    W_UNIFY      = Wt("W_UNIFY",      0.0);
    // Exponent on `need` in the FALLBACK tier only. Unifying the two tiers
    // measured worse (t -2.8), so the "improves the margin this turn" tier stays
    // a strict priority -- but within the fallback the old code ranked on raw
    // enemy paint and ignored cost, happily starting a fresh region needing four
    // charges over one already at three. 0 = that old behaviour.
    W_FBNEED     = Wt("W_FBNEED", 0.1645357105);
    // Exponent on paint cost when ranking plans. 1 = points-turns per paint,
    // the shipping rate. 0 = rank by total value, which docs/ideas.md lists as
    // measured-and-rejected -- but that measurement used a win rate at +-3.5%,
    // a map generator that did not match GridMaker, and a wall-clock bail-out
    // that penalised whichever candidate was slower. Re-measure before trusting.
    W_RATEPOW    = Wt("W_RATEPOW", 1);
    // Trunk bias in routing. cheapest() prices each directed pair on its own,
    // but a cell is paid once per ACTIVE CONNECTION it carries and on-path cells
    // carry 3.18 of them (measured, ARENA_DIAG). So the same paint is worth ~3x
    // on a corridor several pairs want, and the per-pair Dijkstra cannot see it:
    // it only gets the discount retroactively, when a LATER pair finds the track
    // already there and free. This discounts the routing step by the fraction of
    // desired pairs whose cheapest terrain corridor runs through the cell, so the
    // first corridor is laid where the next ones will reuse it. Selection only --
    // the plan is still PRICED at the paint we actually pay. 0 = per-pair routing,
    // which reproduces the old bot exactly.
    W_TRUNK      = Wt("W_TRUNK", 8);
    // Recompute the confluence map every turn over NON-INKED terrain, counting
    // only pairs that are still terrain-routable, instead of once at startup.
    // The static map goes stale hard: 21 of 28 pairs are unroutable by turn 40
    // (measured), and it keeps steering paint down their dead corridors.
    // 0 = the startup-only map.
    W_TRUNKLIVE  = Wt("W_TRUNKLIVE", 1);
    ABL = (int)Wt("ABL", 0);
}
// Referee neighbour order (Grid.ADJACENCY). The BFS tie-break rides on it.
static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};   // N E S W

static int W, H, N, NREG;
static vector<int>  REG, COST;       // region id / paint cost per cell
static vector<int>  PLANCOST;        // COST inflated where a region is close to inking
static vector<double> CONFW;         // share of desired pairs whose corridor crosses a cell
static vector<char> ISTOWN;
static vector<int>  townCell;                 // town id -> cell
static vector<int>  srcTowns;                 // towns that desire something
static vector<vector<int>> wantDst;           // town -> desired town ids
static vector<vector<int>> regCells;          // region -> its cells

static inline int cellOf(int x, int y) { return y * W + x; }

// ---------------------------------------------------------------------------
// Exact referee scorer.
// pass[c]: a train may cross c (it holds a town or a track). Inked regions have
// their track washed, so their cells simply stop being passable -- no special
// case needed here. own[c]: -1 none, 0/1 player, 2 neutral (scores for nobody).
// ---------------------------------------------------------------------------
struct Board {
    vector<char> pass;
    vector<signed char> own;
};

static vector<int> par_, seen_;
static int stamp_ = 0;

// Referee TrainBFS from `start`: neighbours N,E,S,W, visited on enqueue, so the
// parent chain of any reached cell IS the path the referee would pick.
static void bfsFrom(const Board& b, int start) {
    static vector<int> q;
    ++stamp_;
    q.clear(); q.push_back(start); seen_[start] = stamp_; par_[start] = -1;
    for (size_t h = 0; h < q.size(); ++h) {
        const int c = q[h], cx = c % W, cy = c / W;
        for (int k = 0; k < 4; k++) {
            const int nx = cx + DX[k], ny = cy + DY[k];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
            const int nc = ny * W + nx;
            if (seen_[nc] == stamp_ || !b.pass[nc]) continue;
            seen_[nc] = stamp_; par_[nc] = c; q.push_back(nc);
        }
    }
}

static void scoreBoard(const Board& b, int me, int& myPts, int& foePts) {
    myPts = foePts = 0;
    const int foe = 1 - me;
    for (int s : srcTowns) {
        bfsFrom(b, townCell[s]);
        for (int d : wantDst[s]) {
            const int t = townCell[d];
            if (seen_[t] != stamp_) continue;             // no path -> not active
            for (int c = t; c != -1; c = par_[c]) {
                if (b.own[c] == me) myPts++;
                else if (b.own[c] == foe) foePts++;
            }
        }
    }
}

// Our per-turn income minus theirs: the only number worth maximising.
static inline int margin(const Board& b, int me) {
    int a, f; scoreBoard(b, me, a, f); return a - f;
}

// ---------------------------------------------------------------------------
// Confluence: for each cell, the share of desired pairs that have a
// cheapest-terrain corridor through it. dist[t][c] is the paint to reach c from
// town t over bare terrain; c lies on a cheapest s->d corridor iff
// dist[s][c] + dist[d][c] - cost(c) <= dist[s][d], subtracting cost(c) because
// both halves charge for entering it. 12 Dijkstras at 12 towns: built at startup,
// and rebuilt with `blocked` set only on the turns the ink set changes, which is
// ~30 times a game rather than 100.
// ---------------------------------------------------------------------------
static void buildConfluence(int nTowns, const vector<char>* blocked = nullptr) {
    CONFW.assign(N, 0.0);
    vector<vector<int>> dist(nTowns, vector<int>(N, INF));
    for (int t = 0; t < nTowns; t++) {
        auto& dt = dist[t];
        priority_queue<pair<int,int>, vector<pair<int,int>>, greater<>> pq;
        dt[townCell[t]] = 0; pq.push({0, townCell[t]});
        while (!pq.empty()) {
            auto [d, c] = pq.top(); pq.pop();
            if (d != dt[c]) continue;
            const int cx = c % W, cy = c / W;
            for (int k = 0; k < 4; k++) {
                const int nx = cx + DX[k], ny = cy + DY[k];
                if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                const int nc = ny * W + nx;
                if (blocked && (*blocked)[nc]) continue;       // inked: no corridor here
                const int nd = d + (ISTOWN[nc] ? 0 : COST[nc]);
                if (nd < dt[nc]) { dt[nc] = nd; pq.push({nd, nc}); }
            }
        }
    }
    int pairs = 0;
    for (int s : srcTowns) for (int d : wantDst[s]) {
        const int D = dist[s][townCell[d]];
        if (D >= INF) continue;
        pairs++;
        for (int c = 0; c < N; c++) {
            if (dist[s][c] >= INF || dist[d][c] >= INF) continue;
            if (dist[s][c] + dist[d][c] - (ISTOWN[c] ? 0 : COST[c]) <= D) CONFW[c] += 1.0;
        }
    }
    // Normalised by pair count so one W_TRUNK means the same thing on a 4-town
    // map as on a 12-town one.
    if (pairs) for (int c = 0; c < N; c++) CONFW[c] /= pairs;
}

// ---------------------------------------------------------------------------
// Cheapest completion of from->to in paint. Existing passable cells cost 0
// (reusing anyone's track is free); inked cells and, optionally, enemy track
// are walls. Returns cost and the empty cells that must be bought.
// ---------------------------------------------------------------------------
struct Plan { int cost = INF; vector<int> build; };

static Plan cheapest(int from, int to, const Board& b, const vector<char>& blocked,
                     const vector<char>* wall, bool byLength = false) {
    static vector<int> dist, par;
    dist.assign(N, INF); par.assign(N, -1);
    priority_queue<pair<int,int>, vector<pair<int,int>>, greater<>> pq;
    dist[from] = 0; pq.push({0, from});
    while (!pq.empty()) {
        auto [d, c] = pq.top(); pq.pop();
        if (d != dist[c]) continue;
        if (c == to) break;
        const int cx = c % W, cy = c / W;
        for (int k = 0; k < 4; k++) {
            const int nx = cx + DX[k], ny = cy + DY[k];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
            const int nc = ny * W + nx;
            if (nc != to) {
                if (blocked[nc]) continue;                       // inked: unusable
                if (wall && (*wall)[nc]) continue;
            }
            // byLength: every cell on the path counts 1, free or not, so this
            // minimises what the referee actually measures.
            int step = byLength ? 8
                                : 8 * (b.pass[nc] ? 0 : PLANCOST[nc]) + (int)lround(8 * W_LEN);
            // Trunk discount (selection only; p.cost below still charges real
            // paint). Not applied to the by-length flavour, whose whole point is
            // to measure the referee's own metric and nothing else. Clamped
            // positive: Dijkstra needs non-negative steps.
            if (W_TRUNK > 0 && !byLength)
                step = max(1, step - (int)lround(8 * W_TRUNK * CONFW[nc]));
            if (dist[c] + step < dist[nc]) {
                dist[nc] = dist[c] + step; par[nc] = c; pq.push({dist[nc], nc});
            }
        }
    }
    Plan p;
    if (dist[to] >= INF) return p;
    for (int c = to; c != -1; c = par[c]) if (!b.pass[c]) p.build.push_back(c);
    reverse(p.build.begin(), p.build.end());
    // Route selection is risk-weighted, but the price we quote is the paint we
    // will actually pay -- eta and the rate are compared against spot moves.
    p.cost = 0;
    for (int c : p.build) p.cost += COST[c];
    if (p.cost == 0) p.cost = 1;
    return p;
}

#ifndef PONY_SELFTEST
int main() {
    int me;
    loadWeights();
    cin >> me >> W >> H;
    N = W * H;
    REG.assign(N, 0); COST.assign(N, 1); ISTOWN.assign(N, 0);
    par_.assign(N, -1); seen_.assign(N, 0);

    int maxReg = 0;
    for (int i = 0; i < N; i++) {
        int r, type; cin >> r >> type;
        REG[i] = r;
        COST[i] = (type == 2 ? 3 : type == 1 ? 2 : 1);   // mountain / river / plains
        maxReg = max(maxReg, r);
    }
    NREG = maxReg + 1;
    regCells.assign(NREG, {});
    for (int i = 0; i < N; i++) regCells[REG[i]].push_back(i);

    int nTowns; cin >> nTowns;
    townCell.assign(nTowns, -1); wantDst.assign(nTowns, {});
    vector<string> desired(nTowns);
    for (int i = 0; i < nTowns; i++) {
        int id, x, y; string d;
        cin >> id >> x >> y >> d;
        townCell[id] = cellOf(x, y); desired[id] = d;
    }
    for (int i = 0; i < nTowns; i++) {
        ISTOWN[townCell[i]] = 1;
        if (desired[i] == "x") continue;
        stringstream ss(desired[i]); string tok;
        while (getline(ss, tok, ',')) wantDst[i].push_back(stoi(tok));
        if (!wantDst[i].empty()) srcTowns.push_back(i);
    }
    buildConfluence(nTowns);
    // A region holding a town can never be inked (Game.doInstabilityCheck), so
    // DISRUPTing it is a wasted point.
    vector<char> regHasTown(NREG, 0);
    for (int i = 0; i < nTowns; i++) regHasTown[REG[townCell[i]]] = 1;

    const int foe = 1 - me;
    // Carried across turns so the enemy's charges can be differenced out of the
    // instability we see. myLast is what WE spent, so the rest is theirs.
    vector<int> prevInst(NREG, 0);
    vector<double> foeRate(NREG, 0.0);
    int myLast = -1;
    int lastInked = -1;                  // forces the first CONFW build on turn 0
    for (int turn = 0;; turn++) {
        int myScore, foeScore;
        if (!(cin >> myScore)) break;                    // EOF: local smoke runs
        cin >> foeScore;

        Board b; b.pass.assign(N, 0); b.own.assign(N, -1);
        vector<char> blocked(N, 0);                      // inked -> cannot build
        // One DISRUPT from being washed. Heavily penalised in routing rather
        // than fenced off outright: an opponent can park regions at 3 cheaply
        // and never finish, and a hard refusal would let them wall us out.
        vector<char> risky(N, 0);
        vector<int> regInst(NREG, 0);
        vector<char> regInked(NREG, 0);
        vector<double> regHeat(NREG, 0);      // track sitting on live connections
        for (int i = 0; i < N; i++) {
            int owner, inst, inked; string act;
            cin >> owner >> inst >> inked >> act;
            regInst[REG[i]] = inst;
            if (act != "x" && owner >= 0) regHeat[REG[i]] += 1;
            if (inked) { regInked[REG[i]] = 1; blocked[i] = 1; }
            if (owner >= 0) { b.own[i] = (signed char)owner; b.pass[i] = 1; }
            if (ISTOWN[i]) b.pass[i] = 1;
        }
        // Track bought in a region that inks is washed before it ever scores, so
        // route around regions the enemy is already inking, and refuse outright
        // to build in one that a single DISRUPT finishes off.
        PLANCOST.assign(N, 0);
        for (int i = 0; i < N; i++) {
            const int inst = regInst[REG[i]];
            PLANCOST[i] = (ABL & A_RISK) ? COST[i]
                : (int)(COST[i] * (regHasTown[REG[i]] ? 1.0 : inst >= 3 ? W_INST3 : inst >= 2 ? W_INST2 : 1.0));
            if (!(ABL & A_RISK) && !regHasTown[REG[i]] && inst >= INK_AT - 1) risky[i] = 1;
        }
        const auto t0 = chrono::steady_clock::now();
        auto elapsedMs = [&] {
            return chrono::duration_cast<chrono::milliseconds>(
                       chrono::steady_clock::now() - t0).count();
        };

        // Dead corridors stop counting: a pair whose terrain route is inked out is
        // no longer a reason to prefer a cell. 12 Dijkstras, ~0.3 ms.
        // Only when the ink set actually changed: CONFW is a pure function of it
        // (terrain and towns never move), and a region that inks can never come
        // back, so an unchanged COUNT means an unchanged set. ~30 rebuilds a game
        // instead of 100. Inside the turn clock on purpose -- it is the one
        // per-turn cost this bot added, so the bail-outs below must see it.
        int nInked = 0;
        for (int r = 0; r < NREG; r++) nInked += regInked[r];
        if (W_TRUNK > 0 && W_TRUNKLIVE > 0 && nInked != lastInked) {
            buildConfluence((int)townCell.size(), &blocked);
            lastInked = nInked;
        }

        // Enemy charge on r this turn = the rise, less our own contribution.
        if (turn) for (int r = 0; r < NREG; r++) {
            const int d = regInst[r] - prevInst[r] - (r == myLast ? 1 : 0);
            foeRate[r] = W_OBSDECAY * foeRate[r] + (1.0 - W_OBSDECAY) * (d > 0 ? 1.0 : 0.0);
        }
        prevInst = regInst;

        // Scorings still to come, this turn's included. T prices disruption,
        // which is permanent and really does pay to the end; TP prices paint,
        // which does not survive that long (see PAINT_LIFE).
        const int T = MAXTURNS - turn;
        const int TP = (ABL & A_PAINTLIFE) ? T : min((double)T, W_PAINT_LIFE);
        vector<string> out;

        // ------------------------------------------------------------------
        // How long paint survives, per region instead of as one constant.
        // A region inks after INK_AT charges; charges arrive at ~2/turn total
        // (both sides spend 1, no carryover) and are NOT spread evenly -- both
        // bots aim them at regions carrying live scoring track, which is
        // exactly what `partOfActiveConnections` reports. So split the observed
        // arrival rate across live regions in proportion to that heat, and read
        // the expected lifetime straight off the absorbing chain:
        //     life(r) = (INK_AT - instability(r)) / hazard(r).
        // Town regions can never ink, so they live to the horizon.
        // ------------------------------------------------------------------
        double heatSum = 0;
        for (int r = 0; r < NREG; r++)
            if (!regInked[r] && !regHasTown[r]) heatSum += W_HAZ_BASE + W_HAZ_PATH * regHeat[r];
        vector<double> regLife(NREG, T);
        for (int r = 0; r < NREG; r++) {
            if (regInked[r] || regHasTown[r] || heatSum <= 0) continue;
            const double haz = W_HAZ_RATE * (W_HAZ_BASE + W_HAZ_PATH * regHeat[r]) / heatSum
                             + W_OBS * foeRate[r];
            regLife[r] = haz > 0 ? min((double)T, (INK_AT - regInst[r]) / haz) : T;
        }
        // A corridor dies when ANY region on it inks, so hazards add: the
        // expected life of a set of cells is the harmonic-style combination
        // below, which collapses to regLife[r] for a single cell.
        auto lifeOf = [&](const vector<int>& cells) {
            double h = 0; int last = -1;
            for (int c : cells) {
                const int r = REG[c];
                if (r == last) continue;                 // paths run in region runs
                last = r; if (regLife[r] > 0) h += 1.0 / regLife[r];
            }
            return h > 0 ? min((double)TP, 1.0 / h) : (double)TP;
        };

        // ------------------------------------------------------------------
        // Paint. One cell at a time, best rate wins: SPOT (pays now) against
        // PLAN (pays once finished). Rate is points-turns per paint, so the two
        // are directly comparable.
        // ------------------------------------------------------------------
        int budget = PAINT;
        int base = margin(b, me);
        { int a, f; scoreBoard(b, me, a, f);
          cerr << "T" << turn << " me" << myScore << " foe" << foeScore << " inc" << a << "/" << f << endl; }
        vector<int> planCells;                           // cells we commit to this turn

        auto bestPlan = [&](double& rate, Plan& out_) {
            rate = -1e18;
            for (int s : srcTowns) for (int d : wantDst[s]) {
                const int nflav = (ABL & A_NOFOE) ? 1 : (W_STEAL > 0 ? 3 : 2);
                for (int flavour = 0; flavour < nflav; flavour++) {
                    // flavour 1 refuses to lean on enemy track: routing through
                    // it is free but pays them, so sometimes our own corridor
                    // is worth the paint.
                    vector<char> noFoe;
                    if (flavour) {
                        noFoe.assign(N, 0);
                        for (int i = 0; i < N; i++) if (b.own[i] == foe) noFoe[i] = 1;
                    }
                    Plan p = cheapest(townCell[s], townCell[d], b, blocked,
                                      flavour ? &noFoe : nullptr, flavour == 2);
                    if (p.cost >= INF || p.build.empty()) continue;   // already live
                    Board sim = b;
                    for (int c : p.build) { sim.pass[c] = 1; sim.own[c] = (signed char)me; }
                    const int gain = margin(sim, me) - base;
                    if (gain <= 0) continue;
                    const int eta = (p.cost + PAINT - 1) / PAINT;
                    const double pay = max(0.0, lifeOf(p.build) - eta);
                    const double r = gain * pay / pow((double)p.cost, W_RATEPOW);
                    if (r > rate) { rate = r; out_ = p; }
                }
                if (elapsedMs() > 30 * W_TIME) return;   // 50 ms wall; bail out early
            }
        };

        double planRate = -1e18; Plan plan;
        if (!(ABL & A_PLAN)) bestPlan(planRate, plan);

        while (budget > 0) {
            // SPOT: a cell can only sit on a path if it has two passable
            // neighbours, which prunes the frontier to a few dozen candidates.
            int spotCell = -1; double spotRate = 0;      // strictly-positive only
            // Anti-coordination. Two bots that rank cells identically buy the
            // same cell on the same turn and it goes neutral -- scoring for
            // nobody, paint burnt on both sides. With W_JITTER > 0 the pick is
            // drawn uniformly from every cell within that fraction of the best
            // rate, so the collision probability drops to 1/k instead of 1.
            vector<pair<double,int>> band; unsigned rng = 2166136261u ^ (turn * 2654435761u) ^ (me * 40503u);
            auto rnd = [&](int n) { rng = rng * 1664525u + 1013904223u; return (int)(rng % (unsigned)n); };
            for (int c = 0; c < N && !(ABL & A_SPOT) && elapsedMs() < 40 * W_TIME; c++) {
                if (b.pass[c] || blocked[c] || risky[c] || COST[c] > budget) continue;
                int nb = 0;
                const int cx = c % W, cy = c / W;
                for (int k = 0; k < 4; k++) {
                    const int nx = cx + DX[k], ny = cy + DY[k];
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                    if (b.pass[ny * W + nx]) nb++;
                }
                if (nb < 2) continue;
                Board sim = b;
                sim.pass[c] = 1; sim.own[c] = (signed char)me;
                const int gain = margin(sim, me) - base;
                if (gain <= 0) continue;
                const double r = (double)gain * lifeOf({c}) / pow((double)COST[c], W_RATEPOW);
                // Same anti-collision reasoning: split equal-rate ties by id.
                if (r > spotRate || (r == spotRate && spotCell >= 0 && (me == 0) == (c < spotCell)))
                    { spotRate = r; spotCell = c; }
                if (W_JITTER > 0) band.push_back({r, c});
            }
            if (W_JITTER > 0 && spotCell >= 0) {
                vector<int> keep;
                for (auto& [r, c] : band) if (r >= spotRate * (1.0 - W_JITTER)) keep.push_back(c);
                if (!keep.empty()) spotCell = keep[rnd((int)keep.size())];
            }

            int pick = -1;
            if (spotCell >= 0 && spotRate >= planRate) pick = spotCell;
            else {
                // Which cell of the plan to buy. Both players run similar
                // planners, and a cell they both buy on the same turn turns
                // neutral -- scoring for nobody, paint wasted on both sides. So
                // grow the line from OUR end: cells touching our own track
                // first, then a direction along the path that differs by player
                // id, which makes two mirrored bots build toward each other and
                // collide at most once instead of every turn.
                long bestKey = LONG_MIN;
                for (size_t i = 0; i < plan.build.size(); i++) {
                    const int c = plan.build[i];
                    if (b.pass[c] || blocked[c] || risky[c] || COST[c] > budget) continue;
                    int adj = 0;
                    const int cx = c % W, cy = c / W;
                    for (int k = 0; k < 4; k++) {
                        const int nx = cx + DX[k], ny = cy + DY[k];
                        if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                        if (b.own[ny * W + nx] == me) { adj = 1; break; }
                    }
                    const long key = (ABL & A_OWNEND) ? -(long)i
                                                      : adj * 4096L + (me == 0 ? -(long)i : (long)i);
                    if (key > bestKey || (W_JITTER > 0 && key == bestKey && rnd(2)))
                        { bestKey = key; pick = c; }
                }
            }
            if (pick < 0) break;

            out.push_back("PLACE_TRACKS " + to_string(pick % W) + " " + to_string(pick / W));
            budget -= COST[pick];
            b.pass[pick] = 1; b.own[pick] = (signed char)me;
            base = margin(b, me);
            planCells.push_back(pick);

            if (budget > 0 && !(ABL & A_PLAN) && elapsedMs() < 35 * W_TIME) bestPlan(planRate, plan);
            else break;
        }

        // ------------------------------------------------------------------
        // Disruption. 1 point/turn, no carryover, so it is spent whenever any
        // region is worth hitting. Value = the swing inking it produces, net of
        // our own losses, spread over the turns of instability still to buy.
        // ------------------------------------------------------------------
        // Track the enemy has already paid for and we can wash away, whether or
        // not it is scoring yet.
        auto edgeOf = [&](int r) {
            int e = 0;
            for (int c : regCells[r]) e += (b.own[c] == foe) - (b.own[c] == me);
            return e;
        };
        int target = -1; double bestVal = 0;
        int fallback = -1; double fallbackEdge = 0;
        bool lastOk = false;   // sticky: finish the region we started inking
        for (int r = 0; r < NREG; r++) {
            if (regInked[r] || regHasTown[r] || regInst[r] >= INK_AT) continue;
            const int need = INK_AT - regInst[r];
            // Measured: these games END at turn ~55, not 100, because the map
            // runs out of routable pairs. Amortising a sever over MAXTURNS-turn
            // prices it against turns that will never be played.
            const double TS = min((double)T, W_SEVER_LIFE);
            if (need >= TS) continue;                    // cannot finish in time
            bool ours = false;
            for (int c : regCells[r])
                if (find(planCells.begin(), planCells.end(), c) != planCells.end()) ours = true;
            if (ours) continue;                          // never ink what we just bought

            Board sim = b;
            for (int c : regCells[r]) { sim.pass[c] = 0; sim.own[c] = -1; }
            const double swing = base - margin(sim, me); // margin we keep by NOT inking
            if (r == myLast && (-swing > 0 || edgeOf(r) > 0)) lastOk = true;
            if (W_UNIFY > 0) {
                // One ranking for every region: what the sever denies over the
                // turns left, plus their sunk paint we wash away, per charge it
                // still costs. A region that scores nothing yet simply has
                // swing 0 and competes on its enemy paint alone -- which is the
                // old fallback, now correctly divided by `need`.
                const double val = (-swing * (TS - need) + W_EDGE * edgeOf(r)) / need;
                if (val > bestVal) { bestVal = val; target = r; }
                continue;
            }
            if (-swing > 0) {
                const double val = (-swing * (TS - need) + W_EDGE * edgeOf(r)) / need;
                if (val > bestVal) { bestVal = val; target = r; }
            }
            // Nothing scores here yet, but enemy track sitting in the region
            // will. Charge it up rather than waste the point.
            const double edge = edgeOf(r) / pow((double)need, W_FBNEED);
            if (!(ABL & A_FALLBACK) && edge > fallbackEdge) { fallbackEdge = edge; fallback = r; }
        }
        if (target < 0) target = fallback;
        if (lastOk) target = myLast;
        if (ABL & A_DISRUPT) target = -1;
        myLast = target;
        if (target >= 0) out.push_back("DISRUPT " + to_string(target));

        if (out.empty()) out.push_back("WAIT");
        for (size_t i = 0; i < out.size(); i++) cout << (i ? ";" : "") << out[i];
        cout << endl;
    }
}
#endif

#ifdef PONY_SELFTEST
// One runnable check on the part everything else is built from: that scoreBoard
// reproduces the referee's path choice, including the N,E,S,W tie-break that
// decides which player's track a connection actually pays.
//   g++ -std=c++17 -DPONY_SELFTEST -o /tmp/st code/main.cpp && /tmp/st
#include <cassert>
int main() {
    // 3x3, every cell passable, towns at 0 (NW) and 8 (SE). Two 5-cell routes
    // tie; the referee expands EAST before SOUTH, so the path is 0,1,2,5,8.
    W = H = 3; N = 9; NREG = 1;
    REG.assign(N, 0); COST.assign(N, 1); ISTOWN.assign(N, 0);
    par_.assign(N, -1); seen_.assign(N, 0);
    townCell = {0, 8}; ISTOWN[0] = ISTOWN[8] = 1;
    wantDst.assign(2, {}); wantDst[0] = {1}; srcTowns = {0};

    // The two routes are owned by opposite players, so picking the wrong one
    // inverts the score completely.
    Board b; b.pass.assign(N, 1); b.own.assign(N, -1);
    for (int c : {1, 2, 5}) b.own[c] = 0;               // east route: ours
    for (int c : {3, 6, 7}) b.own[c] = 1;               // south route: theirs
    int mine, foe;
    scoreBoard(b, 0, mine, foe);
    assert(mine == 3 && foe == 0);   // south-first would have read 0 and 3

    // Cut the east route at cell 2 and the path swings to 0,1,4,5,8.
    b.pass[2] = 0; b.own[4] = 1;
    scoreBoard(b, 0, mine, foe);
    assert(mine == 2 && foe == 1);   // 1 and 5 ours, 4 theirs

    // cheapest(): existing track is free, empty cells cost terrain, walls block.
    W = 3; H = 1; N = 3; NREG = 2;
    REG = {0, 1, 0}; COST = {1, 2, 1}; ISTOWN = {1, 0, 1};
    PLANCOST = COST;
    par_.assign(N, -1); seen_.assign(N, 0);
    Board l; l.pass = {1, 0, 1}; l.own.assign(3, -1);
    vector<char> clear(N, 0);
    Plan p = cheapest(0, 2, l, clear, nullptr);
    assert(p.cost == 2 && p.build.size() == 1 && p.build[0] == 1);   // river costs 2

    l.pass[1] = 1;                                       // already track -> free
    assert(cheapest(0, 2, l, clear, nullptr).build.empty());

    l.pass[1] = 0;
    vector<char> blocked(N, 0); blocked[1] = 1;          // inked -> unreachable
    assert(cheapest(0, 2, l, blocked, nullptr).cost >= INF);

    // buildConfluence(): 3x3 plains, towns at 0 (NW) and 2 (NE), 0 wants 2. The
    // only cheapest corridor is 0-1-2, so those three cells carry the single pair
    // and nothing else does -- the centre (4) is a 2-cell detour and must read 0.
    W = H = 3; N = 9; NREG = 1;
    REG.assign(N, 0); COST.assign(N, 1); ISTOWN.assign(N, 0);
    townCell = {0, 2}; ISTOWN[0] = ISTOWN[2] = 1;
    wantDst.assign(2, {}); wantDst[0] = {1}; srcTowns = {0};
    buildConfluence(2);
    assert(CONFW[0] == 1.0 && CONFW[1] == 1.0 && CONFW[2] == 1.0);
    for (int c : {3, 4, 5, 6, 7, 8}) assert(CONFW[c] == 0.0);

    // Ink cell 1 and the corridor has to go the long way, 0-3-4-5-2: the centre
    // now carries the pair and the old corridor cell does not. This is the whole
    // point of rebuilding the map when a region inks.
    vector<char> ink(N, 0); ink[1] = 1;
    buildConfluence(2, &ink);
    assert(CONFW[4] == 1.0 && CONFW[3] == 1.0 && CONFW[5] == 1.0);
    assert(CONFW[1] == 0.0 && CONFW[7] == 0.0);

    puts("ok");
}
#endif

