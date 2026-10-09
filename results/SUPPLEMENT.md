# TrustOps-Evidence — Supplement

Campaign v3, 9 Oct 2026. Linux/ARM64 container (Docker 28.1.1) on an Apple M1 host, 1 CPU, 1 GB; gcc -O2, OpenSSL 3.5.7.
Dataset: UNSW-NB15 official partitions, 42 features, inputs rounded to float32. Forests of 25, 100, 200 trees, depth ≤ 12.

## S1. Per-operation cost (ns per operation)

201 samples per operation; each sample times a batch lasting ≥ 50 µs. Median, interquartile range (IQR), bootstrap 95% CI of the median (2,000 resamples), mean.

| Trees | Operation | Level | Median | IQR | 95% CI | Mean |
|---:|---|---|---:|---|---|---:|
| 25 | inference_compiled | - | 1302 | 854–1864 | 1198–1438 | 2012 |
| 25 | inference_reference_walk | - | 1016 | 901–1188 | 991–1050 | 1310 |
| 25 | explain_saabas_top3 | - | 1357 | 1263–1513 | 1326–1387 | 1469 |
| 25 | digest_sha256 | - | 209 | 209–210 | 209–209 | 216 |
| 25 | digest_keyed_hmac | - | 297 | 297–301 | 297–297 | 304 |
| 25 | drift_update | - | 47 | 47–47 | 47–47 | 47 |
| 25 | serialize_json | L1 | 238 | 238–244 | 238–239 | 248 |
| 25 | serialize_binary | L1 | 17 | 17–17 | 17–17 | 17 |
| 25 | serialize_json | L2 | 569 | 568–570 | 568–569 | 574 |
| 25 | serialize_binary | L2 | 18 | 18–18 | 18–18 | 18 |
| 25 | serialize_json | L3 | 5615 | 5508–5721 | 5594–5635 | 5619 |
| 25 | serialize_binary | L3 | 80 | 80–81 | 80–80 | 81 |
| 25 | sha256-chain | L2 | 236 | 235–236 | 236–236 | 237 |
| 25 | fs-hmac | L2 | 837 | 836–838 | 837–837 | 841 |
| 25 | fssagg-mac | L2 | 839 | 838–839 | 839–839 | 845 |
| 25 | ed25519-record | L2 | 30750 | 30667–31084 | 30708–30750 | 32505 |
| 25 | anchor_event | - | 34458 | 34229–34812 | 34396–34542 | 35647 |
| 25 | append | buffered | 283 | 259–304 | 277–289 | 295 |
| 25 | append | write | 451 | 435–468 | 447–453 | 462 |
| 25 | append | fsync-every-100 | 7074 | 484–7558 | 6960–7174 | 5317 |
| 25 | append | fsync-every-10 | 47724 | 45469–53477 | 46839–49578 | 41107 |
| 25 | append | fsync-every-1 | 342854 | 309636–358198 | 321771–352646 | 333499 |
| 100 | inference_compiled | - | 4286 | 3604–4901 | 4083–4386 | 4377 |
| 100 | inference_reference_walk | - | 8000 | 4542–10208 | 7125–8500 | 7602 |
| 100 | explain_saabas_top3 | - | 7391 | 6625–8490 | 7089–7672 | 7644 |
| 100 | digest_sha256 | - | 209 | 209–212 | 209–210 | 215 |
| 100 | digest_keyed_hmac | - | 297 | 297–298 | 297–297 | 301 |
| 100 | drift_update | - | 47 | 47–47 | 47–47 | 57 |
| 100 | serialize_json | L1 | 238 | 238–239 | 238–239 | 240 |
| 100 | serialize_binary | L1 | 17 | 17–17 | 17–17 | 17 |
| 100 | serialize_json | L2 | 574 | 572–578 | 573–575 | 577 |
| 100 | serialize_binary | L2 | 18 | 18–18 | 18–18 | 18 |
| 100 | serialize_json | L3 | 5555 | 5440–5669 | 5516–5591 | 5570 |
| 100 | serialize_binary | L3 | 80 | 80–80 | 80–80 | 80 |
| 100 | sha256-chain | L2 | 236 | 235–236 | 235–236 | 237 |
| 100 | fs-hmac | L2 | 837 | 836–837 | 836–837 | 841 |
| 100 | fssagg-mac | L2 | 839 | 839–840 | 839–839 | 843 |
| 100 | ed25519-record | L2 | 30708 | 30625–30750 | 30667–30708 | 30906 |
| 100 | anchor_event | - | 34500 | 34188–34667 | 34416–34542 | 34686 |
| 100 | append | buffered | 276 | 263–295 | 273–282 | 289 |
| 100 | append | write | 450 | 439–460 | 447–453 | 453 |
| 100 | append | fsync-every-100 | 6552 | 482–6954 | 6369–6625 | 5208 |
| 100 | append | fsync-every-10 | 46638 | 41423–53775 | 44214–49083 | 42319 |
| 100 | append | fsync-every-1 | 323604 | 296437–353323 | 308917–349125 | 329986 |
| 200 | inference_compiled | - | 13375 | 8208–21084 | 12041–16417 | 15675 |
| 200 | inference_reference_walk | - | 16291 | 9541–20167 | 14455–16792 | 15319 |
| 200 | explain_saabas_top3 | - | 16146 | 13885–18636 | 15677–16708 | 16733 |
| 200 | digest_sha256 | - | 209 | 209–212 | 209–209 | 214 |
| 200 | digest_keyed_hmac | - | 297 | 297–298 | 297–297 | 302 |
| 200 | drift_update | - | 47 | 47–47 | 47–47 | 47 |
| 200 | serialize_json | L1 | 238 | 238–239 | 238–239 | 240 |
| 200 | serialize_binary | L1 | 17 | 17–17 | 17–17 | 17 |
| 200 | serialize_json | L2 | 573 | 572–574 | 573–573 | 654 |
| 200 | serialize_binary | L2 | 18 | 18–18 | 18–18 | 18 |
| 200 | serialize_json | L3 | 5565 | 5461–5688 | 5536–5602 | 5585 |
| 200 | serialize_binary | L3 | 80 | 80–80 | 80–80 | 81 |
| 200 | sha256-chain | L2 | 236 | 235–236 | 235–236 | 237 |
| 200 | fs-hmac | L2 | 836 | 836–837 | 836–837 | 841 |
| 200 | fssagg-mac | L2 | 839 | 838–839 | 839–839 | 846 |
| 200 | ed25519-record | L2 | 30667 | 30625–30709 | 30667–30667 | 30909 |
| 200 | anchor_event | - | 34270 | 34062–34438 | 34188–34312 | 34108 |
| 200 | append | buffered | 264 | 254–280 | 261–271 | 276 |
| 200 | append | write | 433 | 426–446 | 432–436 | 438 |
| 200 | append | fsync-every-100 | 6528 | 475–6835 | 6415–6602 | 4915 |
| 200 | append | fsync-every-10 | 46062 | 41957–50897 | 44047–48310 | 43047 |
| 200 | append | fsync-every-1 | 311938 | 280135–332865 | 288542–325542 | 319500 |

