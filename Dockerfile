# Measurement image (linux/arm64): compiles the generated forest and the
# C benchmark, runs the campaign, computes statistics.
FROM python:3.12-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc libc6-dev libssl-dev && rm -rf /var/lib/apt/lists/*
RUN pip install --no-cache-dir numpy==2.5.3 pandas==3.0.5 tabulate==0.10.0
WORKDIR /bench
COPY bench.c analyze.py build_and_run.sh ./
