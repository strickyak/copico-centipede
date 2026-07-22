# Cooperative Multitasking Sketch for Background Core

## The Problem (Recap)

The current background is one `while(1)` loop with a `switch` statement.
When any handler blocks (floppy USB I/O, putchar_raw, SpoonFeeder),
the entire background stalls — fg2bg stops draining, flow control kicks in,
and the 6809 halts.

## Proposed Architecture

Replace the single background loop with **4 coroutines** running in a
cooperative round-robin scheduler. Each coroutine has its own stack and
yields explicitly.

```
┌─────────────────────────────────────────────────┐
│  background() — Round-Robin Scheduler           │
│                                                 │
│   ┌──────────┐  ┌──────────┐  ┌──────────┐     │
│   │  Drain   │  │  Floppy  │  │  Spoon   │     │
│   │  Task    │  │  Task    │  │  Task    │     │
│   └────┬─────┘  └────┬─────┘  └────┬─────┘     │
│        │yield()       │yield()      │yield()    │
│        └──────────────┴─────────────┘           │
│                                                 │
│   PumpUsbCobs() runs between every resume       │
└─────────────────────────────────────────────────┘
```

---

## Coroutine Primitive

A minimal stackful coroutine using ARM Cortex-M33 context switching.
Each coroutine gets a 4KB stack (the RP2350 has 520KB RAM — plenty).

```cpp
// coro.h — Minimal cooperative coroutine for RP2350

#include <csetjmp>
#include <cstdint>

struct Coro {
    jmp_buf caller_ctx;   // scheduler's context
    jmp_buf coro_ctx;     // coroutine's context
    uint8_t* stack;
    bool started;
    bool finished;
    void (*func)(Coro& self);

    Coro(void (*f)(Coro&), uint8_t* stack_mem, uint32_t stack_size)
        : stack(stack_mem), started(false), finished(false), func(f) {
        // Stack grows downward on ARM. Initial SP = top of stack.
        // Will be set up on first resume().
    }

    // Called by scheduler to run the coroutine until it yields.
    void resume() {
        if (finished) return;
        if (!started) {
            started = true;
            if (setjmp(caller_ctx) == 0) {
                // Switch to coroutine's stack and call func.
                // On ARM Cortex-M33, manipulate MSP/PSP:
                register uint32_t new_sp = (uint32_t)(stack);
                asm volatile("mov sp, %0" :: "r"(new_sp));
                func(*this);
                finished = true;
                longjmp(caller_ctx, 1);  // Return to scheduler
            }
        } else {
            if (setjmp(caller_ctx) == 0) {
                longjmp(coro_ctx, 1);    // Resume where coroutine yielded
            }
        }
    }

    // Called from inside the coroutine to give up control.
    void yield() {
        if (setjmp(coro_ctx) == 0) {
            longjmp(caller_ctx, 1);      // Return to scheduler
        }
        // When resumed, execution continues here.
    }
};
```

> [!NOTE]
> The `setjmp`/`longjmp` with stack manipulation shown above is illustrative.
> A production implementation would use a small (~20 instruction) naked
> assembly function to save/restore `r4-r11`, `sp`, and `lr` — avoiding
> `setjmp`'s overhead and portability quirks. The API stays the same.

---

## Channel Primitive

A bounded, single-producer/single-consumer channel, inspired by Go's `chan`.
`send()` and `recv()` yield when the channel is full or empty.

```cpp
// chan.h — Bounded channel with cooperative blocking

template<typename T, int N>
struct Chan {
    T buf[N];
    volatile int head = 0;
    volatile int tail = 0;
    Coro* sender_coro = nullptr;    // Who calls send()
    Coro* receiver_coro = nullptr;  // Who calls recv()

    bool empty() const { return head == tail; }
    bool full()  const { return ((head + 1) % N) == tail; }

    // Blocking send — yields until space is available.
    void send(Coro& self, const T& val) {
        sender_coro = &self;
        while (full()) self.yield();
        buf[head] = val;
        head = (head + 1) % N;
    }

    // Blocking recv — yields until data is available.
    T recv(Coro& self) {
        receiver_coro = &self;
        while (empty()) self.yield();
        T val = buf[tail];
        tail = (tail + 1) % N;
        return val;
    }

    // Non-blocking try_send — for use from foreground (no coroutine).
    bool try_send(const T& val) {
        if (full()) return false;
        buf[head] = val;
        head = (head + 1) % N;
        return true;
    }
};
```

---

## The Transformed Background

### Current Code (simplified)

```cpp
void background() {
    while (1) {
        HaltOff();
        const uint chore = BLOCKING_PULL_FROM_FG();  // blocks on fg2bg
        // ... all of these happen SEQUENTIALLY:
        switch (chore >> 24) {
            case FG2BG_READ:    putchar_raw(...); break;      // can block on USB
            case FG2BG_WRITE:   putchar_raw(...); break;      // can block on USB
            case FG2BG_NMI:     release_nmi(); break;
            case FG2BG_FLOPPY_COMMAND:
                ReceiveSectorData();   // BLOCKS for milliseconds!
                break;
            case FG2BG_SPOON_ON_RESET:
                SpoonFeeder();         // BLOCKS for MINUTES!
                break;
        }
    }
}
```

