#!/usr/bin/env bash
# Runs INSIDE the containers.
#   prepare (amd64 prep image): train forests, generate C, export data
#   compile (arm64 image):      compile generated forests + benchmark
#   run     (arm64 image):      measure (append logs go to /tmp in the container)
set -euo pipefail
STAGE=$1
TREES="25 100 200"
MAIN=100
Q=${QUICK:-0}
QFLAG=""; [ "$Q" = "1" ] && QFLAG="--quick"
OUT=/bench/out
mkdir -p $OUT

if [ "$STAGE" = "prepare" ]; then
  if [ "$Q" = "1" ]; then TREES="25"; MAIN=25; fi
  python prepare.py --train /bench/data/UNSW_NB15_training-set.csv \
                    --test  /bench/data/UNSW_NB15_testing-set.csv \
                    --out $OUT/build --trees "$(echo $TREES | tr ' ' ',')" 2>&1 \
        | { grep -v "Treelite version\|Parallel compilation\|originated from a newer" || true; }
  echo "$TREES" > $OUT/build/trees.txt; echo $MAIN > $OUT/build/main.txt
fi

if [ "$STAGE" = "compile" ]; then
  for T in $(cat $OUT/build/trees.txt); do
    echo "compiling forest T=$T (can take several minutes)"
    gcc -O2 -c $OUT/build/model_T$T/main.c -o $OUT/build/model_T$T.o
    gcc -O2 -I $OUT/build/model_T$T bench.c $OUT/build/model_T$T.o \
        -o $OUT/build/bench_T$T -lcrypto -lpthread -lm
  done
  gcc --version | head -1 > $OUT/build/gcc_version.txt
fi

if [ "$STAGE" = "run" ]; then
  MAIN=$(cat $OUT/build/main.txt)
  for T in $(cat $OUT/build/trees.txt); do
    if [ "$T" = "$MAIN" ]; then ONLY="E1,E2,E3,E4,E5,E6"; else ONLY="E1,E2"; fi
    echo "== T=$T ($ONLY)"
    mkdir -p $OUT/results/T$T
    $OUT/build/bench_T$T $OUT/build $T $OUT/results/T$T $QFLAG --only $ONLY
  done
  cp $OUT/build/prepare_report.json $OUT/build/gcc_version.txt $OUT/results/
  python /bench/analyze.py $OUT/results > /dev/null
  echo "done: summary in out/results/summary/SUMMARY.md"
fi
