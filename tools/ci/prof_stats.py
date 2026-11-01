#!/usr/bin/env python3
"""F6.06: numbers from the per-frame profiler lines ("rensga: pf f=.. t=.. u=.. r=.. p=.. a=.. ... s=name:us:calls:parent,...") of a game log.
   prof_stats.py <log> [--budget-ms 16.667] [--skip N] [--label NAME] [--json]
Prints frames, median / p99 / max frame time, frames over 1.5 x budget, the worst frame with its samples, and the mean Update time.
Exit 1 when the log has no pf lines."""
import json, re, sys

def parse(path):
    out = []
    for line in open(path, errors="replace"):
        i = line.find("rensga: pf ")
        if i < 0:
            continue
        kv = dict(re.findall(r"(\w+)=(\S*)", line[i + 11:]))
        try:
            fr = {"f": int(kv["f"]), "t": int(kv["t"]), "u": int(kv.get("u", 0)), "r": int(kv.get("r", 0)), "p": int(kv.get("p", 0)),
                  "a": int(kv.get("a", 0)), "samples": []}
        except (KeyError, ValueError):
            continue
        s = kv.get("s", "-")
        if s != "-":
            for item in s.split(","):
                parts = item.split(":")
                if len(parts) >= 3:
                    fr["samples"].append((parts[0], int(parts[1]), int(parts[2])))
        out.append(fr)
    return out

def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__); return 2
    path, budget, skip, label, as_json = a[0], 1000000 / 60.0, 5, "", False
    i = 1
    while i < len(a):
        if a[i] == "--budget-ms": budget = float(a[i + 1]) * 1000; i += 2
        elif a[i] == "--skip": skip = int(a[i + 1]); i += 2
        elif a[i] == "--label": label = a[i + 1]; i += 2
        elif a[i] == "--json": as_json = True; i += 1
        else: i += 1
    fr = parse(path)[skip:]
    if not fr:
        print("prof_stats: no 'rensga: pf' lines in %s" % path, file=sys.stderr); return 1
    ts = sorted(f["t"] for f in fr)
    med = ts[len(ts) // 2]
    p99 = ts[min(len(ts) - 1, int(len(ts) * 0.99))]
    worst = max(fr, key=lambda f: f["t"])
    over = [f for f in fr if f["t"] > budget * 1.5]
    res = {"label": label, "frames": len(fr), "median_us": med, "p99_us": p99, "max_us": worst["t"], "worst_frame": worst["f"],
           "over_1_5x_budget": len(over), "spike_frames": [f["f"] for f in over][:12], "mean_update_us": sum(f["u"] for f in fr) // len(fr),
           "worst_samples": ["%s=%dus" % (n, u) for n, u, c in worst["samples"]]}
    if as_json:
        print(json.dumps(res))
    else:
        print("%-8s frames=%d median=%.2f ms p99=%.2f ms max=%.2f ms (frame %d, %s) over 1.5x budget: %d frames %s, mean update %.3f ms" % (
            label or "run", res["frames"], med / 1000, p99 / 1000, worst["t"] / 1000, worst["f"], ", ".join(res["worst_samples"]) or "no samples",
            len(over), res["spike_frames"], res["mean_update_us"] / 1000))
    return 0

if __name__ == "__main__":
    sys.exit(main())
