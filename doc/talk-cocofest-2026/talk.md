# An RP2350B walks into a 6809 bar

https://github.com/strickyak/copico-centipede/

https://github.com/strickyak/copico-centipede/blob/main/doc/talk-cocofest-2026/talk.md

The RP2350 is the microcontroller in the Raspberry Pi Pico 2 boards.
(the RP2040 was in the Raspberry Pi Pico 1 board).

| Board | Year | Microcontroller | Num Usable GPIO Pins | Num PIO | Num SM | 5V Tolerant |
|-------|------|-----------------|----------------------|---------|--------|-------------|
| Pi Pico 1 | 2021 | RP2040        | 26                   | 2       | 8      |  no         |
| Pi Pico 2 | 2024 | RP2350A       | 26                   | 3       | 12      |  Yes         |
| Pi Pico 2 "XL" | 2025 | RP2350B       | 46                   | 3       | 12      |  Yes         |

The Copico project, started by Thomas Shanks, has been using them
experimentally to enhance Cocos for the last couple of years.

* Thomas Shanks (project founder)
* Phoenix Brown
* Rachel Keslensky
* Henry Strickland

| Year | Name | What |
|------|------|------|
| 2024 | Copico | Coco Cart with Pi Pico 1; very flexible, experimental |
| 2024 | Aardvark | Coco Cart with Pi Pico 1; pioneered Spoon-Feeding  |
| 2025 | Bonobo   | Coco Cart with Pi Pico 1; Spoon-Feeding boot & network device for NekotOS gaming OS |
| 2026 | Centipede | Coco Cart with RP2350B; can do anything & everything? |

## What can we fake

Today:

* Cartridge ROM
* Extra RAM (64K for 16K machine) (but with VDG RAM Aliasing problem)
* Floppy Drives
* Crisp, clean VDG screen (to atone for the VDG RAM Aliasing problem)

Future:

* 128K, 256K, 512K? and MMU as in a Coco3
* CocoSDC
* Orchestra-90 (two 8-bit analog outputs)
* VGA output
* Ethernet / Internet access
* fast Fujinet (ESP32-DevKit hat)
* fast Drivewire
* various I2C or SPI or UART devices (sensors, controls, displays)
* NekotOS (realtime gaming OS)

## GOSUB images/ FOLDER and RETURN

## C++ Framework

```
// IO Ports and RAM

using IOReader = std::function<byte(uint addr)>;
using IOWriter = std::function<void(uint addr, byte data)>;

IOReader Readers[256];
IOWriter Writers[256];

byte ram[128 * 1024];
```

## C++ Curiously-Recurring Template Pattern

```C
template <class T>
class DontLog { public: void Log(const char* s) {} };

template <class T>
class DoLog { public: void Log(const char* s) {
    printf("LOG: <%s>\n", s);
} };

//////////////////

template <class T>
class BigRam { public:
    uint LogicalToPhysical(uint addr) { ... }
    byte Peek(uint addr) { ... }
    void Poke(uint addr, byte x) { ... }
};

template <class T>
class SmallRam { public:
    uint LogicalToPhysical(uint addr) { ... }
    byte Peek(uint addr) { ... }
    void Poke(uint addr, byte x) { ... }
};

/////////////////

template <class T>
class Common { public:
    void Run() { T::Log("running"); ... }
};

class FastEngine:
    public Common<FastEngine>,
    public DontLog<FastEngine>,
    public SmallRam<FastEngine> { };

class SlowEngine:
    public Common<FastEngine>,
    public DoLog<FastEngine>,
    public BigRam<FastEngine> { };

int main() {
    if (fast) (new FastEngine)->Run();
    else (new SlowEngine)->Run();
}
```

## If CTS and SCS are NOT active:

### Read Cycle

```C
        if (LIKELY(reading)) {
          if (abus >= 0xFF00) {
            IOReader r = Readers[abus & 0x00FF];
            if (r) {
              dbus = r(abus);
              gpio_set_dir_out_masked(0xFF);
              gpio_put_masked(0xFF, dbus);
            } else {
            }
          } else if (T::UseCoco64kRam(abus)) {
            dbus = T::Peek(abus);

            gpio_set_dir(G_SLENB, GPIO_OUT);
            busy_wait_at_least_cycles(12);  // YAK

            gpio_set_dir_out_masked(0xFF);
            gpio_put_masked(0xFF, dbus);

          } else {
            // Don't get involved.
          }
          STALL_WHILE(G_E, not CENTIPEDE_INVERT_EQ, 'r');
          gpio_set_dir_in_masked(0xFF);
          gpio_set_dir(G_SLENB, GPIO_IN);
        } else {
```

### Write Cycle

```C
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
```

## If either CTS or SCS is active:

```C
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
          gpio_set_dir_in_masked(0xFF);

        } else {  // Special CPU WRITING -- we RX

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
```

## How can we fake 64RAM for a 16K coco2?

```C
// Two SAM bits are needed:
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
    for (uint a = 0xFFD4; a < 0xFFE0; a++) {
      Writers[255 & a] = WriteOtherSamBit;
    }

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
  }
  static void WriteFFD5_P1Set(uint a, byte d) {
    SamP1Bit = true;
  }
  static void WriteFFDE_TyClear(uint a, byte d) {
    SamTyBit = false;
  }
  static void WriteFFDF_TySet(uint a, byte d) {
    SamTyBit = true;
  }
};
```

## Adding an ACIA (UART) M6850

This needs some IRQ support in the `foreground()` routine.

```C
// Motorola 6850 Asynchronous Commuication Interface Adapter (UART)

template <typename T>
struct DontAcia {
  constexpr static bool DoesAcia() { return false; }
};
template <typename T>
struct DoAcia {
  constexpr static bool DoesAcia() { return true; }

  static void Acia_Install(uint port) {
    uint sub = 255 & port;
    // Readers

    IOReaders[sub + 0] = [](uint addr, byte data) {
        // MC6850 Status Read
      data = 0x02;  // Transmit buffer always considered empty.
      data |= (acia_irq_firing) ? 0x80 : 0x00;
      data |= (acia_char_in_ready) ? 0x01 : 0x00;

      acia_irq_firing = false;  // Side effect of reading status.
      return data;
    };
    IOReaders[sub + 1] = [](uint addr, byte data) {
        // MC6850 Data Read
      if (acia_char_in_ready) {
        data = acia_char;
        acia_char_in_ready = false;
      } else {
        data = 0;
      }
      return data;
    };

    // Writers

    IOWriters[sub + 0] = [](uint addr, byte data) {
        // MC6850 Command Write
      if ((data & 0x80) != 0) {
        acia_irq_enabled = true;
      } else {
        acia_irq_enabled = false;
      }
    };

    IOWriters[sub + 1] = [](uint addr, byte data) {
        // MC6850 Data Write
      if (data == 0 || data >= 128) {
        putbyte(C_PUTCHAR);
      }  // otherwise, normal 7-bit chars don't need the prefix.
      putbyte(data);
    };
  }
};
```
