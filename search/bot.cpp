// Back Track King -- simulation-based search bot.
//
// 1. Exact engine: a C++ copy of the referee's turn (placements -> neutral on
//    collision, disrupts, ink-out, BFS scoring with N,E,S,W tie-break, game end
//    when no desired pair is terrain-routable). Validated against Game.java.
// 2. Plans (macro-actions) for BOTH players: per directed pair, a few routing
//    flavours (cheapest / avoid enemy / shortest / shortest avoiding enemy),
//    plus one-cell spot moves, each paired with a DISRUPT target that is hit
//    every turn until it inks.
// 3. Simultaneous-move search: payoff matrix my-plan x foe-plan, each entry a
//    rollout of D turns where both sides keep executing their plan, valued as
//    point margin + R * income margin at the leaf. Solved by regret matching;
//    we play the best response to the foe's equilibrium mix.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <queue>
#include <sstream>
#include <string>
#include <vector>
#include <climits>
using namespace std;

static double Wt(const char* k, double d) { const char* v = getenv(k); return v ? atof(v) : d; }
static int    D_DEPTH, B_BUILDS, Z_DISRUPTS, MAXCAND;
static double R_LEAF, L_PRIOR, W_RISK3, W_RISK2, W_WIN, W_UNIF, TIME_MS;

static const int MAXN = 30 * 20, MAXT = 12, INK_AT = 4, PAINT = 3, MAXTURNS = 100;
static int W, H, N, NZ, NT;
static int zoneOf[MAXN], costOf[MAXN], townAt[MAXN], nb[MAXN][4];
static int townCell[MAXT];
static vector<int> want[MAXT], srcs, zoneCells[MAXN];
static bool zoneHasTown[MAXN];

struct State {
    int8_t track[MAXN];      // -1 none, 0/1 player, 2 neutral
    uint8_t inst[MAXN];      // per zone
    uint8_t inked[MAXN];     // per zone
    int score[2], turn;
    bool over;
};

static inline bool passable(const State& s, int c) { return townAt[c] >= 0 || s.track[c] >= 0; }

// ---------------------------------------------------------------- scoring
static int bfsPar[MAXN], bfsSeen[MAXN], bfsStamp = 0, bfsQ[MAXN];
static void bfs(const State& s, int start) {
    ++bfsStamp;
    int h = 0, t = 0;
    bfsQ[t++] = start; bfsSeen[start] = bfsStamp; bfsPar[start] = -1;
    while (h < t) {
        int c = bfsQ[h++];
        for (int k = 0; k < 4; k++) {
            int n = nb[c][k];
            if (n < 0 || bfsSeen[n] == bfsStamp || !passable(s, n)) continue;
            bfsSeen[n] = bfsStamp; bfsPar[n] = c; bfsQ[t++] = n;
        }
    }
}
// Per-turn income of each player.
static void income(const State& s, int inc[2]) {
    inc[0] = inc[1] = 0;
    for (int a : srcs) {
        bfs(s, townCell[a]);
        for (int d : want[a]) {
            int t = townCell[d];
            if (bfsSeen[t] != bfsStamp) continue;
            for (int c = t; c != -1; c = bfsPar[c]) {
                int o = s.track[c];
                if (o == 0) inc[0]++; else if (o == 1) inc[1]++;
            }
        }
    }
}
// Same, plus per-zone income lost by each player if that zone vanished
// (approximation: the whole connection is lost).
static void incomeByZone(const State& s, int inc[2], vector<int>& zl0, vector<int>& zl1) {
    inc[0] = inc[1] = 0;
    zl0.assign(NZ, 0); zl1.assign(NZ, 0);
    static int mark[MAXN]; static int ms = 0;
    for (int a : srcs) {
        bfs(s, townCell[a]);
        for (int d : want[a]) {
            int t = townCell[d];
            if (bfsSeen[t] != bfsStamp) continue;
            int p0 = 0, p1 = 0;
            for (int c = t; c != -1; c = bfsPar[c]) { int o = s.track[c]; p0 += o == 0; p1 += o == 1; }
            inc[0] += p0; inc[1] += p1;
            ++ms;
            for (int c = t; c != -1; c = bfsPar[c]) {
                int z = zoneOf[c];
                if (zoneHasTown[z] || mark[z] == ms) continue;
                mark[z] = ms; zl0[z] += p0; zl1[z] += p1;
            }
        }
    }
}

