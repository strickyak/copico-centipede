#define MHz 250

#define CENTIPEDE_REV 3226 // 32z
// #define CENTIPEDE_REV 3204 // 32d
//#define CENTIPEDE_REV 3205 // 32e

#define DBUS_HOLD_CYCLES 0

#define CENTIPEDE_INVERT_EQ 1

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define FORCE_INLINE inline __attribute__((always_inline))

#include <hardware/clocks.h>

#include <functional>
// #include <hardware/dma.h>
#include <hardware/pio.h>

#include "hardware/sync.h"
// #include <hardware/structs/systick.h>
// #include <hardware/timer.h>
#include <pico/multicore.h>
#include <pico/stdlib.h>
#include <pico/time.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <arm_acle.h>
#include <cmsis_gcc.h>
#include <cstring>
#include <setjmp.h>
#include <stdio.h>

extern int stdio_usb_in_chars(char* buf, int length);

#ifdef __cplusplus
}
#endif

#define G_RW 20
#define G_E 21
#define G_Q 22

#if CENTIPEDE_REV == 3205 // 32e

#define G_LED 25
#define G_SCS 26
#define G_CART 27
#define G_SLENB 28
#define G_HALT 29
#define G_NMI 30
#define G_CTS 31

#elif CENTIPEDE_REV == 3204 // 32d

#define G_CTS 18   // bodged
#define G_SCS 19   // bodged

#define G_LED 25
#define G_SND 26
#define G_CART 27
#define G_SLENB 28
#define G_HALT 29
#define G_NMI 30
#define G_RESET 31

#elif CENTIPEDE_REV == 3226 // 32z

#define G_CTS 8   // bodged
#define G_SCS 9   // bodged

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

#define SET_LED(X) gpio_put(G_LED, (X))

#include <array>
#include <atomic>
#include <cstdint>

using byte = unsigned char;

#include "cross-core.h"

CrossCoreFIFO<uint, 1024> ccfifo;

FORCE_INLINE uint ccfifo_pop_blocking() {
    uint z = 0;
    while (1) {
        bool ok = ccfifo.pop(z);
        if (ok) return z;
    }
}

// #define PUSH force_inline_multicore_fifo_push_blocking
// #define POP  multicore_fifo_pop_blocking

#define PUSH ccfifo.push
#define POP  ccfifo_pop_blocking

#define INCLUDING
#include "disk11_rom.c"  // byte disk11_rom[8192]...
// #include "hdbdw3cc3_rom.c"
// #include "hdbchs_rom.c"
// #include "hdbsdc_rom.c"


using IOReader = std::function<byte(uint addr)>;
using IOWriter = std::function<void(uint addr, byte data)>;

IOReader Readers[256];
IOWriter Writers[256];

byte DefaultReader(uint addr) {
  gpio_set_dir_out_masked(0);
  return 43;
}
void DefaultWriter(uint addr, byte d) { gpio_set_dir_out_masked(0); }

////////////////////////////////////////////////////////

template <uint G>
class PositiveControl {
  static void Init(bool value) {
    gpio_init(G);
    gpio_set_dir(G, GPIO_OUT);
    gpio_put(G, value);
  }
  FORCE_INLINE static void Put(bool active) { gpio_put(G, active); }
};

template <uint G>
class NegativeOpenCollectorControl {
  static void Init(void) {
    gpio_init(G);
    gpio_set_dir(G, GPIO_OUT);
    gpio_put(G, 0);
    gpio_set_dir(G, GPIO_IN);
    gpio_set_pulls(G, false, false);
  }
  FORCE_INLINE static void Put(bool active) {
    gpio_set_dir(G, active ? GPIO_OUT : GPIO_IN);
  }
};

PositiveControl<G_LED> Led;

NegativeOpenCollectorControl<G_HALT> Halt;
NegativeOpenCollectorControl<G_NMI> Nmi;
NegativeOpenCollectorControl<G_CART> Cart;
#if G_RESET
NegativeOpenCollectorControl<G_RESET> Reset;
#endif

template <uint GPIN>
class NegativeSignal {
  static void Init(void) {
    gpio_init(GPIN);
    gpio_set_dir(GPIN, GPIO_IN);
    gpio_set_pulls(GPIN, false, false);
  }
  FORCE_INLINE static bool Get(void) { return gpio_get(GPIN); }
};