## S2. End-to-end cost per decision (µs)

10 repetitions × 20,000 decisions (2,000 for fsync on every record), first 200 decisions of each run discarded, configuration order rotated. Default: L2, forward-secure HMAC, fsync every 100 records, anchor every 100 records. Periodic decisions: the 1% that flush the log and emit an anchor. CI: bootstrap over per-repetition medians.

| Trees | Config | Inference p50 | Evidence p50 | 95% CI | Evidence IQR | Evidence mean | Periodic p50 | Total p99 | Total p99.9 | Total max | ≤100 µs (%) | ≤1 ms (%) |
|---:|---|---:|---:|---|---|---:|---:|---:|---:|---:|---:|---:|
| 25 | default | 0.62 | 4.00 | 4.00–4.04 | 3.21–4.92 | 9.31 | 496.0 | 333.7 | 551.5 | 8908.8 | 99.00 | 99.99 |
| 25 | level-L1 | 0.58 | 1.92 | 1.92–1.92 | 1.92–1.96 | 7.14 | 485.3 | 337.8 | 573.2 | 1807.4 | 98.99 | 100.00 |
| 25 | level-L3 | 0.67 | 9.21 | 9.21–9.23 | 8.21–10.79 | 14.87 | 504.5 | 406.8 | 601.2 | 2458.4 | 99.00 | 99.99 |
| 25 | integ-chain | 0.62 | 3.38 | 3.33–3.38 | 2.58–4.25 | 8.55 | 488.0 | 170.6 | 556.5 | 2283.0 | 99.00 | 100.00 |
| 25 | integ-fssagg | 0.62 | 3.96 | 3.96–4.00 | 3.21–4.88 | 9.22 | 493.2 | 149.1 | 556.3 | 2476.2 | 99.00 | 99.99 |
| 25 | integ-ed25519 | 0.92 | 35.00 | 34.92–35.08 | 33.83–35.96 | 40.76 | 536.0 | 447.5 | 622.0 | 6438.4 | 98.99 | 99.98 |
| 25 | dur-buffered | 0.58 | 3.54 | 3.54–3.56 | 2.79–4.38 | 4.05 | 40.5 | 38.4 | 44.5 | 209.1 | 100.00 | 100.00 |
| 25 | dur-fsync1 | 1.92 | 339.44 | 325.69–350.52 | 302.86–368.38 | 348.87 | 389.2 | 694.3 | 919.1 | 8915.0 | 0.00 | 99.91 |
| 100 | default | 4.92 | 10.92 | 10.90–10.96 | 7.25–14.17 | 16.08 | 497.8 | 106.3 | 643.7 | 3011.8 | 99.00 | 99.97 |
| 100 | level-L1 | 4.71 | 2.29 | 2.29–2.29 | 2.12–2.46 | 7.61 | 474.9 | 305.0 | 551.7 | 5015.1 | 99.00 | 99.97 |
| 100 | level-L3 | 4.96 | 16.00 | 15.88–16.08 | 12.17–20.12 | 21.71 | 512.2 | 140.7 | 604.2 | 6903.5 | 99.00 | 99.97 |
| 100 | integ-chain | 4.88 | 10.21 | 10.17–10.25 | 6.58–13.46 | 15.20 | 494.9 | 135.2 | 579.5 | 2328.8 | 99.00 | 99.98 |
| 100 | integ-fssagg | 4.88 | 10.88 | 10.79–10.96 | 7.21–14.12 | 15.96 | 496.6 | 167.3 | 605.0 | 3872.9 | 99.00 | 99.97 |
| 100 | integ-ed25519 | 5.50 | 42.42 | 42.29–42.48 | 38.17–45.29 | 47.54 | 535.3 | 177.7 | 623.8 | 12237.7 | 98.99 | 99.97 |
| 100 | dur-buffered | 4.42 | 10.00 | 9.92–10.06 | 6.54–13.00 | 10.23 | 49.7 | 42.8 | 62.9 | 5120.3 | 99.99 | 100.00 |
| 100 | dur-fsync1 | 8.60 | 334.75 | 330.19–340.54 | 299.17–365.46 | 354.65 | 390.0 | 758.9 | 1725.3 | 6747.8 | 0.00 | 99.22 |
| 200 | default | 13.17 | 21.08 | 20.75–20.96 | 13.79–27.00 | 26.86 | 558.5 | 512.9 | 706.5 | 18539.6 | 98.27 | 99.97 |
| 200 | level-L1 | 12.08 | 2.62 | 2.60–2.62 | 2.38–2.75 | 7.70 | 483.4 | 172.9 | 544.4 | 2264.9 | 99.00 | 99.99 |
| 200 | level-L3 | 12.96 | 25.79 | 25.75–25.88 | 18.58–32.62 | 31.28 | 570.9 | 372.9 | 635.7 | 3068.0 | 98.95 | 99.99 |
| 200 | integ-chain | 12.96 | 20.21 | 20.10–20.25 | 12.96–26.12 | 25.48 | 560.8 | 501.7 | 639.1 | 55311.6 | 98.85 | 99.98 |
| 200 | integ-fssagg | 12.88 | 20.75 | 20.67–20.83 | 13.54–26.67 | 25.60 | 557.7 | 393.0 | 620.8 | 3746.1 | 98.97 | 99.99 |
| 200 | integ-ed25519 | 13.50 | 52.50 | 52.33–52.62 | 45.08–58.38 | 57.71 | 595.8 | 527.5 | 684.1 | 8533.0 | 97.39 | 99.99 |
| 200 | dur-buffered | 12.08 | 19.33 | 19.21–19.38 | 12.33–25.12 | 19.30 | 67.5 | 72.7 | 104.9 | 3089.9 | 99.86 | 100.00 |
| 200 | dur-fsync1 | 19.62 | 343.54 | 340.67–346.48 | 308.07–372.80 | 352.42 | 406.8 | 693.8 | 1614.4 | 1879.1 | 0.00 | 99.80 |