// Any desired pair still routable over non-inked terrain?
static bool anyAlive(const State& s) {
    static int seen[MAXN], st = 0, q[MAXN];
    for (int a : srcs) {
        ++st; int h = 0, t = 0; q[t++] = townCell[a]; seen[townCell[a]] = st;
        while (h < t) {
            int c = q[h++];
            for (int k = 0; k < 4; k++) {
                int n = nb[c][k];
                if (n < 0 || seen[n] == st || s.inked[zoneOf[n]]) continue;
                seen[n] = st; q[t++] = n;
            }
        }
        for (int d : want[a]) if (seen[townCell[d]] == st) return true;
    }
    return false;
}

// ---------------------------------------------------------------- engine
struct Act { int n = 0; int16_t cells[8]; int dz = -1; };

static int placedStamp[2][MAXN], plStamp = 0;
static void step(State& s, const Act* a) {
    ++plStamp;
    int pl[2][8], np[2] = {0, 0};
    for (int p = 0; p < 2; p++) {
        int dosh = PAINT;
        for (int i = 0; i < a[p].n; i++) {
            int c = a[p].cells[i];
            if (townAt[c] >= 0 || s.inked[zoneOf[c]] || s.track[c] >= 0 || placedStamp[p][c] == plStamp) continue;
            if (costOf[c] > dosh) continue;
            dosh -= costOf[c]; placedStamp[p][c] = plStamp; pl[p][np[p]++] = c;
        }
    }
    for (int p = 0; p < 2; p++)
        for (int i = 0; i < np[p]; i++) {
            int c = pl[p][i];
            s.track[c] = placedStamp[1 - p][c] == plStamp ? 2 : p;
        }
    bool inkedNow = false;
    for (int p = 0; p < 2; p++) {
        int z = a[p].dz;
        if (z < 0 || z >= NZ || s.inked[z] || zoneHasTown[z]) continue;
        s.inst[z]++;
    }
    for (int p = 0; p < 2; p++) {
        int z = a[p].dz;
        if (z < 0 || z >= NZ || s.inked[z] || zoneHasTown[z] || s.inst[z] < INK_AT) continue;
        s.inked[z] = 1; inkedNow = true;
        for (int c : zoneCells[z]) s.track[c] = -1;
    }
    int inc[2]; income(s, inc);
    s.score[0] += inc[0]; s.score[1] += inc[1];
    s.turn++;
    if (s.turn >= MAXTURNS || (inkedNow && !anyAlive(s))) s.over = true;
}

// ---------------------------------------------------------------- plans
struct Plan {
    vector<int> cells;   // cells to buy, in build order
    int dz = -1;         // zone to hit every turn until inked
    double prior = 0;
    int paint = 0;
};

// Next turn's action of a plan on state s; advances idx past cells now taken.
static Act planAct(const Plan& P, size_t& idx, const State& s) {
    Act a; int dosh = PAINT;
    size_t i = idx;
    while (i < P.cells.size()) {
        int c = P.cells[i];
        if (s.track[c] >= 0) { i++; if (a.n == 0) idx = i; continue; }
        if (s.inked[zoneOf[c]]) { idx = P.cells.size(); a.n = 0; break; }   // plan dead
        if (costOf[c] > dosh || a.n >= 8) break;
        dosh -= costOf[c]; a.cells[a.n++] = c; i++;
    }
    a.dz = (P.dz >= 0 && !s.inked[P.dz]) ? P.dz : -1;
    return a;
}