NegativeSignal<G_RW> Write;
NegativeSignal<G_E> E;
NegativeSignal<G_Q> Q;
#if G_SND
NegativeSignal<G_SND> Snd;
#endif

////////////////////////////////////////////////////////

byte ram[128 * 1024];

template <class T>
class DoCoverRam32K {
  FORCE_INLINE static bool Predicate(uint a) { return a < 0x8000; }
  FORCE_INLINE static void Write(uint a, byte d) {
    // ram[a] = d // should already be done.
  }
  FORCE_INLINE static byte Read(uint a) { return ram[a]; }
};

////////////////////////////////////////////////////////

#define MARK_FLOPPY_LATCH 0x100
#define MARK_FLOPPY_COMMAND 0x200
#define MARK_FLOPPY_TRACK 0x300
#define MARK_FLOPPY_SECTOR 0x400
#define MARK_FLOPPY_MASK 0xFFFFFF00

// Code from fast to slow main.
#define SLOW_SEND_NMI 150

// Code to tethered PC.
//
// Length is explicit:
#define C_LOGGING 130
#define C_DISK_READ 173
#define C_DISK_WRITE 174
//
// Length is implicit:
#define C_PUTCHAR 193
#define C_RAM2_WRITE 195
#define C_CYCLE_RD3 211

// Commands into the FIFO to the slow core
#if 0
#define FIFO_READ (0x01u << 24)
#define FIFO_ROM (0x02u << 24)
#define FIFO_WATCH_R (0x08u << 24)
#define FIFO_TRIGGER_R (0x09u << 24)
#define FIFO_IDLING (0x0Au << 24)
#define FIFO_GRABBED (0x0Bu << 24)
#endif
#define FIFO_WRITE (0x03u << 24)

#define FIFO_NMI (0x04u << 24)
#define FIFO_FLOPPY_COMMAND (0x05u << 24)
#define FIFO_W_256 (0x06u << 24)  // finished 256 bytes of written data
#define FIFO_FLOPPY_LATCH (0x07u << 24)

uint trigger;
volatile uint idling;
volatile uint bg_busy;

byte floppy_latch;
byte floppy_command;
byte floppy_status;
byte floppy_track;
byte floppy_sector;
byte* floppy_ptr;

byte floppy_buf[256];
#define floppy_limit (256 + floppy_buf)

#define volatile_sio_hw ((volatile sio_hw_t*)SIO_BASE)

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

void HaltOn() {
  gpio_set_dir(G_HALT, GPIO_OUT);
  // gpio_put(G_HALT, false);
  SET_LED(1);
}
void HaltOff() {
  SET_LED(0);
  // sleep_us(100);
  // gpio_put(G_HALT, true);
  gpio_set_dir(G_HALT, GPIO_IN);
}

