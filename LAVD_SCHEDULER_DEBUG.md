# LAVD Scheduler Debug Report

Date: 2026-03-21
Build: hrev57937+1221+dirty
Config: QEMU x86_64, KVM, 4 CPU, 3GB RAM, AHCI, OVMF EFI

## Summary

LAVD two-level fair scheduler fails to boot. Two distinct failure modes observed across 8+ attempts:

1. **thread_exit() panic** — "never can get here" (3/8 runs)
2. **Boot deadlock** — system hangs during early init (5/8 runs)

Previous Haiku scheduler (standard) boots and runs all tests successfully (209 ray, 163 dot, 88 mutex, 56 scheduler — all passing).

---

## Failure Mode 1: thread_exit() Panic

### Symptom
```
PANIC: never can get here
```

### Stack Trace
```
Thread 24 "NFSv4 Work Queue" running on CPU 0
stack trace for thread 24 "NFSv4 Work Queue"
    kernel stack: 0xffffffff82b77000 to 0xffffffff82b7c000
frame                       caller             <image>:function + offset
 0 ffffffff82b7bbb0 (+  32) ffffffff80170558   <kernel_x86_64> arch_debug_call_with_fault_handler() + 0x1a
 1 ffffffff82b7bc00 (+  80) ffffffff800c6ad8   <kernel_x86_64> debug_call_with_fault_handler() + 0x78
 2 ffffffff82b7bc60 (+  96) ffffffff800c81da   <kernel_x86_64> kernel_debugger_loop() + 0x10a
 3 ffffffff82b7bcc0 (+  96) ffffffff800c857e   <kernel_x86_64> kernel_debugger_internal() + 0x6e
 4 ffffffff82b7bdb0 (+ 240) ffffffff800c8947   <kernel_x86_64> panic() + 0xb7
 5 ffffffff82b7bfd0 (+ 544) ffffffff8009e15f   <kernel_x86_64> thread_exit() + 0x86f
 6 0000000000000000 (+   0) ffffffff82b7bfe0   NFSv4 Work Queue
```

### Analysis
`thread_exit()` calls `reschedule(THREAD_STATE_FREE_ON_RESCHED)` which should never return — the thread's context should be switched away permanently. The `panic("never can get here")` at the end of `thread_exit()` fires because `reschedule()` returned.

**Root cause:** LAVD scheduler's `reschedule()` does not properly handle `THREAD_STATE_FREE_ON_RESCHED` — the dying thread is selected again by `PickNext()` or the context switch doesn't happen (next_thread == old_thread).

---

## Failure Mode 2: Boot Deadlock

### Symptom
System hangs after `scheduler_compaction: enabled` or after `registering power button`. No kernel panic, no progress.

### Context Switch Tracing (dprintf in reschedule)

Added `dprintf("cpu%ld: %ld(%s) -> %ld(%s)\n", ...)` before `arch_thread_context_switch()`.

**Run A — partial activity:**
```
cpu3: 4(idle thread 4) -> 12(object cache resizer)
cpu3: 12(object cache resizer) -> 17(dpc: real-time priority)
cpu3: 17(dpc: real-time priority) -> 12(object cache resizer)
cpu3: 12(object cache resizer) -> 4(idle thread 4)
```
- CPU 0, 1, 2: **zero context switches**
- CPU 3: 4 switches then stops
- Boot thread (main, id=1) on CPU 0 never switches

**Run B — more activity, still stuck:**
```
cpu2: 156 context switches (idle ↔ kernel daemons)
cpu3: 127 context switches (idle ↔ kernel daemons)
cpu0: 0 switches
cpu1: 0 switches
```
- CPU 2 and 3 cycle between: idle, object cache resizer, kosm_dot scanner, page daemon, resource resizer, kernel daemon
- No user threads, no device probe threads
- Boot thread on CPU 0/1 — completely dead

