#define MHz 250  // 250

#define ON_RESET_DO_SPOONFEED_CONSOLE 1
#define GSPOON_POC_DEMO 0
#define ECHO_PUTCHAR_ON_CONSOLE 1
#define USE_ORCHESTRA90 1

enum TracingSpeed { NO_SPEED, SLOW_SPEED, MEDIUM_SPEED, FAST_SPEED };
TracingSpeed Speed = SLOW_SPEED;
// TracingSpeed Speed = MEDIUM_SPEED;
// TracingSpeed Speed = FAST_SPEED;

// #define TRIGGER_ON_WRITE 0xFE7F

// #define CENTIPEDE_REV 3204 // 32d
// #define CENTIPEDE_REV 3205 // 32e
#define CENTIPEDE_REV 3226  // 32z

#define DBUS_HOLD_CYCLES 0

#define CENTIPEDE_INVERT_EQ 1

#define IN_FLASH __in_flash("FLASH")
#define IN_RAM __not_in_flash("centipede")

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define FORCE_INLINE inline __attribute__((always_inline))

#include <hardware/clocks.h>
#include <hardware/pio.h>
#include <hardware/regs/pads_qspi.h>
#include <hardware/structs/qmi.h>
#include <hardware/sync.h>
#include <pico/multicore.h>
#include <pico/platform.h>
#include <pico/stdlib.h>
#include <pico/time.h>

#include "pico/rand.h"

extern "C" {
#include <arm_acle.h>
#include <cmsis_gcc.h>
#include <setjmp.h>
#include <stdio.h>

#include "../littlefs/lfs-centipede.h"
#include "../littlefs/lfs.h"
#include "../littlefs/lfs_util.h"

int _getentropy(void* buffer, size_t length) {
  char* ptr = (char*)buffer;
  while (length >= 4) {
    uint32_t r = get_rand_32();
    memcpy(ptr, &r, 4);
    ptr += 4;
    length -= 4;
  }
  if (length > 0) {
    uint32_t r = get_rand_32();
    memcpy(ptr, &r, length);
  }
  return 0;
}
int getentropy(void* buffer, size_t length) {
  return _getentropy(buffer, length);
}
}

#include <cstring>
#include <functional>

#include "script.h"

std::vector<script::Command> script::global_script_commands;

Tcl_Interp* global_tcl_interp = nullptr;

extern "C" int TclCommandWrapper(ClientData clientData, Tcl_Interp* interp,
                                 int argc, char* argv[]) {
  script::CommandFunction func =
      reinterpret_cast<script::CommandFunction>(clientData);
  std::vector<std::string> args;
  args.reserve(argc);
  for (int i = 0; i < argc; ++i) {
    args.push_back(argv[i]);
  }
  script::errstring err = func(args);
  if (!err.empty()) {
    Tcl_SetResult(interp, const_cast<char*>(err.c_str()), TCL_VOLATILE);
    return TCL_ERROR;
  }
  return TCL_OK;
}

#define G_RW 20
#define G_E 21
#define G_Q 22

#if CENTIPEDE_REV == 3205  // 32e

#define G_LED 25
#define G_SCS 26
#define G_CART 27
#define G_SLENB 28
#define G_HALT 29
#define G_NMI 30
#define G_CTS 31

#elif CENTIPEDE_REV == 3204  // 32d

#define G_CTS 18  // bodged
#define G_SCS 19  // bodged

#define G_LED 25
#define G_SND 26
#define G_CART 27
#define G_SLENB 28
#define G_HALT 29
#define G_NMI 30
#define G_RESET 31

#elif CENTIPEDE_REV == 3226  // 32z

#define G_CTS 8
#define G_SCS 9

#define G_LED 25
#define G_SND 26
#define G_CART 27
#define G_SLENB 28
#define G_HALT 29
#define G_NMI 30
#define G_RESET 31

#else  // Original Centipede experiment (all wire-wrapped)

#define G_LED 25
#define G_NMI 26
#define G_RESET 27
#define G_HALT 28
#define G_SLENB 29

#endif

#define G_D0 0
#define G_A0 32

#include <array>
#include <atomic>
#include <cstdint>

using byte = unsigned char;
using addr16 = uint16_t;

#define SET_LED(X) gpio_put(G_LED, (X))
#define volatile_sio_hw ((volatile sio_hw_t*)SIO_BASE)

