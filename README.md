# TrustOps-Evidence benchmark

Measures the per-decision cost of auditability by evidence level, with a
**compiled** inference path and a **compiled** evidence pipeline, on the
UNSW-NB15 intrusion-detection dataset.

## What you need

- Docker Desktop (running), on the Mac M1.
- The two official UNSW-NB15 partitions, placed in `data/`:
  - `data/UNSW_NB15_training-set.csv`
  - `data/UNSW_NB15_testing-set.csv`

  Source: https://research.unsw.edu.au/projects/unsw-nb15-dataset
  (CSV Files → Training and Testing Sets).

## Run

```bash
cd trustops-bench-v3
chmod +x run_campaign.sh
caffeinate -i ./run_campaign.sh
```

The script runs a short smoke test first (results in `out/results_quick`),
then the full campaign (results in `out/results`). Send back `out/results`
(zipped) and `out/prepare_log.txt`, `out/run_log.txt`.

Expect roughly 30–60 minutes in total; compiling the 200-tree forest is the
slowest step.

## How it is organised

| Step | Image | What it does |
|---|---|---|
| prepare | `Dockerfile.prep` (linux/amd64, emulated) | trains forests (25, 100, 200 trees, depth 12) on the training partition, reports accuracy and F1 on the test partition, compiles each forest to C with treelite/tl2cgen, exports node arrays, the decision stream, drift streams |
| compile | `Dockerfile` (linux/arm64) | compiles the generated forests and `bench.c` with gcc -O2 against OpenSSL |
| run | `Dockerfile` (linux/arm64), 1 CPU, 1 GB | runs the benchmark, then `analyze.py` |

tl2cgen ships no ARM64 wheel, so code generation runs in an amd64 container.
It only produces portable C source; all compilation and all measurements
happen on ARM64.

## Experiments (`bench.c`)

| | Measures | Output |
|---|---|---|
| parity | compiled forest vs scikit-learn (must be ~1e-16, else abort); Saabas reconstruction error | `bench_meta.json` |
| E1 | per-operation cost: compiled inference, Saabas top-3, plain and keyed input digest, drift update, JSON and binary serialisation (L1–L3), four integrity variants, anchor event, five write policies. Batched timing (≥50 µs per sample), 201 samples | `e1_ops_raw.csv` |
| E2 | end-to-end per decision on the UNSW-NB15 stream (20,000 decisions × 10 repetitions, configurations rotated), inference and evidence timed separately | `e2_e2e_raw.csv` |
| E3 | record size by level, encoding (JSON-hex, binary-raw) and width (42, 20, 100, 500) | `e3_sizes.csv` |
| E4 | tamper tests: 11 attacks × 3 attacker models (storage, node compromise, auditor holding K0) × 4 integrity variants | `e4_tamper.csv` |
| E5 | auditor-side verification cost | `e5_verify.csv` |
| E6 | drift monitor on real shifts (normal → one attack category), threshold calibrated on held-out normal training rows | `e6_drift.csv` |

Integrity variants: SHA-256 hash chain; forward-secure HMAC chain
(Schneier–Kelsey key evolution, keys erased with `explicit_bzero`);
FssAgg-MAC (Ma–Tsudik, only the aggregate is kept); Ed25519 per record.
Anchors (every 100 records) carry count, chain head and aggregate, are
signed with Ed25519 and sent over a socket.

## Honest limits of this platform

- Docker Desktop on an Apple M1: a desktop core in a Linux VM. Absolute
  numbers are platform-specific.
- fsync reaches the VM's virtual disk; durability costs are not
  representative of flash storage, and no power-cut test is performed.
- Key erasure with `explicit_bzero` removes the key from the buffers the
  code controls; it does not cover copies in registers, swap or the VM.