## S3. Record size (bytes, mean of 2,000 records)

| Features | Encoding | L1 | L2 | L3 | Hash fields |
|---:|---|---:|---:|---:|---:|
| 42 | json-hex | 336.0 | 387.3 | 555.0 | 161 |
| 42 | binary-raw | 119.0 | 137.0 | 473.0 | 64 |
| 20 | json-hex | 336.0 | 386.5 | 485.3 | 161 |
| 20 | binary-raw | 119.0 | 137.0 | 297.0 | 64 |
| 100 | json-hex | 336.0 | 387.7 | 777.6 | 161 |
| 100 | binary-raw | 119.0 | 137.0 | 937.0 | 64 |
| 500 | json-hex | 336.0 | 390.3 | 2251.5 | 161 |
| 500 | binary-raw | 119.0 | 137.0 | 4137.0 | 64 |

## S4. Auditor verification (ns per record, 15 replays of 2,000 records)

| Integrity | Median | IQR | Min | Max |
|---|---:|---|---:|---:|
| ed25519-record | 84060 | 83994–84123 | 83837 | 84890 |
| fs-hmac | 896 | 892–901 | 890 | 978 |
| fssagg-mac | 890 | 888–898 | 883 | 973 |
| sha256-chain | 315 | 314–316 | 313 | 331 |