// Dijkstra s->d for player p. flavour bit0: avoid enemy track; bit1: shortest length first.
static bool route(const State& s, int p, int from, int to, int flav, vector<int>& out, int& paint) {
    static long long dist[MAXN]; static int par[MAXN];
    for (int i = 0; i < N; i++) dist[i] = LLONG_MAX;
    typedef pair<long long, int> QI;
    priority_queue<QI, vector<QI>, greater<QI>> pq;
    dist[from] = 0; pq.push({0, from});
    const long long BIG = 1 << 12;
    while (!pq.empty()) {
        auto [dd, c] = pq.top(); pq.pop();
        if (dd != dist[c]) continue;
        if (c == to) break;
        for (int k = 0; k < 4; k++) {
            int n = nb[c][k];
            if (n < 0) continue;
            long long paintStep, lenStep = 1;
            if (n == to) paintStep = 0;
            else if (townAt[n] >= 0) paintStep = 0;
            else if (s.track[n] >= 0) {
                if ((flav & 1) && s.track[n] == 1 - p) continue;
                paintStep = 0;
            } else {
                int z = zoneOf[n];
                if (s.inked[z]) continue;
                paintStep = costOf[n] * 4;
                if (!zoneHasTown[z]) paintStep += s.inst[z] >= 3 ? (long long)W_RISK3 : s.inst[z] == 2 ? (long long)W_RISK2 : 0;
            }
            long long step = (flav & 2) ? lenStep * BIG + paintStep : paintStep * BIG + lenStep;
            if (dd + step < dist[n]) { dist[n] = dd + step; par[n] = c; pq.push({dist[n], n}); }
        }
    }
    if (dist[to] == LLONG_MAX) return false;
    out.clear(); paint = 0;
    for (int c = par[to]; c != from && c >= 0; c = par[c])
        if (!passable(s, c)) { out.push_back(c); paint += costOf[c]; }
    reverse(out.begin(), out.end());
    return true;
}

static inline int marginOf(const int inc[2], int p) { return inc[p] - inc[1 - p]; }

// Build + disrupt candidates for player p.
static vector<Plan> genPlans(const State& s, int p, int B, int Z) {
    int inc0[2]; vector<int> zl0, zl1;
    incomeByZone(s, inc0, zl0, zl1);
    const int base = marginOf(inc0, p);

    // ---- builds
    vector<Plan> builds;
    auto addBuild = [&](vector<int>& cells, int paint) {
        if (cells.empty()) return;
        for (auto& b : builds) if (b.cells == cells) return;
        State t = s;
        for (int c : cells) t.track[c] = p;
        int inc[2]; income(t, inc);
        int gain = marginOf(inc, p) - base;
        if (gain <= 0) return;
        int eta = (paint + PAINT - 1) / PAINT;
        Plan P; P.cells = cells; P.paint = paint;
        P.prior = gain * max(0.5, L_PRIOR - eta) / max(1, paint);
        builds.push_back(P);
    };
    vector<int> cells; int paint;
    for (int a : srcs) for (int d : want[a])
        for (int f = 0; f < 4; f++)
            if (route(s, p, townCell[a], townCell[d], f, cells, paint)) addBuild(cells, paint);
    // spot: single cells with >= 2 passable neighbours
    for (int c = 0; c < N; c++) {
        if (passable(s, c) || s.inked[zoneOf[c]]) continue;
        int k = 0; for (int j = 0; j < 4; j++) if (nb[c][j] >= 0 && passable(s, nb[c][j])) k++;
        if (k < 2) continue;
        vector<int> one{c}; addBuild(one, costOf[c]);
    }
    sort(builds.begin(), builds.end(), [](const Plan& x, const Plan& y) { return x.prior > y.prior; });
    if ((int)builds.size() > B) builds.resize(B);
    if (builds.empty()) builds.push_back(Plan());

    // ---- disrupts: value = enemy income on connections through the zone minus ours, per hit needed
    const vector<int>& mine = p == 0 ? zl0 : zl1;
    const vector<int>& theirs = p == 0 ? zl1 : zl0;
    vector<pair<double, int>> dz;
    for (int z = 0; z < NZ; z++) {
        if (s.inked[z] || zoneHasTown[z]) continue;
        int need = INK_AT - s.inst[z];
        int et = 0, mt = 0;
        for (int c : zoneCells[z]) { et += s.track[c] == 1 - p; mt += s.track[c] == p; }
        double v = (theirs[z] - mine[z]) * 3.0 + 0.3 * (et - mt);
        if (v <= 0) continue;
        dz.push_back({v / need, z});
    }
    sort(dz.rbegin(), dz.rend());
    vector<int> zs;
    for (int i = 0; i < (int)dz.size() && i < Z; i++) zs.push_back(dz[i].second);
    if (zs.empty()) zs.push_back(-1);

    vector<Plan> out;
    for (auto& b : builds) for (int z : zs) { Plan P = b; P.dz = z; out.push_back(P); }
    return out;
}

