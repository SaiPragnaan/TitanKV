# TitanKV v1 benchmark run

Executed locally on 2026-09-12 with GCC 11.4, `-O2`, and 20,000 operations per workload. These are machine-specific observations, not portable resume claims.

Total Test time (real) =   0.42 sec
write_test ops=20000 ops/sec=47093.53 p50_us=0.94 p95_us=2.16 p99_us=20.73
read_test ops=20000 ops/sec=312879.62 p50_us=2.34 p95_us=6.77 p99_us=10.76
mixed_workload ops=20000 ops/sec=132435.13 p50_us=2.37 p95_us=8.52 p99_us=13.78
concurrency_test ops=20000 ops/sec=42302.91 p50_us=2.04 p95_us=119.57 p99_us=1752.59
recovery_test recovery_us=72820.24 recovered_keys=20000
fault validation : 5,000 deterministic injected failure events, replay-checked every 100 events