### Proposed Code

```cpp
// Stacks for each coroutine (4KB each, 12KB total)
uint8_t drain_stack[4096] __attribute__((aligned(8)));
uint8_t floppy_stack[4096] __attribute__((aligned(8)));
uint8_t spoon_stack[4096] __attribute__((aligned(8)));

// Channels for dispatching work from drain to other tasks
Chan<uint, 4> floppy_chan;   // drain → floppy task
Chan<uint, 4> spoon_chan;    // drain → spoon task

//------------------------------------------------------------
// Task 1: DRAIN — pops from fg2bg and dispatches
//
// This is the ONLY task that reads fg2bg.
// It handles simple chores inline and dispatches
// complex ones to other tasks via channels.
//------------------------------------------------------------
void drain_task(Coro& self) {
    while (true) {
        HaltOff();

        uint chore;
        while (!fg2bg.pop(chore)) {
            PumpUsbCobs();
            self.yield();  // Let other tasks run while FIFO is empty
        }

        uint chore_num = chore >> 24;
        byte chore_byte = chore & 0xFF;

        switch (chore_num) {
            case FG2BG_PUTCHAR:
                putchar_raw(chore_byte);
                break;

            case FG2BG_READ:
                // Cycle log — handle inline (fast)
                putchar_raw(C_RAM2_READ);
                putchar_raw(chore_byte);
                putchar_raw((chore >> 8) & 0xFF);
                putchar_raw((chore >> 16) & 0xFF);
                break;

            case FG2BG_WRITE:
                putchar_raw(C_RAM2_WRITE);
                putchar_raw(chore_byte);
                putchar_raw((chore >> 8) & 0xFF);
                putchar_raw((chore >> 16) & 0xFF);
                break;

            case FG2BG_NMI:
                RELEASE_NMI();
                // Log inline (fast)
                putchar_raw(C_LOGGING);
                putchar_raw(4 + 128);
                putchar_raw('N'); putchar_raw('M');
                putchar_raw('I'); putchar_raw('\n');
                break;

            case FG2BG_FLOPPY_COMMAND:
            case FG2BG_FLOPPY_LATCH:
            case FG2BG_W_256:
                // Dispatch to floppy task (non-blocking try_send)
                floppy_chan.try_send(chore);
                break;

            case FG2BG_SPOON_ON_RESET:
                // Dispatch to spoon task
                spoon_chan.try_send(chore);
                break;
        }
    }
}

//------------------------------------------------------------
// Task 2: FLOPPY — handles disk I/O
//
// Receives floppy chores from drain_task via floppy_chan.
// Can yield() while waiting for USB sector data without
// blocking the drain task.
//------------------------------------------------------------
void floppy_task(Coro& self) {
    while (true) {
        uint chore = floppy_chan.recv(self);  // yields until work arrives
        byte chore_byte = chore & 0xFF;
        uint chore_num = chore >> 24;

        switch (chore_num) {
            case FG2BG_FLOPPY_LATCH:
                BackgroundFifoFloppyLatch(chore_byte);
                break;

            case FG2BG_FLOPPY_COMMAND:
                switch (chore_byte) {
                    case 0x80: {  // read sector
                        // Send request to tether
                        putchar_raw(C_DISK_READ);
                        putchar_raw(5 + 128);
                        putchar_raw('f');
                        putchar_raw(chore_byte);
                        putchar_raw(floppy_latch);
                        putchar_raw(floppy_track);
                        putchar_raw(floppy_sector);

                        // Wait for USB response — YIELDING instead of blocking!
                        std::string* pkt = nullptr;
                        while (!pkt) {
                            PumpUsbCobs();
                            pkt = usb_packet_buf.Yoink([](std::string* s) {
                                return s && s->length() > 0 &&
                                       (byte)(*s)[0] == 173;
                            });
                            if (!pkt) self.yield();  // ← THE KEY DIFFERENCE
                        }

                        // Load data
                        for (int i = 0; i < 256; i++)
                            floppy_buf[i] = (*pkt)[pkt->length() - 256 + i];
                        delete pkt;

                        floppy_ptr = floppy_buf;
                        floppy_status.store(0x02, std::memory_order_release);
                        break;
                    }
                    // ... case 0xA0 (write), case 0x17 (seek) ...
                }
                break;

            case FG2BG_W_256:
                SendSectorData();
                break;
        }
    }
}

//------------------------------------------------------------
// Task 3: SPOONFEEDER — Tcl console
//
// Runs the interactive console. Can yield between
// command processing without blocking drain/floppy.
//------------------------------------------------------------
void spoon_task(Coro& self) {
    while (true) {
        uint chore = spoon_chan.recv(self);  // yields until RESET
        // Run SpoonFeeder, yielding periodically
        // (SpoonFeeder's internal loops would call self.yield())
        HaltOff();
        SpoonFeeder(self);  // Modified to accept Coro& and yield internally
    }
}

//------------------------------------------------------------
// Scheduler — the new background() entry point
//------------------------------------------------------------
void background() {
    Coro drain(drain_task,  drain_stack  + sizeof(drain_stack),
               sizeof(drain_stack));
    Coro floppy(floppy_task, floppy_stack + sizeof(floppy_stack),
                sizeof(floppy_stack));
    Coro spoon(spoon_task,  spoon_stack  + sizeof(spoon_stack),
               sizeof(spoon_stack));

    // Round-robin scheduler
    while (true) {
        drain.resume();     // Always runs — drains fg2bg
        PumpUsbCobs();      // Pump USB between every task switch
        floppy.resume();    // Runs if floppy work is pending
        PumpUsbCobs();
        spoon.resume();     // Runs if console is active
        PumpUsbCobs();
    }
}
```