## S5. Tamper detection (1 = detected)

|                                                            |   ed25519-record |   fs-hmac |   fssagg-mac |   sha256-chain |
|:-----------------------------------------------------------|-----------------:|----------:|-------------:|---------------:|
| ('backdate-model-version', 'storage', 'anchored')          |                1 |         1 |            1 |              1 |
| ('delete-record', 'storage', 'anchored')                   |                1 |         1 |            1 |              1 |
| ('delete-record', 'storage', 'unanchored')                 |                1 |         1 |            1 |              0 |
| ('forge-record', 'auditor-with-K0', 'unanchored')          |                1 |         0 |            0 |              0 |
| ('insert-backdated-model-update', 'storage', 'unanchored') |                1 |         1 |            1 |              0 |
| ('modify-decision', 'storage', 'anchored')                 |                1 |         1 |            1 |              1 |
| ('modify-decision', 'storage', 'unanchored')               |                1 |         1 |            1 |              0 |
| ('reorder-records', 'storage', 'unanchored')               |                1 |         1 |            1 |              0 |
| ('rewrite-suffix', 'node-compromise', 'unanchored')        |                0 |         1 |            1 |              0 |
| ('truncate-tail', 'node-compromise', 'unanchored')         |                0 |         0 |            1 |              0 |
| ('truncate-tail', 'storage', 'unanchored')                 |                0 |         0 |            1 |              0 |

## S6. Drift monitor on UNSW-NB15

EWMA α = 0.02; threshold τ = 1.05 × maximum score on 5,000 held-out normal training flows; reference statistics from 51,000 normal training flows. Phase A: normal test flows; phase B: one attack category. Indices count decisions; −1 = never.

| category       |   threshold |   calib_max_score |   phase_a |   phase_b |   false_alarm_at |   detected_after |   max_score_a |   max_score_b |
|:---------------|------------:|------------------:|----------:|----------:|-----------------:|-----------------:|--------------:|--------------:|
| Analysis       |      0.6993 |             0.666 |      5000 |       677 |              253 |                0 |         1.374 |         2.96  |
| Backdoor       |      0.6993 |             0.666 |      5000 |       583 |              226 |               11 |         1.563 |         2.538 |
| DoS            |      0.6993 |             0.666 |      5000 |      2000 |              233 |               12 |         1.298 |        17.11  |
| Exploits       |      0.6993 |             0.666 |      5000 |      2000 |              275 |                7 |         1.563 |        23.52  |
| Fuzzers        |      0.6993 |             0.666 |      5000 |      2000 |              231 |                0 |         1.387 |         3.797 |
| Generic        |      0.6993 |             0.666 |      5000 |      2000 |              200 |                4 |         1.615 |         6.403 |
| Reconnaissance |      0.6993 |             0.666 |      5000 |      2000 |              207 |                3 |         1.584 |         2.018 |
| Shellcode      |      0.6993 |             0.666 |      5000 |       378 |              263 |                5 |         1.784 |         1.985 |