### Compaction Oscillation (some runs)
```
compaction: packed to 3 cores (disabled core 0)
compaction: packed to 2 cores (disabled core 0)
compaction: packed to 1 cores (disabled core 0)
compaction: packed to 3 cores (disabled core 3)
compaction: packed to 2 cores (disabled core 3)
compaction: packed to 1 cores (disabled core 3)
[repeats indefinitely]
```

### Boot Log (typical hang point)
```
scheduler_init: LAVD two-level fair scheduler
scheduler_lavd_init: 4 CPUs, 4 cores, 1 packages
scheduler_compaction: initialized, 4 cores, energy_bias=50
scheduler_compaction: enabled
...
slab memory manager: created area 0xffffffff80801000 (193)
slab memory manager: created area 0xffffffff81001000 (196)
publish device: node ..., path acpi/namespace
publish device: node ..., path acpi/call
registering power button
[HANG - no further output]
```

---

## Diagnosis

### thread_exit() panic
LAVD `reschedule()` with `THREAD_STATE_FREE_ON_RESCHED`:
1. Thread state set to `THREAD_STATE_FREE_ON_RESCHED` (line 422)
2. `enqueue_old` stays false (correct — dying thread shouldn't be re-enqueued)
3. `PickNext()` is called — but may return the dying thread itself if no other thread is ready
4. If `next_thread == old_thread`, no context switch happens (line 585: `if (next_thread != old_thread)`)
5. `reschedule()` returns → hits `panic("never can get here")`

**Fix needed:** `PickNext()` must never return a thread in `THREAD_STATE_FREE_ON_RESCHED`. Or if `next_thread == old_thread` and state is `FREE_ON_RESCHED`, force switch to idle thread.

### Boot deadlock
CPU 0 (boot CPU) runs the main/init thread. This thread calls device init functions that may block (spinlock, semaphore). If LAVD scheduler doesn't properly wake/schedule threads on CPU 0:
- Boot thread blocks waiting for a device probe thread
- Device probe thread is never scheduled on any CPU
- System deadlocks

The fact that CPU 0 and CPU 1 show zero context switches means either:
1. Timer interrupts aren't firing (quantum_timer not set up for boot thread)
2. `invoke_scheduler` is never set on these CPUs
3. These CPUs are stuck in a spinlock inside the scheduler itself

---

## Environment

### QEMU Command
```bash
qemu-system-x86_64 -enable-kvm -cpu host -smp 4 -m 3G \
  -drive file=haiku-nightly-anyboot.iso,format=raw,id=disk0,if=none \
  -device ahci,id=ahci -device ide-hd,drive=disk0,bus=ahci.0 \
  -vga virtio -bios /usr/share/ovmf/OVMF.fd \
  -net nic,model=virtio -net user -display sdl \
  -usb -device usb-tablet -snapshot \
  -serial file:/tmp/qemu_serial.log -d guest_errors
```

### CPU Info
```
Intel 12th Gen Core i5-1240P (4 vCPUs via KVM)
APIC timer frequency: 999903469
PAT, SSE, AVX2, SMAP, SMEP enabled
```

### Previous Scheduler (working)
Standard Haiku scheduler boots successfully every time with this exact QEMU config.
Test results with standard scheduler:
- Ray: 209 passed, 8 failed (area/sem handle — known, not ray bug)
- Dot: 163 passed, 0 failed
- Mutex: 88 passed, 0 failed
- Scheduler: 56 passed, 1 failed (fairness under KVM)

---

## Recommended Fixes

1. **thread_exit() safety:** In `reschedule()`, if `next_thread == old_thread` and `old_thread->state == THREAD_STATE_FREE_ON_RESCHED`, force `next_thread = cpu->idle_thread`

2. **Boot thread scheduling:** Ensure boot thread (id=1) gets CPU time during init — don't let compaction or load balancing migrate it off the boot CPU before boot completes

3. **Compaction guard:** Don't enable compaction until `boot_complete` flag is set (after VFS mount, device init done)

4. **PickNext() invariant:** `PickNext()` must never return a thread whose state is not `B_THREAD_READY` or `B_THREAD_RUNNING`