#ifndef CENTIPEDE_FIRMWARE_CORO_H_
#define CENTIPEDE_FIRMWARE_CORO_H_

// Minimal cooperative coroutines for RP2350 (ARM Cortex-M33).
//
// Usage:
//   uint8_t my_stack[4096] __attribute__((aligned(8)));
//   Coro my_coro;
//   coro_create(&my_coro, my_function, my_stack, sizeof(my_stack));
//   coro_resume(&my_coro);  // Runs until my_function yields or returns
//
// Inside the coroutine function:
//   void my_function(Coro& self) {
//       while (true) {
//           do_work();
//           coro_yield(&self);  // Return to scheduler
//       }
//   }

#include <csetjmp>
#include <cstdint>

struct Coro {
  jmp_buf caller_ctx;  // Saved context of whoever called resume()
  jmp_buf coro_ctx;    // Saved context of the coroutine
  bool ready;          // Has been initialized via coro_create
  bool finished;       // The coroutine function has returned
  void (*func)(Coro&); // The coroutine's entry function
};

// Internal: the Coro* being set up during coro_create.
// Safe because only one background core runs coro_create.
static Coro* _coro_creating = nullptr;

// Internal: switch SP and branch to the entry point.
// Naked function: no prologue/epilogue, r0=new_sp, r1=entry.
__attribute__((naked))
static void _coro_switch_sp_and_branch(uint32_t /*new_sp*/, void (* /*entry*/ )()) {
  asm volatile(
      "mov sp, r0\n"  // Set stack pointer to the new stack
      "bx r1\n"       // Branch to entry function
  );
}

// Internal: entry point that runs on the coroutine's stack.
// Called once during coro_create to set up coro_ctx, then
// called again each time the coroutine is resumed.
__attribute__((noinline))
static void _coro_entry_point() {
  Coro* c = _coro_creating;

  // Save this context (on the new stack) into coro_ctx.
  // Then longjmp back to coro_create's setjmp.
  if (setjmp(c->coro_ctx) == 0) {
    longjmp(c->caller_ctx, 1);
    // Does not return here during setup.
  }

  // When coro_resume longjmps to coro_ctx, we land here.
  c->func(*c);

  // The coroutine function returned.
  c->finished = true;
  longjmp(c->caller_ctx, 1);  // Return to the last coro_resume
}

// Initialize a coroutine with the given function and stack.
// Does NOT run the function — call coro_resume to start it.
static void coro_create(Coro* c, void (*func)(Coro&),
                        uint8_t* stack_base, uint32_t stack_size) {
  c->func = func;
  c->finished = false;
  c->ready = false;
  _coro_creating = c;

  if (setjmp(c->caller_ctx) == 0) {
    // 8-byte aligned stack top (ARM AAPCS requirement).
    uint32_t stack_top = ((uint32_t)(stack_base + stack_size)) & ~7u;
    _coro_switch_sp_and_branch(stack_top, _coro_entry_point);
    // Does not return — _coro_entry_point longjmps back.
  }
  // longjmp from _coro_entry_point returns here.
  // coro_ctx is now set up pointing to the new stack.
  c->ready = true;
}

// Resume a coroutine. Returns when the coroutine yields or finishes.
static inline void coro_resume(Coro* c) {
  if (c->finished || !c->ready) return;
  if (setjmp(c->caller_ctx) == 0) {
    longjmp(c->coro_ctx, 1);  // Jump into the coroutine
  }
  // Coroutine yielded or finished — we're back.
}

// Yield from inside a coroutine. Returns when the coroutine is resumed.
static inline void coro_yield(Coro* c) {
  if (setjmp(c->coro_ctx) == 0) {
    longjmp(c->caller_ctx, 1);  // Return to coro_resume
  }
  // coro_resume called us again — continue.
}

#endif  // CENTIPEDE_FIRMWARE_CORO_H_
