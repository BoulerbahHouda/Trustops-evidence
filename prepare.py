"""
TrustOps campaign v3, step 1 (Python, run once).

Loads UNSW-NB15 (official training/testing partitions), trains random
forests, compiles each forest to C with treelite/tl2cgen, and exports
everything the C benchmark needs as flat binary files.

Usage:
  python prepare.py --train UNSW_NB15_training-set.csv \
                    --test  UNSW_NB15_testing-set.csv --out build
  python prepare.py --synthetic --out build     # DEV ONLY, not for results

Outputs in --out:
  model_T<trees>/          tl2cgen C sources for that forest
  forest_T<trees>.bin      node arrays (for Saabas and the reference walk)
  stream.bin               test-set feature matrix, float64, row-major
  stream_meta.json         rows, features, labels, attack categories
  drift_streams.bin/.json  stationary and shifted streams for the drift test
  train_stats.bin          per-feature training mean and std (float64)
  prepare_report.json      dataset, accuracy, F1, parity checks
"""

import argparse
import json
import os
import struct
import sys

import numpy as np

CAT_COLS = ("proto", "service", "state")
DROP_COLS = ("id", "attack_cat", "label")


def load_unsw(train_path, test_path):
    import pandas as pd
    tr = pd.read_csv(train_path)
    te = pd.read_csv(test_path)
    tr.columns = [c.strip() for c in tr.columns]
    te.columns = [c.strip() for c in te.columns]
    feats = [c for c in tr.columns if c not in DROP_COLS]
    # ordinal-encode categoricals from the TRAINING vocabulary only;
    # unseen test values map to -1 (a valid numeric split value)
    for c in CAT_COLS:
        vocab = {v: i for i, v in enumerate(sorted(tr[c].astype(str).unique()))}
        tr[c] = tr[c].astype(str).map(vocab).astype(float)
        te[c] = te[c].astype(str).map(vocab).fillna(-1).astype(float)
    Xtr = tr[feats].to_numpy(dtype=np.float64)
    Xte = te[feats].to_numpy(dtype=np.float64)
    return (Xtr, tr["label"].to_numpy(int), Xte, te["label"].to_numpy(int),
            te["attack_cat"].astype(str).str.strip().to_numpy(), feats,
            {"source": "UNSW-NB15 official partitions",
             "train_rows": int(len(tr)), "test_rows": int(len(te))})


def load_synthetic():
    """Same shape as UNSW-NB15 (42 features). DEV ONLY."""
    rng = np.random.default_rng(0)
    d = 42
    w = rng.normal(size=d)
    def gen(n):
        X = rng.lognormal(size=(n, d))
        y = ((np.log(X) @ w) > 0).astype(int)
        return X, y
    Xtr, ytr = gen(20000)
    Xte, yte = gen(10000)
    cats = np.where(yte == 0, "Normal", "Generic")
    return (Xtr, ytr, Xte, yte, cats, [f"f{i}" for i in range(d)],
            {"source": "SYNTHETIC stand-in, NOT UNSW-NB15 (dev only)",
             "train_rows": 20000, "test_rows": 10000})


def export_forest(clf, path):
    """Binary layout (little endian):
       int32 n_trees, int32 n_features
       per tree: int32 n_nodes, then arrays of n_nodes:
         int32 left, int32 right, int32 feature, float64 threshold,
         float64 p1 (class-1 probability of the node)
    """
    with open(path, "wb") as f:
        f.write(struct.pack("<ii", len(clf.estimators_), clf.n_features_in_))
        for est in clf.estimators_:
            t = est.tree_
            v = t.value[:, 0, :]
            v = v / v.sum(axis=1, keepdims=True)
            p1 = v[:, 1] if v.shape[1] > 1 else np.zeros(t.node_count)
            f.write(struct.pack("<i", t.node_count))
            f.write(t.children_left.astype("<i4").tobytes())
            f.write(t.children_right.astype("<i4").tobytes())
            f.write(np.maximum(t.feature, 0).astype("<i4").tobytes())
            f.write(t.threshold.astype("<f8").tobytes())
            f.write(p1.astype("<f8").tobytes())