void INPUT(int i) {
  gpio_init(i);
  gpio_set_dir(i, GPIO_IN);
  gpio_set_pulls(i, false, false);
}
void OUTPUT(int i, int x) {
  gpio_init(i);
  gpio_set_dir(i, GPIO_OUT);
  gpio_put(i, x);
}

#if 0
void ResetOn() {
  gpio_set_dir(G_RESET, GPIO_OUT);
}
void ResetOff() {
  gpio_set_dir(G_RESET, GPIO_IN);
}
#endif

void HaltOn() {
  gpio_set_dir(G_HALT, GPIO_OUT);
  // SET_LED(1);
}
void HaltOff() {
  // SET_LED(0);
  gpio_set_dir(G_HALT, GPIO_IN);
}

#include "cross-core.h"
#include "usb_pipeline.h"

CircBuf<unsigned char, 1024> usb_raw_buf;
CircBuf<std::string*, 64> usb_packet_buf;

UsbReceiver usb_receiver(usb_raw_buf);
CobsDecoder<1024, 64> cobs_decoder(usb_raw_buf, usb_packet_buf);

CrossCoreFIFO<uint, 8192> fg2bg;
CrossCoreFIFO<uint, 8192> bg2fg;

inline void PumpUsbCobsWithHalts() {
  if (PumpUsbCobsHasWork()) {
    HaltOn();
    PumpUsbCobs();
    HaltOff();
  }
}

FORCE_INLINE uint ccfifo_pop_blocking() {
  uint z = 0;
  while (1) {
    PumpUsbCobsWithHalts();
    bool ok = fg2bg.pop(z);
    if (ok) return z;
  }
}

//--too-small-- #define PUSH_TO_BG force_inline_multicore_fifo_push_blocking
//--too-small-- #define BLOCKING_PULL_FROM_FG  multicore_fifo_pop_blocking

#define SAY(C) PUSH_TO_BG(FG2BG_PUTCHAR, 0, (C) & 255)
#define PUSH_TO_BG(T, A, D) fg2bg.push(((T) << 24) | ((A) << 8) | (D))
#define BLOCKING_PULL_FROM_FG ccfifo_pop_blocking

#define INCLUDING
#include "disk11_rom.h"  // byte disk11_rom[8192]...

using IOReader = std::function<byte(uint addr)>;
using IOWriter = std::function<void(uint addr, byte data)>;

IOReader Readers[256];
IOWriter Writers[256];

byte ram[64 * 1024];

// Code to tethered PC.
//
// Length is explicit:
#define C_LOGGING 130
#define C_DISK_READ 173
#define C_DISK_WRITE 174
#define C_COMPRESSED_CYCLES 175
//
// Length is implicit:
#define C_PUTCHAR 193     // 0xC1
#define C_RAM2_WRITE 195  // 0xC3
#define C_RAM2_READ 211   // 0xD3
#define C_CYCLE_RD3 211   // 0xD3

// Commands into the FIFO to the slow core

enum FifoNumbers {
  FG2BG_PUTCHAR,         // 0
  FG2BG_READ,            // 1
  FG2BG_SPOON_ON_RESET,  // 2
  FG2BG_WRITE,           // 3
  FG2BG_SYNC_NEEDED,     // a boundary, not an event.
  FG2BG_NMI,
  FG2BG_FLOPPY_COMMAND,
  FG2BG_FLOPPY_LATCH,
  FG2BG_W_256,
  FG2BG_PEEK_REPLY,
};

enum Bg2FgNumbers {
  BG2FG_PEEK = 1,
  BG2FG_POKE = 2,
};

// {
#define COMPRESS_CYCLES 1  // Seems safe by now.
#if COMPRESS_CYCLES

// ResetCompressCycles();          // call once at session start
// uint n = CompressCycles(buf, words, len, pred);  // call per block, state
// persists
#include "compress.h"

#define COMPRESSION_MAX 20
byte compression_buffer[5 * COMPRESSION_MAX];
uint32_t cycle_buffer[COMPRESSION_MAX];
uint cycle_i;

bool IsRomPredicateForCompression(addr16 addr) {
  return 0x8000 <= addr && addr < 0xFF00;
}

FORCE_INLINE void SendSizePrefix(uint sz) {
  if (sz > 63) {
    putchar_raw(0xC0 + (sz >> 6));
    putchar_raw(0x80 + (sz && 63));
  } else {
    putchar_raw(0x80 + sz);
  }
}

#define ASSERT_HALT() gpio_set_dir(G_HALT, GPIO_OUT)
#define RELEASE_HALT() gpio_set_dir(G_HALT, GPIO_IN)

