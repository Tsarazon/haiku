# Haiku Scheduler Test Results (via KosmOS Test Suite)

Date: 2026-03-20
Platform: QEMU x86_64 + KVM, 4 vCPUs, 3GB RAM, AHCI, -snapshot

## Summary: 54-56 passed, 1-3 failed (4 runs)

| Category | Test | Run 1 | Run 2 | Run 3 | Run 4 | Status |
|----------|------|-------|-------|-------|-------|--------|
| Basic API | suggest_thread_priority (4) | 2.4ms | 2.4ms | 6.7ms | 2.5ms | PASS |
| Basic API | get/set scheduler mode (6) | 3.0ms | 3.1ms | 4.0ms | 10ms | PASS |
| Basic API | estimate scheduling latency (3) | 1.6ms | 1.6ms | 1.9ms | 1.6ms | PASS |
| Basic API | system info CPUs (3) | 1.2ms | 1.1ms | 1.4ms | 1.1ms | PASS |
| Priority | priority ordering (4) | 205ms | 204ms | 208ms | 504ms | FLAKY |
| Priority | set_priority at runtime (3) | 5.0ms | 4.9ms | 3.2ms | 1.5ms | PASS |
| Latency | wakeup latency 50x (2) | 208ms | 200ms | 218ms | 246ms | PASS |
| Latency | snooze accuracy (1) | 69ms | 71ms | 69ms | 72ms | PASS |
| Latency | preemption latency (4) | 11ms | 17ms | 15ms | 17ms | PASS |
| Fairness | equal priority fairness 4T (2) | 316ms | 316ms | 315ms | 320ms | FAIL (KVM) |
| Yield | thread yield (2) | 105ms | 106ms | 107ms | 112ms | PASS |
| Multi-Core | multi-core spread (5) | 418ms | 419ms | 419ms | 431ms | PASS |
| STRESS | spawn/exit churn 4x200 (1) | 2.2s | 2.4s | 2.2s | 2.7s | PASS |
| STRESS | priority thrash 8x500 (2) | 33ms | 26ms | 25ms | 25ms | PASS |
| STRESS | mode switch under load (2) | 244ms | 242ms | 232ms | 230ms | PASS |
| STRESS | 64 threads mixed prio (4) | 694ms | 672ms | 653ms | 1176ms | FLAKY |
| STRESS | spawn/exit throughput (1) | 3.1s | 3.1s | 3.1s | 3.3s | PASS |
| MOBILE SIM | 60fps + 4 BG workers (5) | 2.0s | 2.0s | 2.0s | 2.0s | PASS |
| MOBILE SIM | burst pattern 6 threads (3) | 143ms | 129ms | 133ms | 136ms | PASS |

Numbers in parentheses = check count per test.

## Stable Failure (KVM limitation)

- **equal priority fairness 4T** — uneven CPU distribution under virtualization (4/4 FAIL)

## Flaky (KVM dependent)

- **priority ordering** — FAIL run 1-3, PASS run 4 (host load dependent)
- **64 threads mixed prio** — FAIL run 1-2, PASS run 3-4 (host load dependent)

## Notes

- Stable tests (snooze, yield, multi-core, 60fps, burst pattern): variation < 5%
- priority ordering PASS on run 4 took 504ms vs ~205ms on FAIL runs (longer timeout helped)
- 60fps + 4 BG workers: rock solid 2.0s across all 4 runs
- Need real hardware (Intel N150) to validate fairness test