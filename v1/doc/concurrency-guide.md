# Concurrency Guide for Copico Centipede Firmware

This document explains how the two RP2350 cores interact in the Copico Centipede firmware,
the rationale behind key design decisions, and the pitfalls that can break things in future edits.

## Architecture Overview

The RP2350B has two ARM Cortex-M33 cores. The firmware uses them as follows:

| Core | Name | Role | Speed Constraint |
|------|------|------|------------------|
| Core 1 | **Foreground** | Bus cycle processing via PIO (gerbil), driving data bus, intercepting I/O | Must respond within ~1.1 µs (one 6809 bus cycle at 0.9 MHz) |
| Core 0 | **Background** | USB I/O, cycle logging, floppy disk emulation, Tcl console (SpoonFeeder) | Latency-tolerant, but must keep up with average throughput |

The foreground has interrupts disabled (`save_and_disable_interrupts()`).
The background runs with interrupts enabled (TinyUSB needs them for USB CDC).

## Communication Channels

### `fg2bg`: Foreground → Background FIFO

```
CrossCoreFIFO<uint, 8192> fg2bg;
```

This is the primary channel. The foreground pushes 32-bit words encoding a chore type (8 bits),
address (16 bits), and data byte (8 bits):

```cpp
#define PUSH_TO_BG(T, A, D) fg2bg.push(((T) << 24) | ((A) << 8) | (D))
```

Chore types (from `FifoNumbers` enum):

| Chore | Pushed by | Processed by | Purpose |
|-------|-----------|-------------|---------|
| `FG2BG_PUTCHAR` | Foreground | Background | Send a character to USB stdout |
| `FG2BG_READ` | Foreground | Background | Log a read bus cycle |
| `FG2BG_WRITE` | Foreground | Background | Log a write bus cycle |
| `FG2BG_SPOON_ON_RESET` | Foreground | Background | Trigger SpoonFeeder console on RESET |
| `FG2BG_NMI` | Foreground | Background | Release NMI line + log (NMI was already asserted by foreground) |
| `FG2BG_FLOPPY_COMMAND` | Foreground | Background | Process floppy command (triggers USB disk I/O) |
| `FG2BG_FLOPPY_LATCH` | Foreground | Background | Update floppy drive select latch |
| `FG2BG_W_256` | Foreground | Background | Send 256-byte sector over USB (floppy write) |
| `FG2BG_PEEK_REPLY` | Foreground | Background | Reply to a BG2FG_PEEK from SpoonFeeder |

> [!IMPORTANT]
> `fg2bg.push()` returns `false` when the FIFO is full, and **the return value is not checked**.
> The watermark-based flow control (below) prevents overflow in practice, but any new code
> that pushes to fg2bg at high rates must be aware of this.

### `bg2fg`: Background → Foreground FIFO

```
CrossCoreFIFO<uint, 8192> bg2fg;
```

Used only during SpoonFeeder console mode. The background pushes commands for the foreground
to execute (peek, poke, exit):

| Chore | Purpose |
|-------|---------|
| `BG2FG_PEEK` | Read a byte from the 6809 address space |
| `BG2FG_POKE` | Write a byte to the 6809 address space |
| `BG2FG_EXIT_CONSOLE` | Signal DriveConsole to exit (on "bye") |

The foreground polls `bg2fg` inside `DriveConsole()`. Outside of console mode,
`bg2fg` is not polled.

## Flow Control: The Watermark System

### The Problem

At `SLOW_SPEED`, every read bus cycle pushes an `FG2BG_READ` entry to `fg2bg`.
At 0.9 MHz, that's ~900K pushes/sec. The background drains via `putchar_raw()`
over USB CDC (12 Mbps ≈ 300K entries/sec of 4 bytes each). When the drain can't
keep up, the FIFO fills and pushes are silently dropped.

### The Solution

The **foreground** manages flow control by checking the FIFO level every bus cycle
and asserting/releasing HALT on the 6809:

```cpp
#define FG2BG_HIGH_WATERMARK 80
#define FG2BG_LOW_WATERMARK  40

FORCE_INLINE void FlowControlCheck() {
  if (fg_halt_for_flow_control) {
    if (fg2bg.size() < FG2BG_LOW_WATERMARK) {
      HaltOff();
      fg_halt_for_flow_control = false;
    }
  } else {
    if (fg2bg.size() > FG2BG_HIGH_WATERMARK) {
      HaltOn();
      fg_halt_for_flow_control = true;
    }
  }
}
```