#define ASSERT_NMI() gpio_set_dir(G_NMI, GPIO_OUT)
#define RELEASE_NMI() gpio_set_dir(G_NMI, GPIO_IN)

#define GERBIL_GET() gerbil_program_get_word(pio, sm)
#define GERBIL_DRIVE(X) gerbil_program_put_word(pio, sm, 0x100 | (X))
#define GERBIL_PASS() gerbil_program_put_word(pio, sm, 0)

#include "console.h"
#include "flash-label.h"
#include "floppy.h"
#include "gerbil.pio.h"
#include "gspoon.h"
#include "script.h"

void IN_RAM InsertCycleWithCompression(uint32_t chore) {
  cycle_buffer[cycle_i] = chore;
  cycle_i++;
  if (cycle_i == COMPRESSION_MAX) {
    uint n = CompressCycles(compression_buffer, cycle_buffer, cycle_i,
                            IsRomPredicateForCompression);
    putchar_raw(C_COMPRESSED_CYCLES);
    SendSizePrefix(n);
    for (uint i = 0; i < n; i++) {
      putchar_raw(compression_buffer[i]);
    }
    cycle_i = 0;
  }
}

#endif  // COMPRESS_CYCLES
// }

uint trigger;
volatile uint idling;

FORCE_INLINE bool inline_volatile_gpio_get(uint pin) {
#if NUM_BANK0_GPIOS <= 32
  return volatile_sio_hw->gpio_in & (1u << pin);
#else
  if (pin < 32) {
    return volatile_sio_hw->gpio_in & (1u << pin);
  } else {
    return volatile_sio_hw->gpio_hi_in & (1u << (pin - 32));
  }
#endif
}

FORCE_INLINE void force_inline_multicore_fifo_push_blocking(uint32_t data) {
  // We wait for the fifo to have some space
  while (!multicore_fifo_wready()) tight_loop_contents();

  sio_hw->fifo_wr = data;

  // Fire off an event to the other core
  __sev();
}

bool MmuEnabled;
byte MmuTask;
bool StickyRamFFEx;
byte MmuMap[2][8];

bool SamP1Bit;
bool SamTyBit;

#include "coco64k.h"
#include "littlefs.h"
#include "orchestra90.h"

/////////////////////////////////////////////////////////////

#ifdef AUTO_TYPE
const char auto_type[] = AUTO_TYPE;
uint auto_i;
uint auto_skip = 100;
uint auto_hold;
byte auto_value;

const char normal_keyboard[] =
    "@ABCDEFGHIJKLMNOPQRSTUVWXYZ~~~~ 0123456789:;,-./\n";
const char shift_keyboard[] =
    "@abcdefghijklmnopqrstuvwxyz~~~~ ~!\"#$%'()*+<=>?\n";

constexpr int SHIFTED = 0x100;

int find_keycode(char c) {
  for (uint i = 0; normal_keyboard[i]; i++) {
    if (normal_keyboard[i] == c) {
      return i;
    }
  }
  for (uint i = 0; shift_keyboard[i]; i++) {
    if (shift_keyboard[i] == c) {
      return i + SHIFTED;
    }
  }
  return -1;
}

byte keyboard_response(char c) {
  int code = find_keycode(c);
  if (code < 0) return 0xFF;

  byte col = code & 7;
  byte row = (code >> 3) & 7;

  byte probe = ram[0xFF02];
  byte z = 0xff;
  if ((probe & (1u << col)) == 0) {
    z &= 0xFF ^ (1u << row);
  }
  if (code & SHIFTED) {
    if ((probe & 0x80) == 0) {
      z &= 0xFF ^ (1u << 6);
    }
  }
  PUSH_TO_BG(FG2BG_PUTCHAR, 0, '0' + (15 & (z >> 4)));
  PUSH_TO_BG(FG2BG_PUTCHAR, 0, '0' + (15 & (z >> 0)));
  return z;
}

#endif

////////////////////////////////////////////////////////
#if 0
void ResetCocoOnStartup() {
    OUTPUT(G_HALT, 0);
    sleep_ms(100);
    OUTPUT(G_RESET, 0);
    sleep_ms(500);
    OUTPUT(G_RESET, 1);
    sleep_ms(100);
    OUTPUT(G_HALT, 1);
}
#endif
////////////////////////////////////////////////////////

template <class T>
class CoreEngine {
 public:
  static void IN_RAM Fatal(const char* s, int x) {
    for (const char* p = "FATAL: "; *p; p++) {
      putchar(C_PUTCHAR);
      putchar(*p);
    }
    for (const char* p = s; *p; p++) {
      putchar(C_PUTCHAR);
      putchar(*p);
    }
    printf("\nFATAL(%d.): %s\n", x, s);
    while (1) continue;
  }