// ---------------------------------------------------------------- search
static double rollout(const State& root, int me, const Plan& mp, const Plan& op) {
    State s = root;
    size_t mi = 0, oi = 0;
    int inc[2] = {0, 0};
    for (int d = 0; d < D_DEPTH && !s.over; d++) {
        Act a[2];
        a[me] = planAct(mp, mi, s);
        a[1 - me] = planAct(op, oi, s);
        step(s, a);
    }
    double v = (s.score[me] - root.score[me]) - (s.score[1 - me] - root.score[1 - me]);
    if (s.over) {
        int diff = s.score[me] - s.score[1 - me];
        v += W_WIN * (diff > 0 ? 1 : diff < 0 ? -1 : 0);
    } else {
        income(s, inc);
        double rem = min((double)(MAXTURNS - s.turn), R_LEAF);
        v += rem * (inc[me] - inc[1 - me]);
    }
    return v;
}

// Regret matching on payoff A (rows: me, maximise). Returns column mix.
static vector<double> solve(const vector<vector<double>>& A, vector<double>& rowMix) {
    int R = A.size(), C = A[0].size();
    vector<double> rr(R, 0), rc(C, 0), sr(R, 0), sc(C, 0), pr(R), pc(C);
    for (int it = 0; it < 2000; it++) {
        auto norm = [](vector<double>& reg, vector<double>& pp) {
            double t = 0; for (double x : reg) t += max(0.0, x);
            for (size_t i = 0; i < reg.size(); i++) pp[i] = t > 0 ? max(0.0, reg[i]) / t : 1.0 / reg.size();
        };
        norm(rr, pr); norm(rc, pc);
        vector<double> ur(R, 0), uc(C, 0); double v = 0;
        for (int i = 0; i < R; i++) for (int j = 0; j < C; j++) { ur[i] += A[i][j] * pc[j]; uc[j] -= A[i][j] * pr[i]; }
        for (int i = 0; i < R; i++) v += pr[i] * ur[i];
        for (int i = 0; i < R; i++) rr[i] += ur[i] - v;
        for (int j = 0; j < C; j++) rc[j] += uc[j] + v;
        for (int i = 0; i < R; i++) sr[i] += pr[i];
        for (int j = 0; j < C; j++) sc[j] += pc[j];
    }
    double tr = 0, tc = 0; for (double x : sr) tr += x; for (double x : sc) tc += x;
    rowMix = sr; for (double& x : rowMix) x /= tr;
    for (double& x : sc) x /= tc;
    return sc;
}