def compile_treelite(clf, dirpath):
    import treelite
    import tl2cgen
    m = treelite.sklearn.import_model(clf)
    tl2cgen.generate_c_code(m, dirpath=dirpath, params={"quantize": 0})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--train")
    ap.add_argument("--test")
    ap.add_argument("--synthetic", action="store_true")
    ap.add_argument("--out", default="build")
    ap.add_argument("--trees", default="25,100,200")
    ap.add_argument("--depth", type=int, default=12)
    ap.add_argument("--stream-rows", type=int, default=20000)
    a = ap.parse_args()

    if a.synthetic:
        Xtr, ytr, Xte, yte, cats, feats, info = load_synthetic()
    else:
        if not (a.train and a.test):
            sys.exit("give --train and --test (UNSW-NB15 CSVs), or --synthetic")
        Xtr, ytr, Xte, yte, cats, feats, info = load_unsw(a.train, a.test)

    # scikit-learn casts inputs to float32 before comparing them with split
    # thresholds; compiled code compares float64. Rounding all inputs to
    # float32 once makes both paths see exactly the same values.
    Xtr = Xtr.astype(np.float32).astype(np.float64)
    Xte = Xte.astype(np.float32).astype(np.float64)

    from sklearn.ensemble import RandomForestClassifier
    from sklearn.metrics import accuracy_score, f1_score

    models = {}
    os.makedirs(a.out, exist_ok=True)
    rng = np.random.default_rng(1)
    report = {"dataset": info, "n_features": len(feats),
              "features": feats, "depth": a.depth, "models": []}

    for T in [int(t) for t in a.trees.split(",")]:
        clf = RandomForestClassifier(n_estimators=T, max_depth=a.depth,
                                     random_state=0, n_jobs=-1)
        clf.fit(Xtr, ytr)
        pred = clf.predict(Xte)
        export_forest(clf, os.path.join(a.out, f"forest_T{T}.bin"))
        compile_treelite(clf, os.path.join(a.out, f"model_T{T}"))
        models[T] = clf
        report["models"].append({
            "trees": T,
            "accuracy": round(float(accuracy_score(yte, pred)), 4),
            "f1": round(float(f1_score(yte, pred)), 4),
            "nodes_total": int(sum(e.tree_.node_count for e in clf.estimators_)),
        })
        print(report["models"][-1])

    # decision stream: random sample of the official test partition
    n = min(a.stream_rows, len(Xte))
    sel = rng.choice(len(Xte), n, replace=False)
    Xte[sel].astype("<f8").tofile(os.path.join(a.out, "stream.bin"))
    json.dump({"rows": int(n), "features": len(feats),
               "labels": yte[sel].tolist()},
              open(os.path.join(a.out, "stream_meta.json"), "w"))
    # sklearn reference probabilities, checked by the C benchmark against
    # both the treelite-compiled forest and the reference tree walk
    for T, clf in models.items():
        clf.predict_proba(Xte[sel])[:, 1].astype("<f8").tofile(
            os.path.join(a.out, f"ref_proba_T{T}.bin"))

    # training statistics for the drift monitor
    mu, sd = Xtr.mean(axis=0), Xtr.std(axis=0)
    np.concatenate([mu, sd]).astype("<f8").tofile(
        os.path.join(a.out, "train_stats.bin"))

    # drift streams: phase A = normal traffic only (stationary w.r.t. its
    # own reference), phase B = one attack category (real shift).
    # Reference statistics for this test are taken from held-out normal
    # traffic of the TRAINING partition, so phase A is not the reference.
    norm_tr = Xtr[ytr == 0]
    perm = rng.permutation(len(norm_tr))
    n_cal = min(5000, len(norm_tr) // 4)
    cal = norm_tr[perm[:n_cal]]                 # calibration rows (held out)
    ref = norm_tr[perm[n_cal:]]                 # reference statistics
    mu_n, sd_n = ref.mean(axis=0), ref.std(axis=0)
    normal_te = np.where(cats == "Normal")[0]
    drift = {"ref_rows": int(len(ref)), "calib_rows": int(len(cal)),
             "scenarios": []}
    blobs = [cal]
    for cat in sorted(set(cats) - {"Normal"}):
        att = np.where(cats == cat)[0]
        if len(att) < 200:
            continue
        a_idx = rng.choice(normal_te, min(5000, len(normal_te)), replace=False)
        b_idx = rng.choice(att, min(2000, len(att)), replace=False)
        stream = np.vstack([Xte[a_idx], Xte[b_idx]])
        drift["scenarios"].append({"category": cat, "phase_a": int(len(a_idx)),
                                   "phase_b": int(len(b_idx))})
        blobs.append(stream)
    with open(os.path.join(a.out, "drift_streams.bin"), "wb") as f:
        np.concatenate([mu_n, sd_n]).astype("<f8").tofile(f)
        for s in blobs:
            s.astype("<f8").tofile(f)
    json.dump(drift, open(os.path.join(a.out, "drift_streams.json"), "w"))

    json.dump(report, open(os.path.join(a.out, "prepare_report.json"), "w"),
              indent=2)
    print("done:", a.out)


if __name__ == "__main__":
    main()