  static void IN_FLASH InitializePins() {
    for (uint i = 0; i <= 22; i++) {
      gpio_init(i);
      gpio_set_dir(i, GPIO_IN);
      gpio_set_pulls(i, false, false);
    }
    OUTPUT(G_LED, 1);
    INPUT(G_SND);
    INPUT(G_CTS);
    INPUT(G_SCS);
    INPUT(G_RESET);
    INPUT(G_SLENB);  // gerbil.pio will overtake

#define OPEN_DRAIN(PIN)        \
  gpio_init(PIN);              \
  gpio_set_dir(PIN, GPIO_OUT); \
  gpio_put(PIN, 0);            \
  gpio_set_dir(PIN, GPIO_IN);  \
  gpio_set_pulls(PIN, true, false);

    // OPEN_DRAIN(G_RESET);
    OPEN_DRAIN(G_HALT);
    OPEN_DRAIN(G_NMI);
    OPEN_DRAIN(G_CART);

    for (uint i = 32; i <= 47; i++) {
      gpio_init(i);
      gpio_set_dir(i, GPIO_IN);
      gpio_set_pulls(i, false, false);
    }
    // LED off.
    gpio_init(G_LED);
    gpio_set_dir(G_LED, GPIO_OUT);
    SET_LED(0);
  }  // end InitializePins

  FORCE_INLINE static void background() {
    while (1) {
      const uint sz = fg2bg.size();

      // failed to DIR: if (sz < 1) HaltOff(); // allow CPU to run
      HaltOff();  // allow CPU to run

      const uint chore = BLOCKING_PULL_FROM_FG();
      const uint chore_num = chore >> 24;
      const uint chore_addr = 0xFFFF & (chore >> 8);
      const byte chore_byte = 0xFF & chore;

      // failed to DIR: if (sz > 0) HaltOn(); // stop CPU while we work
      HaltOn();  // stop CPU while we work

      switch (chore_num) {
        case FG2BG_PUTCHAR:
          putchar_raw(chore_byte);
          break;

        case FG2BG_READ:  // read cycle
          if (Speed <= SLOW_SPEED) {
#if COMPRESS_CYCLES
            InsertCycleWithCompression(chore);
#else
            putchar_raw(C_RAM2_READ);
            putchar_raw(chore >> 16);
            putchar_raw(chore >> 8);
            putchar_raw(chore);
#endif
          }
          break;

        case FG2BG_WRITE:  // write cycle
          if (Speed <= MEDIUM_SPEED) {
#if COMPRESS_CYCLES
            InsertCycleWithCompression(chore);
#else
            putchar_raw(C_RAM2_WRITE);
            putchar_raw(chore >> 16);
            putchar_raw(chore >> 8);
            putchar_raw(chore);
#endif

#if TRIGGER_ON_WRITE
            if (((chore >> 8) & 0xFFFF) == TRIGGER_ON_WRITE) {
              Speed = SLOW_SPEED;
            }
#endif
          }
          break;

        case FG2BG_NMI:
          gpio_set_dir(G_NMI, GPIO_OUT);
          sleep_us(2);  // for more than a cycle
          gpio_set_dir(G_NMI, GPIO_IN);

          putchar_raw(C_LOGGING);
          putchar_raw(4 + 128);
          putchar_raw('N');
          putchar_raw('M');
          putchar_raw('I');
          putchar_raw('\n');
          break;

        case FG2BG_FLOPPY_LATCH: {
          T::BackgroundFifoFloppyLatch(chore_byte);
        } break;

        case FG2BG_FLOPPY_COMMAND:
          T::BackgroundFifoFloppyCommand(chore, chore_byte);
          break;

        case FG2BG_W_256:
          T::BackgroundFifoFloppyW256();
          break;

        case FG2BG_SPOON_ON_RESET:
          HaltOff();  // Release CPU — foreground needs it running for
                      // Poke1/Peek1
          gspoon::SpoonFeeder();
          break;

        default:
          printf("\nWUT? CHORE=%x\n", chore);
      }  // end switch (chore>>24)
    }  // end while 1
  }  // end background