// ---------------------------------------------------------------- io
int main() {
    D_DEPTH   = (int)Wt("D_DEPTH", 8);
    B_BUILDS  = (int)Wt("B_BUILDS", 8);
    Z_DISRUPTS= (int)Wt("Z_DISRUPTS", 2);
    R_LEAF    = Wt("R_LEAF", 8);
    L_PRIOR   = Wt("L_PRIOR", 10);
    W_RISK3   = Wt("W_RISK3", 12);
    W_RISK2   = Wt("W_RISK2", 2);
    W_WIN     = Wt("W_WIN", 300);
    W_UNIF    = Wt("W_UNIF", 0.3);
    TIME_MS   = Wt("TIME_MS", 38);
    if (const char* f = getenv("LOGF")) freopen(f, "a", stderr);   // local runs: keep our trace

    int me; cin >> me >> W >> H; N = W * H;
    NZ = 0;
    for (int i = 0; i < N; i++) {
        int r, t; cin >> r >> t;
        zoneOf[i] = r; costOf[i] = t == 2 ? 3 : t == 1 ? 2 : t == 3 ? 3 : 1; townAt[i] = -1;
        NZ = max(NZ, r + 1);
    }
    for (int i = 0; i < N; i++) {
        int x = i % W, y = i / W;
        nb[i][0] = y > 0 ? i - W : -1; nb[i][1] = x < W - 1 ? i + 1 : -1;
        nb[i][2] = y < H - 1 ? i + W : -1; nb[i][3] = x > 0 ? i - 1 : -1;
        zoneCells[zoneOf[i]].push_back(i);
    }
    cin >> NT;
    for (int i = 0; i < NT; i++) {
        int id, x, y; string d; cin >> id >> x >> y >> d;
        townCell[id] = y * W + x; townAt[y * W + x] = id; zoneHasTown[zoneOf[y * W + x]] = true;
        if (d != "x") { stringstream ss(d); string tok; while (getline(ss, tok, ',')) want[id].push_back(stoi(tok)); }
    }
    for (int i = 0; i < NT; i++) if (!want[i].empty()) srcs.push_back(i);

    State s; memset(&s, 0, sizeof s);
    for (int turn = 0;; turn++) {
        int ms, fs;
        if (!(cin >> ms >> fs)) break;
        auto t0 = chrono::steady_clock::now();
        s.score[me] = ms; s.score[1 - me] = fs; s.turn = turn; s.over = false;
        for (int i = 0; i < N; i++) {
            int o, inst, ink; string act; cin >> o >> inst >> ink >> act;
            s.track[i] = (int8_t)o; s.inst[zoneOf[i]] = inst; s.inked[zoneOf[i]] = ink;
        }
        vector<Plan> mine = genPlans(s, me, B_BUILDS, Z_DISRUPTS);
        vector<Plan> foes = genPlans(s, 1 - me, B_BUILDS, Z_DISRUPTS);
        int R = mine.size(), C = foes.size();
        vector<vector<double>> A(R, vector<double>(C, 0));
        for (int i = 0; i < R; i++) for (int j = 0; j < C; j++) A[i][j] = rollout(s, me, mine[i], foes[j]);
        vector<double> rowMix;
        vector<double> col = solve(A, rowMix);
        // foe model: equilibrium mixed with uniform
        int best = 0; double bv = -1e18;
        for (int i = 0; i < R; i++) {
            double v = 0;
            for (int j = 0; j < C; j++) v += A[i][j] * ((1 - W_UNIF) * col[j] + W_UNIF / C);
            if (v > bv) { bv = v; best = i; }
        }
        size_t idx = 0;
        Act a = planAct(mine[best], idx, s);
        // leftover paint -> best immediate spot cells
        int used = 0; for (int i = 0; i < a.n; i++) used += costOf[a.cells[i]];
        string out;
        for (int i = 0; i < a.n; i++) out += "PLACE_TRACKS " + to_string(a.cells[i] % W) + " " + to_string(a.cells[i] / W) + ";";
        if (a.dz >= 0) out += "DISRUPT " + to_string(a.dz) + ";";
        double el = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
        cerr << "T" << turn << " R" << R << " C" << C << " v" << (int)bv << " " << (int)el << "ms" << endl;
        if (out.empty()) out = "WAIT"; else out.pop_back();
        cout << out << endl;
    }
}