void Fatal(const char* s, int x) {
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

void SendSectorData() {
  for (uint i = 0; i < 256; i++) {
    putchar_raw(floppy_buf[i]);
  }
}

void ReceiveSectorData() {
  char c = 0;
  int rc;
  do {
    rc = stdio_usb_in_chars(&c, 1);
  } while (rc == PICO_ERROR_NO_DATA);

  if (byte(c) != 0xAD) {
    printf(" ReceiveSectorData: rc=%d. c=%d. \n", rc, c);
    Fatal("bad c", (byte)c);
  }

  int needed = 7;
  char* p = (char*)floppy_buf;  // first write with unneeded header
  while (needed > 0) {
    rc = stdio_usb_in_chars(p, needed);
    if (rc == PICO_ERROR_NO_DATA) continue;

    p += rc;
    needed -= rc;
  }

  needed = 256;
  p = (char*)floppy_buf;  // overwrite with good data
  while (needed > 0) {
    rc = stdio_usb_in_chars(p, needed);
    if (rc == PICO_ERROR_NO_DATA) continue;

    p += rc;
    needed -= rc;
  }
}

bool MmuEnabled;
byte MmuTask;
bool StickyRamFFEx;
byte MmuMap[2][8];

bool SamP1Bit;
bool SamTyBit;

template <class T>
class DontCoco64k {
 public:
  static constexpr bool HasCoco64k() { return false; }
  static void InitCoco64k() {}
  static constexpr bool UseCoco64kRam(uint a) { return false; }
  FORCE_INLINE static uint TranslateCoco64kRamAddress(uint a) { return a; }
};

template <class T>
class DoCoco64k {
 public:
  static constexpr bool HasCoco64k() { return true; }
  FORCE_INLINE static bool UseCoco64kRam(uint a) {
    return (a < (SamTyBit ? 0xFF00 : 0x8000));
  }
  FORCE_INLINE static uint TranslateCoco64kRamAddress(uint a) {
    return SamP1Bit ? (0x8000 ^ a) : a;
  }

  static void InitCoco64k() {
#if 1
    for (uint a = 0xFFD4; a < 0xFFE0; a++) {
      Writers[255 & a] = WriteOtherSamBit;
    }
#endif

    SamP1Bit = false;
    SamTyBit = false;
    Writers[0xD4] = WriteFFD4_P1Clear;
    Writers[0xD5] = WriteFFD5_P1Set;
    Writers[0xDE] = WriteFFDE_TyClear;
    Writers[0xDF] = WriteFFDF_TySet;
  }

  static void WriteOtherSamBit(uint a, byte d) {
    bool odd = a & 1;
    uint bitnum = (a - 0xFFC0) >> 1;
    PUSH((odd ? 'A' : 'a') + bitnum);
  }

  static void WriteFFD4_P1Clear(uint a, byte d) {
    SamP1Bit = false;
    //PUSH('w');
  }
  static void WriteFFD5_P1Set(uint a, byte d) {
    SamP1Bit = true;
    //PUSH('x');
  }
  static void WriteFFDE_TyClear(uint a, byte d) {
    SamTyBit = false;
    //PUSH('y');
  }
  static void WriteFFDF_TySet(uint a, byte d) {
    SamTyBit = true;
    //PUSH('z');
  }
};

template <class T>
class DoCoco3Mmu {
 public:
  static void InitCoco3Mmu() {
    for (uint t = 0; t < 2; t++) {
      for (uint i = 0; i < 8; i++) {
        MmuMap[t][i] = 0x38 + i;
      }
    }

    Writers[0x90] = T::WriteFF90;
    Writers[0x91] = T::WriteFF91;

    for (uint t = 0; t < 2; t++) {
      for (uint i = 0; i < 8; i++) {
        Writers[8 * t + i + 0xA0] = [=](uint a, byte d) { MmuMap[t][i] = d; };
        Readers[8 * t + i + 0xA0] = [=](uint a) { return MmuMap[t][i]; };
      }
    }
  }

 private:
  static void WriteFF90(uint a, byte d) {
    MmuEnabled = (1u << 6) & d;
    StickyRamFFEx = (1u << 3) & d;
  }
  static void WriteFF91(uint a, byte d) { MmuTask = 1u & d; }
};

template <class T>
class SmallRam {
  FORCE_INLINE static uint Phys(uint a) {
    a &= 0xFFFF;
    if (T::UseCoco64kRam(a)) {
      return T::TranslateCoco64kRamAddress(a);
    } else {
      return a;
    }
  }

 public:
  static constexpr bool HasBigRam() { return false; }

  FORCE_INLINE static byte Peek(uint a) {
    uint p = Phys(a);
    return ram[p];
  }
  FORCE_INLINE static void Poke(uint a, byte d) {
    uint p = Phys(a);
    ram[p] = d;
  }
};

template <class T>
class BigRam {
  FORCE_INLINE static uint Phys(uint a) {
    if (!MmuEnabled) return a;
    if (a >= 0xFE00) return a;

    uint slot = 7 & (a >> 13);
    uint offset = (a & 0x1FFF);
    uint block = 15 & MmuMap[MmuTask][slot];
    return (block << 13) + offset;
  }

 public:
  static constexpr bool HasBigRam() { return true; }

  FORCE_INLINE static byte Peek(uint a) {
    uint p = Phys(a);
    return ram[p];
  }
  FORCE_INLINE static void Poke(uint a, byte d) {
    uint p = Phys(a);
    ram[p] = d;
  }
};

////////////////////////////////////////////////////////

//TODO// #define AUTO_TYPE "~~~PRINT MEM\n~~~"

#ifdef AUTO_TYPE
const char auto_type[] = AUTO_TYPE;
uint auto_i;
uint auto_skip = 100;
uint auto_hold;
byte auto_value;

const char normal_keyboard[] = "@ABCDEFGHIJKLMNOPQRSTUVWXYZ~~~~ 0123456789:;,-./\n";
const char shift_keyboard[] = "@abcdefghijklmnopqrstuvwxyz~~~~ ~!\"#$%'()*+<=>?\n";

constexpr int SHIFTED = 0x100;

int find_keycode(char c) {
    for (uint i = 0; normal_keyboard[i]; i++) {
        if (normal_keyboard[i]==c) {
            return i;
        }
    }
    for (uint i = 0; shift_keyboard[i]; i++) {
        if (shift_keyboard[i]==c) {
            return i + SHIFTED;
        }
    }
    return -1;
}

byte keyboard_response(char c) {
    int code = find_keycode(c);
    if (code < 0) return 0xFF;

    byte col = code & 7;
    byte row = (code>>3) & 7;

    byte probe = ram[0xFF02];
    byte z = 0xff;
    if ((probe & (1u<<col)) == 0) {
        z &= 0xFF ^ (1u << row);
    }
    if (code & SHIFTED) {
        if ((probe & 0x80) == 0) {
            z &= 0xFF ^ (1u << 6);
        }
    }
    PUSH('0' + (15 & (z>>4)));
    PUSH('0' + (15 & (z>>0)));
    return z;
}


#endif

////////////////////////////////////////////////////////

template <class T>
class LegacyEngine {
 public:
  static void InitializePins() {
    for (uint i = 0; i <= 22; i++) {
      gpio_init(i);
      gpio_set_dir(i, GPIO_IN);
      gpio_set_pulls(i, false, false);
    }
    OUTPUT(G_LED, 1);
#if G_SND
    INPUT(G_SND);
#endif
#if G_CART
    OUTPUT(G_CART, 1);
#endif

#if G_CTS
    INPUT(G_CTS);
#endif

#if G_SCS
    INPUT(G_SCS);
#endif

    // OUTPUT(G_SLENB, 0);
    gpio_init(G_SLENB);
    gpio_set_dir(G_SLENB, GPIO_OUT);
    gpio_put(G_SLENB, 0);
    gpio_set_dir(G_SLENB, GPIO_IN);
    gpio_set_pulls(G_SLENB, false, false);

    // OUTPUT( G_HALT  , 0);
    gpio_init(G_HALT);
    gpio_set_dir(G_HALT, GPIO_OUT);
    gpio_put(G_HALT, 0);
    gpio_set_dir(G_HALT, GPIO_IN);
    gpio_set_pulls(G_HALT, false, false);

    // OUTPUT( G_NMI   , 0);
    gpio_init(G_NMI);
    gpio_set_dir(G_NMI, GPIO_OUT);
    gpio_put(G_NMI, 0);
    gpio_set_dir(G_NMI, GPIO_IN);
    gpio_set_pulls(G_NMI, false, false);

#if G_RESET
    INPUT(G_RESET);
    gpio_set_pulls(G_RESET, /*up=*/true, /*down=*/false);
#endif

    for (uint i = 32; i <= 47; i++) {
      gpio_init(i);
      gpio_set_dir(i, GPIO_IN);
      gpio_set_pulls(i, false, false);
    }
    // LED off.
    gpio_init(G_LED);
    gpio_set_dir(G_LED, GPIO_OUT);
    SET_LED(0);
  }

  static void background() {
    while (1) {
      bg_busy = false;
      uint x = POP();
      bg_busy = true;
      HaltOn();

      switch (x >> 24) {
        case 0:
          putchar_raw(255 & x);
          break;

#if 0
        case FIFO_TRIGGER_R >> 24:  // watchpoint read
          printf(" T%x ", (x >> 8) & 0xFFFF);
          break;

        case FIFO_WATCH_R >> 24:  // watchpoint read
          printf(" rd(%x) ", 0xFFFF & x);
          break;

        case FIFO_READ >> 24:  // read cycle
          // printf("+%04Xr%02X ", 0xFFFF & (x>>8), 0xFF&x);
          // putchar('r');
          putchar_raw(C_CYCLE_RD3);
          putchar_raw(x >> 16);
          putchar_raw(x >> 8);
          putchar_raw(x);
          break;

        case FIFO_ROM >> 24:  // ROM cycle
          // printf("\nT %04X:%02X ", (x>>8), 0xFF&x);
          putchar_raw(C_CYCLE_RD3);
          putchar_raw(x >> 16);
          putchar_raw(x >> 8);
          putchar_raw(x);
          break;
#endif

#if FIFO_WRITE
        case FIFO_WRITE >> 24:  // write cycle
          // putchar('w');
          // printf("+%04Xw%02X ", (x>>8), 0xFF&x);
          putchar_raw(C_RAM2_WRITE);
          putchar_raw(x >> 16);
          putchar_raw(x >> 8);
          putchar_raw(x);
          break;
#endif

        case FIFO_NMI >> 24:
          gpio_set_dir(G_NMI, GPIO_OUT);
          // gpio_put(G_NMI, 0);  // Activate NMI
          sleep_us(2);  // for more than a cycle
          // gpio_put(G_NMI, 1);  // Release NMI
          gpio_set_dir(G_NMI, GPIO_IN);

          putchar_raw(C_LOGGING);
          putchar_raw(4 + 128);
          putchar_raw('N');
          putchar_raw('M');
          putchar_raw('I');
          putchar_raw('\n');
          break;

        case FIFO_FLOPPY_LATCH >> 24: {
          static uint last_latch;
          if (x != last_latch) {
            printf(" _%02x ", (x & 0xFF));
            last_latch = x;
          }
        } break;

        case FIFO_FLOPPY_COMMAND >> 24:
          printf(" f!%02x ", (x & 0xFF));
          switch (x & 0xFF) {
            case 0x17:  // seek track
              floppy_track = floppy_buf[0];
              break;

            case 0x80:  // read sector
              printf(" %dr%d", floppy_track, floppy_sector);
              putchar_raw(C_DISK_READ);
              putchar_raw(5 + 128);
              putchar_raw('f');
              putchar_raw(x);
              putchar_raw(floppy_latch);
              putchar_raw(floppy_track);
              putchar_raw(floppy_sector);

              ReceiveSectorData();
              floppy_ptr = floppy_buf;

              printf(" ");
              break;

            case 0xA0:  // write sector
              printf(" %dw%d", floppy_track, floppy_sector);
              putchar_raw(C_DISK_WRITE);
              putchar_raw(0xC4);
              putchar_raw(5 + 128);
              putchar_raw('f');
              putchar_raw(x);
              putchar_raw(floppy_latch);
              putchar_raw(floppy_track);
              putchar_raw(floppy_sector);

              floppy_ptr = floppy_buf;

              break;
          }
          break;
        case FIFO_W_256 >> 24:
          SendSectorData();
          floppy_ptr = floppy_buf;

          printf(" [sent] ");
          break;
        default:
          printf("\nWUT? FIFO %x\n", x);
      }


      HaltOff();

    }
  }

#define STALL_WHILE(PIN, HL, IGNORED)             \
  {                                               \
    while (inline_volatile_gpio_get(PIN) == HL) { \
      tight_loop_contents();                      \
    }                                             \
  }

#define SAY(C) PUSH((C) & 255)

  static void foreground() {
    // Disable interrupts in this "fast" core.
    save_and_disable_interrupts();

    while (true) {
      STALL_WHILE(G_E, CENTIPEDE_INVERT_EQ, 'v');

      const uint signals = volatile_sio_hw->gpio_in;
      const bool reading = ((signals & (1u << G_RW)) != 0);
      const uint abus = volatile_sio_hw->gpio_hi_in & 0xFFFF;
      byte dbus = 0x00;

      constexpr uint NEG_CTS = (1 << G_CTS);
      constexpr uint NEG_SCS = (1 << G_SCS);
      constexpr uint NEG_SELECTS = NEG_CTS | NEG_SCS;

      // YAK
      if (LIKELY((signals & NEG_SELECTS) ==
                 NEG_SELECTS)) {  // Not Special Select

        if (LIKELY(reading)) {
          if (abus >= 0xFF00) {
            IOReader r = Readers[abus & 0x00FF];
            if (r) {
              dbus = r(abus);
              gpio_set_dir_out_masked(0xFF);
              gpio_put_masked(0xFF, dbus);
            } else {
            }
          } else if (T::HasBigRam()) {
            dbus = T::Peek(abus);

            gpio_set_dir(G_SLENB, GPIO_OUT);

            gpio_set_dir_out_masked(0xFF);
            gpio_put_masked(0xFF, dbus);
          } else if (T::UseCoco64kRam(abus)) {
            dbus = T::Peek(abus);

            gpio_set_dir(G_SLENB, GPIO_OUT);
            busy_wait_at_least_cycles(12);  // YAK

            gpio_set_dir_out_masked(0xFF);
            gpio_put_masked(0xFF, dbus);

          } else {
            // don't get involved.  don't gpio_set_dir(G_SLENB, GPIO_OUT).
#if 0
                        // But read the data bus, in case it is useful.
                        STALL_WHILE(G_Q , not CENTIPEDE_INVERT_EQ, 'k');
                        dbus = (byte)sio_hw->gpio_in;  // late grab of data
#endif
          }

          STALL_WHILE(G_E, not CENTIPEDE_INVERT_EQ, 'r');
#if DBUS_HOLD_CYCLES
          busy_wait_at_least_cycles(DBUS_HOLD_CYCLES);
#endif
          gpio_set_dir_in_masked(0xFF);
          gpio_set_dir(G_SLENB, GPIO_IN);

          // END NORMAL READING
        } else {
          // NORMAL CPU WRITING -- we RX
          STALL_WHILE(G_Q, not CENTIPEDE_INVERT_EQ, 'p');
          dbus = (byte)sio_hw->gpio_in;  // late grab of data
          if (T::HasBigRam()) {
            T::Poke(abus, dbus);
          } else if (T::UseCoco64kRam(abus)) {
            T::Poke(abus, dbus);
          } else {
            ram[abus] = dbus;
          }

          IOWriter w = 0;
          if (abus >= 0xFF00) {
            w = Writers[abus & 0x00FF];
            if (w) w(abus, dbus);
          }

#if FIFO_WRITE
          PUSH(FIFO_WRITE | (abus<<8) | dbus);
#endif
          STALL_WHILE(G_E, not CENTIPEDE_INVERT_EQ, 'q');

          // END NORMAL WRITING
        }  // end if (writing) else

      } else {                  // Is Special Select
        if (LIKELY(reading)) {  // Special CPU READING -- we TX
          dbus;

          if (LIKELY((signals & NEG_CTS) == 0)) {  // READ CTS
            dbus = disk11_rom[abus & 0x1FFF];

          } else {  // READ SCS
            dbus = ram[abus];
            switch (abus & 15) {
              case 0x8:  // ReadStatus
                dbus = floppy_status;
                floppy_status &= 1;  // Clear all except BUSY.
                break;
              case 0xB:  // ReadData
                dbus = *floppy_ptr++;
                if ((floppy_latch & 0x80) != 0 && floppy_ptr >= floppy_limit) {
                  floppy_ptr = floppy_buf;
                  PUSH(FIFO_NMI);
                }
                break;
              default:
                break;
            }
          }

          gpio_set_dir_out_masked(0xFF);
          gpio_put_masked(0xFF, dbus);
          STALL_WHILE(G_E, not CENTIPEDE_INVERT_EQ, 's');
#if DBUS_HOLD_CYCLES
          busy_wait_at_least_cycles(DBUS_HOLD_CYCLES);
#endif
          gpio_set_dir_in_masked(0xFF);
        } else {  // Special CPU WRITING -- we RX
          // SAY('W');
          STALL_WHILE(G_Q, not CENTIPEDE_INVERT_EQ, 'p');
          dbus = (byte)sio_hw->gpio_in;  // grab dbus after q drops
          ram[abus] = dbus;

          if (LIKELY((signals & NEG_SCS) == 0)) {
            // WRITE SCS
            switch (abus & 15) {
              case 0x0:  // WriteLatch
                floppy_latch = dbus;
                PUSH(FIFO_FLOPPY_LATCH | dbus);
                break;
              case 0x8:  // WriteCommand
                floppy_status =
                    ((dbus & 0xF0) == 0x80) || ((dbus & 0xF0) == 0xA0)
                        ? 0x02
                        : 0x00;  // YAK

                floppy_ptr = floppy_buf;  // Reset pointer.
                if (dbus == 0x17)
                  floppy_track = floppy_buf[0];  // was losing critical race

                PUSH(FIFO_FLOPPY_COMMAND | dbus);
                break;
              case 0x9:  // WriteTrack
                floppy_track = dbus;
                break;
              case 0xA:  // WriteSector
                floppy_sector = dbus;
                break;
              case 0xB:  // WriteData
                *floppy_ptr++ = dbus;
                if ((floppy_latch & 0x80) != 0 && floppy_ptr >= floppy_limit) {
                  PUSH(FIFO_W_256);
                  PUSH(FIFO_NMI);
                }
                break;
              default:
                break;
            }
          }  // end write SCS

          STALL_WHILE(G_E, not CENTIPEDE_INVERT_EQ, 's');

        }  // end read or write
      }  // end if special
    }  // end while true
  }  // end foreground()

  static void RunLegacy() {
    // foreground must be fast.
    multicore_launch_core1(foreground);

    // background on core 0 handles interrupts.
    background();
  }
};  // end LegacyEngine

class Engine0 :
    // public DoCoco3Mmu<Engine0>,
    public SmallRam<Engine0>,
    public DoCoco64k<Engine0>,
    public LegacyEngine<Engine0> {
 public:
  static void Run() {
    // T::InitCoco3Mmu();
    InitCoco64k();
    RunLegacy();
  }
};

#define METADATA_MAX_LEN 256
#define METADATA_ADDR (const uint8_t *)(0x10FFF000)

// +2 guarantees space for the EOF double-NUL even if the string is 256 bytes
char Label[METADATA_MAX_LEN + 2];

void InitLabel() {
    for (uint32_t i = 0; i < METADATA_MAX_LEN; i++) {
        uint8_t b = *(METADATA_ADDR + i);

        // Treat 0xFF (erased flash) the same as 0x00 (EOF)
        if (b == 0x00 || b == 0xFF) {
            Label[i] = '\0';
            Label[i+1] = '\0';
            return;
        }

        if (b == '=' || b == ',') {
            Label[i] = '\0';
        } else {
            Label[i] = (char)b;
        }
    }

    // Safety net: If the string was exactly 256 bytes without a NUL/0xFF,
    // force the double-NUL at the end.
    Label[METADATA_MAX_LEN] = '\0';
    Label[METADATA_MAX_LEN + 1] = '\0';
}

void PrintLabel() {
    // CRITICAL: If you print over USB, wait for the terminal to connect!
    // (If you print over UART, you can remove this while-loop)
    while (!stdio_usb_connected()) {
        sleep_ms(10);
    }

    if (Label[0]=='p' && Label[1]=='\0' && Label[2]=='1' && Label[3]=='\0') {
        const char* p = Label;

        while (*p) {
            const char* q = p + strlen(p) + 1;

            // Print the key and value
            printf("[%s=%s]\n", p, q);

            // Advance p to the next key
            p = q + strlen(q) + 1;
        }
    } else {
        printf("Label did not start with p=1\n");
        printf("Memory dump at 0x10FFF000: ");
        for(int i=0; i<16; i++) {
            printf("%02X ", Label[i]);
        }
        printf("\n");
    }
}

const char* GetLabel(const char* key) {
    // Guard against null pointers or empty search keys
    if (!key || key[0] == '\0') {
        return nullptr;
    }

    const char* p = Label;

    // Iterate through the array. 
    // The loop breaks when p points to the final empty key (the double NUL).
    while (*p != '\0') {
        const char* current_key = p;
        
        // The value starts immediately after the current key's NUL terminator
        const char* current_value = current_key + strlen(current_key) + 1;

        // Check if we found a match
        if (strcmp(current_key, key) == 0) {
            return current_value;
        }

        // Advance 'p' to the start of the next key.
        // This is immediately after the current value's NUL terminator.
        p = current_value + strlen(current_value) + 1;
    }

    // Key was not found in the array
    return nullptr;
}

int main() {
  Engine0::InitializePins();
  InitLabel();
#if MHz != 150
  set_sys_clock_khz(MHz * 1000, true);
#endif
  stdio_usb_init();

  for (uint i = 0; i < 6; i++) {
    SET_LED(1);
    sleep_ms(200);

    SET_LED(0);
    sleep_ms(200);
  }
  PrintLabel();

  Engine0::Run();
}