Key design decisions:

- **Hysteresis** (80/40): Prevents rapid HALT oscillation. The gap between high and low
  watermarks means the CPU runs in bursts of ~40 cycles between HALTs.
- **Low watermarks** (80 out of 8192): Keeps halting smooth and brief rather than in
  large pauses. At 0.9 MHz, 80 cycles ≈ 89 µs — imperceptible.
- **Called every cycle**: `FlowControlCheck()` runs at the top of the foreground cycle
  loop, right after `GERBIL_GET()`. This ensures the CPU is halted promptly.

### Write Cycles Are Never Dropped

```cpp
FORCE_INLINE static void PushFifoWrite(uint abus, byte dbus) {
    if (Speed <= MEDIUM_SPEED) {  // No flow control check!
      PUSH_TO_BG(FG2BG_WRITE, abus, dbus);
    }
  }
```

Write cycles are always pushed regardless of `fg_halt_for_flow_control`. Rationale:

1. Writes are rare (~20% of bus cycles) — they won't overflow the FIFO.
2. Writes are critical for the tether's virtual CoCo screen display.
3. When HALT is asserted, the CPU generates at most 1-2 more writes before halting.

Only **read** cycle pushes (`PushFifoRead`) are suppressed during flow control.

### Background's Role in HALT

The background calls `HaltOff()` at the top of each loop iteration:

```cpp
HaltOff();  // Release HALT before blocking on FIFO pop
const uint chore = BLOCKING_PULL_FROM_FG();
```

This prevents a deadlock: if the background blocks on `putchar_raw()` (USB TX full)
while HALT is asserted, the foreground keeps running `FlowControlCheck()` and sees
the FIFO isn't draining. Without the background's `HaltOff()`, the system would
deadlock because `putchar_raw` needs USB interrupts, which need time to fire.

The foreground's `FlowControlCheck` re-asserts HALT on the very next bus cycle if
the FIFO is still above the watermark. This creates a "pulse" pattern: brief runs,
brief halts — smooth throughput throttling.

> [!CAUTION]
> **Snag**: Both cores toggle the same HALT GPIO. The last write wins. If the
> background calls `HaltOff()` while the foreground wants HALT asserted, a few CPU
> cycles slip through unlogged. This is acceptable — the alternative (a mutex)
> would be too slow for the foreground's cycle loop.

## The Gerbil PIO Protocol

The PIO state machine (`gerbil.pio`) monitors the 6809's bus signals (Q, E, RW)
and communicates with the foreground via the PIO FIFOs.

### Protocol Per Bus Cycle

**Read cycle** (RW=1, CPU reading):
1. PIO samples all 32 GPIO pins, pushes to RX FIFO (`GERBIL_GET()` in foreground)
2. PIO executes `pull block` — waits for foreground response
3. Foreground sends either:
   - `GERBIL_DRIVE(data)` → PIO drives data bus, waits for EBAR rise, releases
   - `GERBIL_PASS()` → PIO enters `read_dont_drive` loop, samples bus, pushes a
     second word to RX FIFO (foreground must consume it via another `GERBIL_GET()`)

**Write cycle** (RW=0, CPU writing):
1. PIO pushes address sample (RX FIFO)
2. PIO waits for QBAR rise, pushes data sample (RX FIFO)
3. Foreground calls `GERBIL_GET()` twice (address + data)

> [!WARNING]
> **Snag: PIO Lockstep**. The foreground MUST respond to every PIO cycle promptly.
> If the foreground is too slow (because of heavy computation in the cycle loop),
> the PIO's 4-deep RX FIFO overflows and the protocol desyncs. This causes the
> foreground to serve wrong data to wrong addresses — the 6809 reads garbage
> opcodes and crashes into random memory.
>
> **Rule**: Never add expensive operations (USB I/O, blocking waits, large loops)
> to the foreground cycle loop. Keep it to simple GPIO reads, array lookups, and
> FIFO pushes.

### The "All Reads Show 00" Artifact

When the foreground calls `GERBIL_PASS()`, the PIO's `read_dont_drive` path samples
the data bus and pushes the result. This is used for logging what the CoCo's own
ROM/RAM chips are putting on the bus. However, if timing is off (foreground was
slightly slow), the source device may have already released the bus, and the PIO
samples 0x00 from floating bus capacitance.

