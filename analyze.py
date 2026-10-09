"""
TrustOps campaign v3, step 3: statistics and summary tables.

Usage: python analyze.py <results_dir>
Reads results/T<trees>/*.csv written by the C benchmark and writes
results/summary/*.csv and results/summary/SUMMARY.md.

Statistics: medians with interquartile range and a 95% bootstrap
confidence interval of the median (2,000 resamples). End-to-end
percentiles are pooled over all repetitions; their CI is a bootstrap
over repetitions.
"""

import glob
import json
import os
import sys

import numpy as np
import pandas as pd

BUDGETS_NS = {"100us": 100_000, "1ms": 1_000_000}
RNG = np.random.default_rng(0)
SIX_MONTHS_DAYS = 182.5


def boot_ci(x, stat=np.median, n=2000):
    x = np.asarray(x, dtype=float)
    if len(x) < 2:
        return (float("nan"), float("nan"))
    idx = RNG.integers(0, len(x), size=(n, len(x)))
    s = stat(x[idx], axis=1)
    return float(np.percentile(s, 2.5)), float(np.percentile(s, 97.5))


def summarise_ops(df):
    rows = []
    for (op, lv), g in df.groupby(["op", "level"], sort=False):
        v = g.ns_per_op.to_numpy()
        lo, hi = boot_ci(v)
        rows.append({"op": op, "level": lv, "n": len(v),
                     "median_ns": np.median(v), "mean_ns": np.mean(v),
                     "p25_ns": np.percentile(v, 25), "p75_ns": np.percentile(v, 75),
                     "ci95_lo_ns": lo, "ci95_hi_ns": hi})
    return pd.DataFrame(rows)