  FORCE_INLINE static void foreground() {
    // Disable interrupts in this "fast" core.
    save_and_disable_interrupts();

    const PIO pio = pio0;
    constexpr uint sm = 0;

    while (true) {
      uint cycle = 0;
      while (true) {
        const uint signals = GERBIL_GET();
        const bool reading = ((signals & (1u << G_RW)) != 0);
        const uint abus = volatile_sio_hw->gpio_hi_in & 0xFFFF;
        byte dbus = 0x00;

        constexpr uint NEG_CTS = (1 << G_CTS);
        constexpr uint NEG_SCS = (1 << G_SCS);
        constexpr uint NEG_SELECTS = NEG_CTS | NEG_SCS;

        if (LIKELY((signals & NEG_SELECTS) == NEG_SELECTS)) {
          // CASE normal

          if (LIKELY(reading)) {
            // CASE normal read
            if (0xFF00 <= abus) {
              auto r = Readers[abus & 0xFF];
              if (r) {
                dbus = r(abus);
                GERBIL_DRIVE(dbus);
              } else {
                GERBIL_PASS();
                dbus = (byte)(GERBIL_GET());  // log & debug
              }
            } else if (not T::UseCoco64kRam(abus) && 0xC000 <= abus &&
                       abus < 0xE000) {
              // I DONT KNOW WHY, but we're not seeing CTS drop for Disk Basic
              // ROM.
              //--SAY('c');
              dbus = disk11_rom[abus & 0x1FFF];
              GERBIL_DRIVE(dbus);
            } else if (T::UseCoco64kRam(abus)) {
              uint atrans = T::TranslateCoco64kRamAddress(abus);
              dbus = ram[atrans];
              GERBIL_DRIVE(dbus);
            } else {
              GERBIL_PASS();
              dbus = (byte)(GERBIL_GET());  // log & debug
            }
            T::PushFifoRead(abus, dbus);
          } else {
            // CASE normal write
            dbus = (byte)(GERBIL_GET());

            if (0xFF00 <= abus) {
              auto w = Writers[abus & 0xFF];
              if (w) {
                w(abus, dbus);
              }
              ram[abus] = dbus;
              T::PushFifoWrite(abus, dbus);

            } else {
              uint atrans = T::UseCoco64kRam(abus)
                                ? T::TranslateCoco64kRamAddress(abus)
                                : abus;
              ram[atrans] = dbus;
              T::PushFifoWrite(atrans, dbus);
            }
          }
        } else {  // Is Special Select
          // CASE special
          if (LIKELY(reading)) {  // Special CPU READING -- we TX
            // CASE special read
            if ((signals & NEG_CTS) == 0) {  // READ CTS
              // CASE special read CTS

              // I DONT KNOW WHY,
              // but we're not seeing CTS drop for Disk Basic ROM,
              // or we are not seeing it soon enough.  If it starts
              // happening again, print R so we notice.
              SAY('R');
              dbus = disk11_rom[abus & 0x1FFF];
            } else {  // READ SCS
              T::ReadScsFloppy(abus, dbus);
            }
            // JOIN special read
            GERBIL_DRIVE(dbus);
            T::PushFifoRead(abus, dbus);
          } else {  // Special CPU WRITING -- we RX
            // SAY('W');
            // CASE special write
            dbus = (byte)(GERBIL_GET());
            ram[abus] = dbus;

            if (LIKELY((signals & NEG_SCS) == 0)) {
              T::WriteScsFloppy(abus, dbus);
            }  // end special write SCS
            T::PushFifoWrite(abus, dbus);
          }  // end special read or write
        }  // end if special

        ++cycle;
#if GSPOON_POC_DEMO
        if (cycle == 2000000) {
          gspoon::SpoonNMI();  // Hijack for PoC demo
        }
#endif

#if ON_RESET_DO_SPOONFEED_CONSOLE
        if ((signals & (1 << G_RESET)) == 0) {
          break;
        }
#endif
      }  // end while true (until RESET)

      // ON RESET:
      gspoon::SpoonfeedConsoleOnReset();

    }  // end while true
  }  // end foreground

  FORCE_INLINE static void PushFifoRead(uint abus, byte dbus) {
    if (Speed <= SLOW_SPEED) {
      if (abus != 0xFFFF) {
        PUSH_TO_BG(FG2BG_READ, abus, dbus);
      }
    }
  }
  FORCE_INLINE static void PushFifoWrite(uint abus, byte dbus) {
    if (Speed <= MEDIUM_SPEED) {
      PUSH_TO_BG(FG2BG_WRITE, abus, dbus);
    }
  }

