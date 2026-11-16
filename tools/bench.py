#!/usr/bin/env python3
"""Play a bot vs the Silver boss on fixed seeds through the CodinGame API.

usage: tools/bench.py BOT.cpp N [data/results.jsonl]   (LOGDIR=dir saves game logs)
Seeds are SEEDS[:N] (filled from new games on first use and appended to seeds.txt),
so two versions of the bot are compared on the same maps.
ponytail: sequential, one game at a time, the API allows nothing else.
"""
import json, os, sys, time, urllib.request, urllib.error

API = "https://www.codingame.com/api/v1"
P = "summer-challenge-2026-back-track-king"
TOKEN = open(os.path.expanduser("~/.config/codingame/token")).read().strip()
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SEEDS_FILE = os.path.join(ROOT, "data", "seeds.txt")
LOGDIR = os.environ.get("LOGDIR")  # optional: save each game's text log there


def call(method, path, body=None):
    while True:
        req = urllib.request.Request(API + path, method=method,
                                     data=json.dumps(body).encode() if body else None,
                                     headers={"Authorization": "Bearer " + TOKEN,
                                              "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return json.loads(r.read())
        except urllib.error.HTTPError as e:
            j = json.loads(e.read() or b"{}")
            if e.code == 429:
                time.sleep(int(j.get("retry_after_seconds", 30)) + 1); continue
            if e.code == 409 and "poll_url" in j:
                call("GET", "/test-games/%s?wait=30&format=json" % j["test_game_id"]); continue
            raise SystemExit("%d %s" % (e.code, j))


def play(code, seed):
    body = {"language": "C++", "code": code, "opponents": ["boss"]}
    if seed: body["game_options"] = "seed=" + seed
    r = call("POST", "/puzzles/%s/test-games?wait=30" % P, body)
    while r.get("status") not in ("done", "failed"):
        r = call("GET", "/test-games/%s?wait=30&format=json" % r["test_game_id"])
    if r["status"] == "failed":
        if "machine" in str(r.get("hint", "")):
            time.sleep(90); return play(code, seed)
        raise SystemExit(str(r))
    log = r["log"]
    s = [l for l in log.splitlines() if l.strip().startswith("scores:")][0].split()[1:]
    sc = {int(k): int(v) for k, v in (x.split("=") for x in s)}
    opt = [l for l in log.splitlines() if l.startswith("options:")][0].split("seed=")[1].split()[0]
    if LOGDIR:
        os.makedirs(LOGDIR, exist_ok=True)
        open(os.path.join(LOGDIR, opt + ".log"), "w").write(log)
    return opt, sc[0], sc[1], r.get("limits", {}).get("test_games", {}).get("remaining")


def main():
    bot, n = sys.argv[1], int(sys.argv[2])
    out = open(sys.argv[3], "a") if len(sys.argv) > 3 else None
    code = open(bot).read()
    seeds = open(SEEDS_FILE).read().split() if os.path.exists(SEEDS_FILE) else []
    tot = [0, 0, 0]
    for i in range(n):
        seed, me, boss, left = play(code, seeds[i] if i < len(seeds) else None)
        if i >= len(seeds):
            seeds.append(seed); open(SEEDS_FILE, "a").write(seed + "\n")
        tot[0] += me > boss; tot[1] += me; tot[2] += boss
        line = {"bot": bot, "seed": seed, "me": me, "boss": boss}
        print(i, seed, me, boss, "WIN" if me > boss else "loss", "left", left, flush=True)
        if out: out.write(json.dumps(line) + "\n"); out.flush()
    print("wins %d/%d  points %d vs %d  margin %.3f" %
          (tot[0], n, tot[1], tot[2], (tot[1] - tot[2]) / max(1, tot[1] + tot[2])))


if __name__ == "__main__":
    main()