def summarise_e2e(df):
    rows = []
    df = df.assign(total_ns=df.inference_ns + df.evidence_ns)
    for cfg, g in df.groupby("config", sort=False):
        per_rep = g.groupby("rep").evidence_ns.median().to_numpy()
        lo, hi = boot_ci(per_rep)
        r = {"config": cfg, "level": g.level.iloc[0],
             "integrity": g.integrity.iloc[0], "durability": g.durability.iloc[0],
             "decisions": len(g), "reps": g.rep.nunique(),
             "inference_p50_ns": g.inference_ns.median(),
             "evidence_p50_ns": g.evidence_ns.median(),
             "evidence_mean_ns": g.evidence_ns.mean(),
             "evidence_p50_ci95_lo": lo, "evidence_p50_ci95_hi": hi,
             "evidence_p95_ns": g.evidence_ns.quantile(.95),
             "evidence_p99_ns": g.evidence_ns.quantile(.99),
             "total_p50_ns": g.total_ns.median(),
             "total_p99_ns": g.total_ns.quantile(.99),
             "total_max_ns": g.total_ns.max()}
        for b, ns in BUDGETS_NS.items():
            r[f"within_{b}_pct"] = 100 * (g.total_ns <= ns).mean()
        rows.append(r)
    return pd.DataFrame(rows)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "results"
    out = os.path.join(root, "summary")
    os.makedirs(out, exist_ok=True)
    md = ["# TrustOps campaign v3 -- summary", ""]

    tdirs = sorted(glob.glob(os.path.join(root, "T*")),
                   key=lambda p: int(os.path.basename(p)[1:]))
    ops_all, e2e_all = [], []
    for d in tdirs:
        T = int(os.path.basename(d)[1:])
        meta = json.load(open(os.path.join(d, "bench_meta.json")))
        md.append(f"- T={T}: parity treelite vs sklearn {meta['parity_max_abs_diff_treelite_vs_sklearn']:.2g}, "
                  f"Saabas reconstruction error {meta['saabas_max_abs_reconstruction_error']:.2g}, "
                  f"{meta['features']} features, {meta['stream_rows']} stream rows")
        p = os.path.join(d, "e1_ops_raw.csv")
        if os.path.exists(p):
            s = summarise_ops(pd.read_csv(p)); s.insert(0, "trees", T); ops_all.append(s)
        p = os.path.join(d, "e2_e2e_raw.csv")
        if os.path.exists(p):
            s = summarise_e2e(pd.read_csv(p)); s.insert(0, "trees", T); e2e_all.append(s)
    md.append("")

    if ops_all:
        ops = pd.concat(ops_all)
        ops.round(1).to_csv(os.path.join(out, "ops.csv"), index=False)
        md += ["## Per-operation cost (median ns, 95% CI)", "",
               "| trees | op | level | median | CI95 | IQR | mean |", "|---|---|---|---|---|---|---|"]
        for _, r in ops.iterrows():
            md.append(f"| {r.trees} | {r.op} | {r.level} | {r.median_ns:.0f} | "
                      f"{r.ci95_lo_ns:.0f}-{r.ci95_hi_ns:.0f} | {r.p25_ns:.0f}-{r.p75_ns:.0f} | {r.mean_ns:.0f} |")
        md.append("")

    if e2e_all:
        e2e = pd.concat(e2e_all)
        e2e.round(1).to_csv(os.path.join(out, "e2e.csv"), index=False)
        md += ["## End-to-end per decision (ns)", "",
               "| trees | config | inference p50 | evidence p50 [CI95] | evidence p99 | total p99 | <=100us | <=1ms |",
               "|---|---|---|---|---|---|---|---|"]
        for _, r in e2e.iterrows():
            md.append(f"| {r.trees} | {r.config} | {r.inference_p50_ns:.0f} | {r.evidence_p50_ns:.0f} "
                      f"[{r.evidence_p50_ci95_lo:.0f}-{r.evidence_p50_ci95_hi:.0f}] | {r.evidence_p99_ns:.0f} | "
                      f"{r.total_p99_ns:.0f} | {r.within_100us_pct:.2f}% | {r.within_1ms_pct:.2f}% |")
        md.append("")

    main_dir = tdirs[len(tdirs) // 2] if tdirs else None
    for d in tdirs:   # the full campaign directory has the tamper results
        if os.path.exists(os.path.join(d, "e4_tamper.csv")):
            main_dir = d
    if main_dir:
        p = os.path.join(main_dir, "e3_sizes.csv")
        if os.path.exists(p):
            s = pd.read_csv(p)
            s["share_hash_pct"] = 100 * s.hash_field_bytes / s.mean_bytes
            s["six_months_1Hz_GB"] = s.mean_bytes * 86400 * SIX_MONTHS_DAYS / 1e9
            s.round(2).to_csv(os.path.join(out, "sizes.csv"), index=False)
            md += ["## Record size (bytes) and six-month storage at 1 Hz (GB)", "",
                   s.pivot_table(index=["features", "encoding"], columns="level",
                                 values="mean_bytes").round(1).to_markdown(), "",
                   "Raw-input retention needed for R2 at L1/L2 (float64 inputs): "
                   + ", ".join(f"d={d}: {8*d} B/decision, {8*d*86400*SIX_MONTHS_DAYS/1e9:.1f} GB per 6 months at 1 Hz"
                               for d in sorted(s.features.unique())), ""]
        p = os.path.join(main_dir, "e4_tamper.csv")
        if os.path.exists(p):
            t = pd.read_csv(p)
            pv = t.pivot_table(index=["attack", "attacker", "region"], columns="integrity",
                               values="detected", aggfunc="max")
            pv.to_csv(os.path.join(out, "tamper.csv"))
            md += ["## Tamper detection (1 = detected)", "", pv.to_markdown(), ""]
        p = os.path.join(main_dir, "e5_verify.csv")
        if os.path.exists(p):
            v = pd.read_csv(p).groupby("integrity").ns_per_record.agg(["median", "min", "max"])
            v["day_at_1Hz_s"] = v["median"] * 86400 / 1e9
            v.round(3).to_csv(os.path.join(out, "verify.csv"))
            md += ["## Auditor verification (ns/record)", "", v.round(1).to_markdown(), ""]
        p = os.path.join(main_dir, "e6_drift.csv")
        if os.path.exists(p):
            dr = pd.read_csv(p)
            dr.to_csv(os.path.join(out, "drift.csv"), index=False)
            md += ["## Drift monitor on UNSW-NB15 (threshold calibrated on held-out normal training rows)", "",
                   dr.to_markdown(index=False), ""]

    open(os.path.join(out, "SUMMARY.md"), "w").write("\n".join(md))
    print("\n".join(md))


if __name__ == "__main__":
    main()