  FORCE_INLINE static void RunCores(void (*core1_func)(void),
                                    void (*core0_func)(void)) {
    const PIO pio = pio0;
    constexpr uint sm = 0;

    pio_clear_instruction_memory(pio);
    pio_add_program_at_offset(pio, &gerbil_program, 0);
    gerbil_program_init(pio, sm, 0);
    printf("#gerbil_program.length=%d\n", gerbil_program.length);

    // foreground must be fast.
    multicore_launch_core1(core1_func);

    // background on core 0 handles interrupts.
    core0_func();
  }
};  // end CoreEngine

#if 0
template <typename T>
struct NoSpyEngine :
    public CoreEngine<T>
{
    FORCE_INLINE static
        void PushFifoRead(uint abus, byte dbus) {}
    FORCE_INLINE static
        void PushFifoWrite(uint abus, byte dbus) {}
};
template <typename T>
struct WriteSpyEngine :
    public CoreEngine<T>
{
    FORCE_INLINE static
        void PushFifoRead(uint abus, byte dbus) {}
    FORCE_INLINE static
    void PushFifoWrite(uint abus, byte dbus) {
            PUSH_TO_BG(FG2BG_WRITE , abus , dbus);
    }
};
template <typename T>
struct ReadWriteSpyEngine :
    public CoreEngine<T>
{
    FORCE_INLINE static
    void PushFifoRead(uint abus, byte dbus) {
            if (abus != 0xFFFF) {
                PUSH_TO_BG(FG2BG_READ , abus , dbus);
            }
    }
    FORCE_INLINE static
    void PushFifoWrite(uint abus, byte dbus) {
            PUSH_TO_BG(FG2BG_WRITE , abus , dbus);
    }
};
#endif

void IN_RAM core1_trampoline();
void IN_RAM core0_trampoline();

class Engine0 : public DoFloppy<Engine0>,
                // public DoCoco3Mmu<Engine0>,
                // public SmallRam<Engine0>,
                // public ReadWriteSpyEngine<Engine0>
                // public WriteSpyEngine<Engine0>
                public DoCoco64k<Engine0>,
                public CoreEngine<Engine0> {
 public:
  static void RunEngine() {
    // T::InitCoco3Mmu();
    InitCoco64k();
#if USE_ORCHESTRA90
    orchestra90::Init();
#endif
    ResetCompressCycles();  // call once at session start
    RunCores(core1_trampoline, core0_trampoline);
  }
};

void IN_RAM core1_trampoline() { Engine0::foreground(); }

void IN_RAM core0_trampoline() { Engine0::background(); }

void IN_RAM restart_core1(void (*func)(void)) {
  // 1. Force Core 1 into reset
  multicore_reset_core1();

  // 2. Small delay to ensure hardware lines settle (often optional, but safe)
  sleep_us(10);

  // 3. Launch it again with the desired entry point
  multicore_launch_core1(func);
}

void IN_RAM safe_adjust_flash_speed() {
#if MHz > 150
  // 1. Critical: Disable interrupts while making the adjustment
  uint32_t ints = save_and_disable_interrupts();

  const uint32_t HASTY = 2;
  const uint32_t SAFE = 4;  // 250 MHz / 4 = 62.5 MHz (Perfect for safety)
  uint32_t clkdiv = SAFE;
  uint32_t rxdelay =
      4;  // On RP2350, for QSPI frequencies, match RXDELAY to CLKDIV

  // 2. Mask and update the CLKDIV and RXDELAY fields in the QMI timing register
  hw_write_masked(
      &qmi_hw->m[0].timing,
      ((clkdiv << QMI_M0_TIMING_CLKDIV_LSB) & QMI_M0_TIMING_CLKDIV_BITS) |
          ((rxdelay << QMI_M0_TIMING_RXDELAY_LSB) & QMI_M0_TIMING_RXDELAY_BITS),
      QMI_M0_TIMING_CLKDIV_BITS | QMI_M0_TIMING_RXDELAY_BITS);

  // 3. Re-enable interrupts
  restore_interrupts(ints);
#endif
}

int IN_RAM main() {
  // ResetCocoOnStartup();

  Engine0::InitializePins();
  FlashLabel::InitLabel();
#if MHz != 150
  set_sys_clock_khz(MHz * 1000, true);
#endif
  stdio_usb_init();
  safe_adjust_flash_speed();

  for (uint i = 0; i < 2; i++) {
    SET_LED(1);
    sleep_ms(200);

    SET_LED(0);
    sleep_ms(200);
  }
  FlashLabel::PrintLabel();
  init_lfs();

  Engine0::RunEngine();
}
