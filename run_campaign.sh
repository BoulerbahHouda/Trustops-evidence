#!/usr/bin/env bash
# TrustOps campaign v3. Run from this folder on the Mac, Docker Desktop open.
# Put UNSW_NB15_training-set.csv and UNSW_NB15_testing-set.csv in ./data first.
set -euo pipefail
for f in data/UNSW_NB15_training-set.csv data/UNSW_NB15_testing-set.csv; do
  [ -f "$f" ] || { echo "missing $f (see README)"; exit 1; }
done
docker build --platform linux/amd64 -f Dockerfile.prep -t trustops-prep:v3 .
docker build --platform linux/arm64 -t trustops-bench:v3 .
mkdir -p out
docker version --format '{{.Server.Version}}' > out/docker_version.txt
sysctl -n machdep.cpu.brand_string > out/host_chip.txt 2>/dev/null || true
VOL="-v $PWD/data:/bench/data:ro -v $PWD/out:/bench/out"
PREP="docker run --rm --platform linux/amd64 $VOL trustops-prep:v3"
RUN="docker run --rm --platform linux/arm64 $VOL"

# 0) smoke test (~5 min): small model, short runs
docker run --rm --platform linux/amd64 $VOL -e QUICK=1 trustops-prep:v3 bash build_and_run.sh prepare
$RUN trustops-bench:v3 bash build_and_run.sh compile
$RUN -e QUICK=1 --cpus=1 --memory=1g trustops-bench:v3 bash build_and_run.sh run
mv out/results out/results_quick; rm -rf out/build

# 1) train, generate and compile the three forests (no CPU limit, takes a while)
$PREP bash build_and_run.sh prepare 2>&1 | tee out/prepare_log.txt
$RUN trustops-bench:v3 bash build_and_run.sh compile 2>&1 | tee -a out/prepare_log.txt
# 2) measure: one CPU, 1 GB, as in the paper
$RUN --cpus=1 --memory=1g trustops-bench:v3 bash build_and_run.sh run 2>&1 | tee out/run_log.txt
echo "Send me the folder out/results (zip it)."