This means logged read data for non-driven addresses (where the CoCo's ROM provides
data) may show 0x00 in the log even though the CPU received the correct data.
The CPU execution is correct — only the log is wrong.

## Floppy Disk Emulation

### Data Flow

```
CPU writes $FF48 (command) →
  foreground: WriteScsFloppy sets floppy_status=BUSY, pushes FG2BG_FLOPPY_COMMAND →
    background: sends C_DISK_READ over USB, calls ReceiveSectorData() →
      tether: reads disk image, sends 256-byte packet back →
        background: loads floppy_buf[], sets floppy_status=DRQ (release) →
          foreground: CPU polls $FF48 status, sees DRQ (acquire), reads $FF4B →
            foreground: ReadScsFloppy returns bytes from floppy_buf[] →
              after 256 bytes: foreground asserts NMI directly on GPIO
```

### The DRQ Race (Fixed)

When the CPU writes a READ SECTOR command ($80) to $FF48, the foreground pushes
`FG2BG_FLOPPY_COMMAND` to fg2bg. At `SLOW_SPEED`, this message gets queued behind
hundreds of `FG2BG_READ` entries. The background takes a long time to reach it.

**The old bug**: the foreground set `floppy_status = 0x02` (DRQ) immediately. The CPU
polled status, saw DRQ, and started reading $FF4B — but the background hadn't loaded
the sector data into `floppy_buf` yet. The CPU got stale/wrong data.

**The fix**: two-phase status handshake:
1. **Foreground** (on command write): `floppy_status.store(0x01, relaxed)` — BUSY only
2. **Background** (after `ReceiveSectorData`): `floppy_status.store(0x02, release)` — DRQ
3. **Foreground** (on status read): `floppy_status.load(acquire)` — sees DRQ only after data is ready

### Memory Ordering on `floppy_status`

`floppy_status` is `std::atomic<byte>` with explicit memory ordering:

| Who | Operation | Ordering | Why |
|-----|-----------|----------|-----|
| Background | `store(0x02)` after ReceiveSectorData | `release` | Ensures all 256 bytes of `floppy_buf[]` are committed before DRQ is visible |
| Foreground | `load()` on status read ($FF48) | `acquire` | Ensures foreground sees all `floppy_buf[]` data when it observes DRQ |
| Foreground | `store(0x01)` on command write | `relaxed` | No cross-core data to synchronize |
| Foreground | `store(dbus & 1)` clear after read | `relaxed` | Foreground-only, no cross-core dependency |

`floppy_ptr` is `volatile byte*` because both cores write to it (background resets it
after loading data; foreground increments it during reads). The `release` barrier on
`floppy_status.store(0x02)` ensures the `floppy_ptr = floppy_buf` write is also visible.

> [!CAUTION]
> **Snag**: If you add a new shared variable between foreground and background for
> floppy state (e.g., a sector counter), it MUST be written before the `release` store
> and read after the `acquire` load, or it will have the same stale-data bug.
> `volatile` alone is NOT sufficient for cross-core visibility on ARM — you need
> `std::atomic` with release/acquire, or explicit `__dmb()` barriers.

### NMI Assertion

The floppy triggers NMI when all 256 bytes of a sector have been read (DRQ complete).

**The old bug**: NMI was asserted by the background via `FG2BG_NMI` in fg2bg. At
`SLOW_SPEED`, the NMI message was queued behind hundreds of read cycle entries. By the
time the background popped and asserted the NMI GPIO, the CPU had moved on and the
floppy operation timed out.

**The fix**: NMI is asserted **directly by the foreground** using `ASSERT_NMI()` on the
exact bus cycle when the 256th byte is read. The `FG2BG_NMI` message is still pushed
to fg2bg so the background can `RELEASE_NMI()` (set GPIO back to input) and log it.

```cpp
// In ReadScsFloppy, case 0xB (ReadData):
dbus = *floppy_ptr++;
if ((floppy_latch & 0x80) != 0 && floppy_ptr >= floppy_limit) {
    floppy_ptr = floppy_buf;
    ASSERT_NMI();                         // Immediate — foreground GPIO
    PUSH_TO_BG(FG2BG_NMI, 0, 0);         // Deferred — background releases + logs
}
```

> [!WARNING]
> **Snag**: Any future code that needs to assert a hardware signal (NMI, IRQ, etc.)
> in response to a bus event MUST do so directly from the foreground, not by routing
> through fg2bg. The FIFO adds variable latency (proportional to queued cycle log
> entries), which can be hundreds of cycles at `SLOW_SPEED`.

