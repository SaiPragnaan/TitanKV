# TitanKV v1

TitanKV is a C++20, single-node persistent key-value server built to the scope in `plans/IMPLEMENTATION.md`: concurrent TCP handling, WAL recovery, an LSM storage engine, indexed SSTable reads with Bloom filters, compaction, benchmarks, and deterministic fault testing. Distributed replication is intentionally not implemented.

`titankv_baseline` is the deliberately single-threaded, RAM-only Phase-0 reference; it supports the same line commands on port 7380 by default.

## Build and run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/titankvd ./data 7379
```

The line protocol accepts `SET key value`, `GET key`, `DELETE key`, `PING`, and `STATS`.

## Validation and benchmarks

```bash
ctest --test-dir build --output-on-failure
./build/write_test
./build/read_test
./build/mixed_workload
./build/concurrency_test
./build/recovery_test
```

`titankv_tests` runs core persistence/compaction tests plus 5,000 seeded injected failure events, with replay checks after every 100 events. Benchmark output reports operations/sec and p50/p95/p99 microsecond latency. Results are machine-dependent; do not substitute the illustrative resume figures in the plan for measured results.

## Durability model

Writes are appended to the WAL before they enter the MemTable. Default writes call `fdatasync`; benchmarks use batched syncing to measure throughput. SSTables are written to a temporary path and atomically renamed. The WAL is retained, so recovery replays it and sequence numbers ensure last-write-wins across MemTable and SSTables.