---

## Why This Is Better

### Before: ReceiveSectorData blocks everything

```
Time ──────────────────────────────────────────────►

Background: ║ drain ║ drain ║ drain ║▓▓ FLOPPY USB WAIT (blocked) ▓▓║ drain ║
fg2bg:      ║  ok   ║  ok   ║  ok   ║▓▓▓▓▓ FILLING UP ▓▓▓▓▓▓▓▓▓▓▓▓║  ok   ║
6809:       ║ runs  ║ runs  ║ runs  ║▓▓▓▓▓ HALTED (flow ctrl) ▓▓▓▓▓║ runs  ║
```

### After: Floppy yields, drain keeps running

```
Time ──────────────────────────────────────────────►

drain:   ║ pop ║ pop ║ pop ║ pop ║ pop ║ pop ║ pop ║ pop ║ pop ║ pop ║
floppy:  ║     ║ wait║     ║ wait║     ║ wait║     ║RECV!║ ──► DRQ   ║
fg2bg:   ║ ok  ║ ok  ║ ok  ║ ok  ║ ok  ║ ok  ║ ok  ║ ok  ║ ok  ║ ok  ║
6809:    ║ run ║ run ║ run ║ run ║ run ║ run ║ run ║ run ║ run ║ run ║
```

The 6809 never halts for floppy I/O. The drain task keeps processing
cycle logs while the floppy task yields waiting for USB data.

---

## What Changes For The Foreground

**Nothing.** The foreground still:
- Runs lock-step with the PIO
- Pushes to fg2bg via `PUSH_TO_BG`
- Calls `FlowControlCheck()` every cycle
- Asserts NMI directly (not through fg2bg)

The coroutine system is entirely on the background core.

---

## What Stays The Same

| Concern | Stays the same? | Why |
|---------|----------------|-----|
| FlowControlCheck watermarks | ✅ Yes | Still needed as a safety net |
| NMI asserted from foreground | ✅ Yes | Latency requirement unchanged |
| floppy_status release/acquire | ✅ Yes | Cross-core ordering unchanged |
| Write cycles always pushed | ✅ Yes | Virtual screen still needs all writes |
| fg2bg silently drops on overflow | ✅ Yes | But overflow is far less likely |
| HALT shared between cores | ✅ Yes | But contention is reduced |

---

## What Gets Simpler

| Current snag | With coroutines |
|-------------|----------------|
| putchar_raw blocks → deadlock | drain_task yields, scheduler pumps USB |
| ReceiveSectorData blocks → FIFO fills | floppy_task yields, drain keeps running |
| SpoonFeeder blocks for minutes | spoon_task yields, drain/floppy keep running |
| New USB RPC → new blocking point | Just yield() in the new task's wait loop |
| 10 snag rules to memorize | 6 remain (foreground-related only) |

---

## Implementation Cost

| Item | Estimate |
|------|----------|
| `Coro` struct + context switch assembly | ~50 lines |
| `Chan<T,N>` template | ~30 lines |
| Refactor background() into 3 tasks | ~200 lines moved, ~50 new |
| RAM for coroutine stacks | 12 KB (3 × 4KB) out of 520 KB |
| Risk to foreground | Zero (no foreground changes) |

---

## Open Questions

1. **Should `putchar_raw` be replaced with a yielding variant?**
   Currently it can block on USB TX. A `putchar_yield(Coro& self, byte c)`
   that checks `tud_cdc_write_available()` and yields if full would
   eliminate the last blocking point in the drain task. Worth doing?

2. **Stack size**: 4KB per coroutine is generous. Could use 2KB if
   SpoonFeeder's Tcl evaluation doesn't recurse deeply. Worth profiling?

3. **Priority**: Should the drain task run more often than others?
   E.g., `drain.resume()` twice per round, or run it between every
   other task switch (as shown above with `PumpUsbCobs()` interspersed)?

4. **setjmp/longjmp vs. naked assembly**: setjmp saves all registers
   (~15 words). A minimal ARM context switch only needs r4-r11 + sp + lr
   (10 words). Worth the ~20 lines of assembly for the speed gain?