## SpoonFeeder Console Mode

On RESET, the foreground calls `SpoonfeedConsoleOnReset()` which:
1. Pushes `FG2BG_SPOON_ON_RESET` to start the background's SpoonFeeder
2. Pokes startup values into CoCo memory (SAM, frame buffer)
3. Enters `DriveConsole()` — a specialized foreground loop

### DriveConsole

`DriveConsole()` is an alternate foreground loop that processes bus cycles while also
polling `bg2fg` for peek/poke/exit commands from SpoonFeeder. It does NOT call
`FlowControlCheck()` or push cycle log entries — only console I/O matters.

### SpoonFeeder

`SpoonFeeder()` runs on the background. It implements a simple Tcl-like console
using peek/poke commands via `bg2fg`. When the user types "bye":

1. **Background**: SpoonFeeder pushes `BG2FG_EXIT_CONSOLE`, returns
2. **Foreground**: DriveConsole pops `BG2FG_EXIT_CONSOLE`, calls `Jump(0xA027)`,
   returns to `SpoonfeedConsoleOnReset()`, returns to `foreground()`
3. Both cores resume normal operation

> [!CAUTION]
> **Snag: No HALT during console mode**. `DriveConsole()` must NOT call `HaltOn()`.
> The 6809 is halted during console mode implicitly (it's executing startup code fed
> by `DriveConsole`'s own bus cycle processing). Asserting HALT in console mode
> causes a deadlock because the foreground is in a different event loop that doesn't
> release HALT.

## Snags For The Unwary: Summary

### 1. Never block in the foreground
The foreground must respond to each PIO bus cycle within ~278 ARM clock cycles
(250 MHz ÷ 0.9 MHz). Any blocking operation (USB I/O, `sleep_us()`, waiting on
a mutex, large `for` loops) will cause the PIO to overflow and desync with the 6809.

### 2. Never route time-critical signals through fg2bg
The fg2bg FIFO adds variable latency. At `SLOW_SPEED`, hundreds of read cycle
entries may be queued ahead of your message. Assert hardware signals (NMI, etc.)
directly from the foreground GPIO.

### 3. Always use release/acquire for cross-core shared data
`volatile` prevents compiler reordering but does NOT provide inter-core memory
ordering on ARM. If core A writes data then writes a flag, core B may see the flag
before the data. Use `std::atomic<T>` with `memory_order_release` (writer) and
`memory_order_acquire` (reader), or explicit `__dmb()` barriers.

### 4. HALT is a shared resource with no locking
Both cores call `HaltOn()`/`HaltOff()` (GPIO direction toggle). The last call wins.
A few cycles may slip through unlogged when the background releases HALT while the
foreground wants it asserted. This is by design — the alternative (a lock) would be
too slow.

### 5. `putchar_raw()` can block
`putchar_raw()` blocks when the USB CDC TX buffer is full. If the background blocks
on `putchar_raw()` while HALT is asserted, the foreground can't drain the FIFO, and
the system deadlocks. The background's `HaltOff()` at the top of each loop iteration
prevents this.

### 6. `fg2bg.push()` silently drops data
The `PUSH_TO_BG` macro ignores the return value of `fg2bg.push()`. If the FIFO is
full, the data is silently lost. The watermark system prevents this in normal
operation, but any new high-frequency pushes must be aware of this.

### 7. Write cycles must never be dropped
The tether's virtual CoCo screen display depends on seeing every write cycle.
`PushFifoWrite` does not check `fg_halt_for_flow_control`. If you add new flow
control logic, ensure write pushes are always preserved.

### 8. Don't set DRQ before data is ready
If the foreground sets a "data ready" flag/status before the background has loaded
the data, the CPU will read stale buffer contents. Use the two-phase handshake:
foreground sets BUSY, background loads data, background sets DRQ with `release`
ordering.

### 9. FlowControlCheck must run every cycle
If you add an early `continue` or `break` in the foreground cycle loop that skips
`FlowControlCheck()`, HALT will never be asserted (FIFO overflow) or never released
(CPU frozen forever). Ensure every path through the loop calls it.

### 10. Console mode is a different world
During `DriveConsole()`, the foreground is NOT in the normal cycle loop. It does not
push to fg2bg, does not call FlowControlCheck, and does not process bg2fg outside of
DriveConsole. Any new cross-core communication designed for normal mode will not work
during console mode without explicit support.
