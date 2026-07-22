#ifndef _GSPOON_H_
#define _GSPOON_H_

// Gerbil SPOON

// This succeeded for a spoon-feeding Proof-Of-Concept that sends NMI
// two seconds after boot to a coco2, capturing the registers
// during the write cycles, keeping control of the coco2 via HALT,
// and then poking "67" on the screen at $0502 by spoon-fed instructions.

// The Gerbil continues to operate the PIO wheel,
// so this is called "gspoon".

namespace gspoon {

volatile bool drive_console_ready = false;

struct addr_byte {
  uint a;
  byte b;
};

struct addr_byte Coco2StartupPokes[] = {
    // PIA0
    {0xff01, 0x00},  // choose data directions
    {0xff03, 0x00},  // choose data directions
    {0xff00, 0x00},  // PA* all inputs
    {0xff02, 0xff},  // PB* all outputs
    {0xff01, 0x34},  // CA2 is 0. Choose data. disable irqA (irq on HS)
    {0xff03, 0x34},  // CB2 is 0. Choose data. disable irqB (irq on FS)
    // PIA1
    {0xff21, 0x00},  // choose data directions
    {0xff23, 0x00},  // choose data directions
    {0xff20, 0xfe},  // PA* outputs, execpt PA0 input.
    {0xff22, 0xfe},  // PB* outputs, execpt PB0 input. // add
    //
    {0xff21, 0x34},  // CA2 is 0. Choose data. disable irqA (firq on rs232)
    {0xff23, 0x37},  // CB2 is 1.
    {0xff22, 0x00},  // PB* are zero.
    {0xff20, 0x02},  // PA* are 0. execpt PA1 is 1 (rs232 out)
    // END
    {0, 0}};

static constexpr uint MAX_LOGS = 100;

struct LogItem {
  char kind;
  uint abus;
  uint want_abus;
  byte dbus;
  byte want_dbus;
  const char* mark;
};
LogItem Logs[MAX_LOGS];
uint Log_step;

const PIO pio = pio0;
constexpr uint sm = 0;

void PrintLog() {
  printf("----\n");
  uint j = 1;
  for (uint i = 0; i < Log_step; i++) {
    struct LogItem* p = Logs + i;
    if (p->mark) {
      printf("----- %s\n", p->mark);
      j = 1;
    } else {
      char k = p->kind;
      if (k < 32 || k > 126) k = '?';
      printf("[%3d.] '%c' %04x %02x (%04x %02x)\n", j, p->kind, p->abus,
             p->dbus, p->want_abus, p->want_dbus);
      j++;
    }
  }
}

void IN_RAM Mark(const char* mark) {
  struct LogItem* p = Logs + Log_step;
  p->mark = mark;
  ++Log_step;
}

void IN_RAM Log(char kind, uint abus, byte dbus, uint want_addr,
                byte feed_data) {
  if (Log_step < MAX_LOGS) {
    struct LogItem* p = Logs + Log_step;
    p->kind = kind;
    p->abus = abus;
    p->dbus = dbus;
    p->want_abus = want_addr;
    p->want_dbus = feed_data;
    ++Log_step;
  }
}

#define PREMISE                                           \
  bool ok = true;                                         \
  const uint signals = GERBIL_GET();                      \
  const bool reading = ((signals & (1u << G_RW)) != 0);   \
  const uint abus = volatile_sio_hw->gpio_hi_in & 0xFFFF; \
  if (want_addr && abus != want_addr) ok = false;         \
  byte dbus = feed_data;

bool IN_RAM IdleStep(uint want_addr = 0, byte feed_data = 0) {
  want_addr = 0xFFFF;
  PREMISE
  if (reading) {  // ------ case READ CYCLE
    GERBIL_DRIVE(0xFF);
  } else {  // ------ case WRITE CYCLE
    ok = false;
    dbus = (byte)GERBIL_GET();
  }
  Log('i' - (reading ? 0 : 32), abus, dbus, want_addr, feed_data);
  return ok;
}

byte IN_RAM GrabStep() {
  constexpr uint want_addr = 0;
  constexpr byte feed_data = 0;
  PREMISE
  if (reading) {  // ------ case READ CYCLE
    GERBIL_PASS();
    dbus = (byte)(GERBIL_GET());  // log & debug
  } else {                        // ------ case WRITE CYCLE
    ok = false;
    dbus = (byte)GERBIL_GET();
  }
  Log('g' - (reading ? 0 : 32), abus, dbus, want_addr, feed_data);
  return dbus;
}

bool IN_RAM ReadStep(uint want_addr = 0, byte feed_data = 0) {
  PREMISE
  if (reading) {  // ------ case READ CYCLE
    GERBIL_DRIVE(feed_data);
  } else {  // ------ case WRITE CYCLE
    ok = false;
    dbus = (byte)GERBIL_GET();
  }
  Log('r' - (reading ? 0 : 32), abus, dbus, want_addr, feed_data);
  return ok;
}

bool IN_RAM WriteStep(uint want_addr = 0, byte feed_data = 0) {
  PREMISE
  if (reading) {  // ------ case READ CYCLE
    ok = false;
    GERBIL_DRIVE(0xFF);
  } else {  // ------ case WRITE CYCLE
    dbus = (byte)GERBIL_GET();
  }
  if (feed_data && dbus != feed_data) ok = false;
  Log('w' - (reading ? 0 : 32), abus, dbus, want_addr, feed_data);
  return ok;
}

uint IN_RAM AnyStep(bool to_log = true) {
  constexpr uint want_addr = 0;
  byte feed_data = 0x7E;
  PREMISE
  if (reading) {  // ------ case READ CYCLE
    GERBIL_DRIVE(feed_data);
  } else {  // ------ case WRITE CYCLE
    dbus = (byte)GERBIL_GET();
  }
  if (to_log) {
    Log('a' - (reading ? 0 : 32), abus, dbus, 0, 0x7E);
  }
  return abus;
}

// Sync on two cycles of the 4-cycle instruction "JMP $7E7E".
void IN_RAM Synchronize7E() {
  // Runs in Foreground.
  // Performance Critical to keep up with the Gerbil.
  uint a;
  while (1) {
    a = AnyStep(false);
    if (a != 0x7e7e) continue;
    a = AnyStep(false);
    if (a != 0x7e7f) continue;
    a = AnyStep(false);
    if (a != 0x7e80) continue;
    a = AnyStep(false);
    if (a != 0xffff) continue;
    a = AnyStep(false);
    if (a != 0x7e7e) continue;
    a = AnyStep(false);
    if (a != 0x7e7f) continue;
    a = AnyStep(false);
    if (a != 0x7e80) continue;
    a = AnyStep(false);
    if (a != 0xffff) continue;
    break;
  }
}

void IN_RAM SpoonFeeder() {
  // Don't block waiting for DriveConsole — start immediately on USB.
  // Coco2 I/O is added dynamically when DriveConsole becomes ready
  // (tcl_io::active_io is updated by the foreground).

  // Print startup banner
  tcl_io::emit_string("COPICO CENTIPEDE CONSOLE\n");

  console::inkey_state iks = {};
  char line[256];

  // Tcl REPL — exits when user types "bye"
  while (true) {
    // Print prompt
    tcl_io::emit_string("TCL>");

    int line_pos = 0;

    // Read a line from any active input
    while (true) {
      byte key = tcl_io::poll_key(&iks);
      if (key == 0) {
        sleep_ms(20);  // ~50 Hz polling
        continue;
      }

      if (key == 13) {  // Enter
        tcl_io::emit('\n');
        break;
      }
      if (key == 8 && line_pos > 0) {  // Backspace
        line_pos--;
        tcl_io::emit(8);
        tcl_io::emit(' ');  // Overwrite character on screen
        tcl_io::emit(8);
        continue;
      }
      if (key == 127 && line_pos > 0) {  // DEL (common USB terminal)
        line_pos--;
        tcl_io::emit(8);
        tcl_io::emit(' ');
        tcl_io::emit(8);
        continue;
      }
      if (key >= 0x20 && line_pos < 254) {
        line[line_pos++] = (char)key;
        tcl_io::emit(key);
      }
    }
    line[line_pos] = '\0';

    // "bye" exits the console
    if (strcmp(line, "bye") == 0) {
      if (tcl_io::active_io & tcl_io::IO_COCO2) {
        // Coco2 is active — tell foreground to exit DriveConsole
        // and launch Coco2 into Disk Basic.
        tcl_io::emit_string("Launching Coco2...\n");
        uint cmd = ((uint)BG2FG_EXIT_CONSOLE << 24);
        while (!bg2fg.push(cmd)) {
          sleep_ms(1);
        }
        // DriveConsole will clear IO_COCO2 on exit.
      } else {
        tcl_io::emit_string("Goodbye.\n");
      }
      printf("SpoonFeeder: bye, returning to background.\n");
      return;  // Return to spoon_task
    }

    if (line_pos > 0) {
      int result = Tcl_Eval(global_tcl_interp, line, 0, (char**)0);
      const char* output = global_tcl_interp->result;
      if (output && output[0]) {
        tcl_io::emit_string(output);
        tcl_io::emit('\n');
      }
    }
  }
}  // SpoonFeeder

void IN_RAM Jump(uint a) {
  // Runs in Foreground.
  // Performance Critical to keep up with the Gerbil.
  Synchronize7E();

  ReadStep(0, 0x7E);  // 7E => JMP extended
  ReadStep(0, (byte)(a >> 8));
  ReadStep(0, (byte)a);
}

byte IN_RAM Peek1(uint a) {
  // Runs in Foreground.
  // Performance Critical to keep up with the Gerbil.
  Synchronize7E();

  ReadStep(0, 0xF6);  // F6 => LDB extended
  ReadStep(0, (byte)(a >> 8));
  ReadStep(0, (byte)a);
  IdleStep();
  return GrabStep();
}

void IN_RAM Poke1(uint a, byte x) {
  // Runs in Foreground.
  // Performance Critical to keep up with the Gerbil.
  Synchronize7E();

  ReadStep(0, 0xCC);  // CC => LDD #immediate
  ReadStep(0, 0);
  ReadStep(0, (byte)x);

  ReadStep(0, 0xF7);  // F7 => STB extended
  ReadStep(0, (byte)(a >> 8));
  ReadStep(0, (byte)a);

  IdleStep();
  WriteStep(a, x);
}

void IN_RAM Poke2(uint a, uint x) {
  // Runs in Foreground.
  // Performance Critical to keep up with the Gerbil.
  Synchronize7E();

  ReadStep(0, 0xCC);  // CC => LDD #immediate
  ReadStep(0, (byte)(x >> 8));
  ReadStep(0, (byte)x);

  ReadStep(0, 0xFD);  // FD => STD extended
  ReadStep(0, (byte)(a >> 8));
  ReadStep(0, (byte)a);

  IdleStep();
  WriteStep(a, (byte)(x >> 8));
  WriteStep(a, (byte)x);
}

void IN_RAM DriveConsole() {
  // Runs in Foreground.
  // Performance Critical to keep up with the Gerbil.

  drive_console_ready = true;
  tcl_io::add_coco2();  // SpoonFeeder can now use Coco2 I/O

  while (true) {
    uint z = 0;
    AnyStep();  // keep gerbil fed (6809 runs JMP $7E7E)
    bool ok = bg2fg.pop(z);

    AnyStep();  // keep gerbil fed before taking action
    if (ok) {
      uint cmd = z >> 24;
      uint addr = (z >> 8) & 0xFFFF;
      byte data = z & 0xFF;

      if (cmd == BG2FG_PEEK) {
        byte val = Peek1(addr);
        PUSH_TO_BG(FG2BG_PEEK_REPLY, addr, val);
      } else if (cmd == BG2FG_POKE) {
        Poke1(addr, data);
      } else if (cmd == BG2FG_EXIT_CONSOLE) {
        // "bye" from SpoonFeeder — reset 6809 and return to normal.
        Jump(0xA027);  // Jump via the 6809 RESET vector
        drive_console_ready = false;
        tcl_io::remove_coco2();  // SpoonFeeder continues on USB only
        printf("DriveConsole: bye, returning to normal foreground.\n");
        return;  // Return to SpoonfeedConsoleOnReset, then to foreground()
      } else {
        printf("DriveConsole: unknown bg2fg cmd %d\n", cmd);
      }
    }
  }
}

void OldSpoonfeedingExperiments();

void IN_RAM SpoonfeedConsoleOnReset() {
  // Runs in Foreground.
  // Performance Critical to keep up with the Gerbil.

  // Start SpoonFeeder if not already running (e.g., from USB-only boot).
  // If already running, this is a no-op — SpoonFeeder will notice
  // IO_COCO2 being added when DriveConsole sets drive_console_ready.
  if (!spoon_has_work) {
    PUSH_TO_BG(FG2BG_SPOON_ON_RESET, 0, 0);
  }

  for (struct addr_byte* p = Coco2StartupPokes; p->a; p++) {
    Poke1(p->a, p->b);
  }

  // Clear all SAM bits except FFC9, so 0x0400 is text frame buffer.
  for (uint a = 0xFFC0; a < 0xFFE0; a += 2) {
    Poke1(a, 42);
  }
  Poke1(0xFFC9, 42);  // Use 0x0400 for frame buffer

  for (uint a = 0x0000; a < 0x0600; a++) {
    // Show what page number is being displayed.
    // Expect '4' (top half) and '5' (bottom half).
    Poke1(a, (byte)('0' + (a >> 8)));
    // was // Poke1(a, (byte)0xE1);
  }

  // Display that for around 1 seconds, before we continue.
  for (uint k = 0; k < (1 << 20); k++) {
    AnyStep();
  }
  // now change those to little boxes
  for (uint a = 0x0000; a < 0x0600; a++) {
    Poke1(a, (byte)0xE1);
  }
  // Then continue with the Console driver.

  // OldSpoonfeedingExperiments();
  DriveConsole();
}  // end SpoonfeedConsoleOnReset

#if 0
void IN_RAM OldSpoonfeedingExperiments() {
  for (uint a = 0x0500; a < 0x0600; a++) {
    Poke1(a, (byte)a);
  }
  for (uint a = 0x04C0; a < 0x04E0; a += 2) {
    Poke1(a, 0xB0 + ((a >> 1) & 15));
    Poke1(a + 1, '*');
  }

  while (true) {
    uint bit = 0x80;
    for (uint i = 0; i < 8; i++) {
      Poke1(0xFF02, 255 ^ bit);
      byte x = Peek1(0xFF00);

      for (uint j = 0; j < 8; j++) {
        uint addr = 0x400 + 16 * i + 2 * j;
        Poke1(addr, (x & 1) ? '.' : '*');
        x >>= 1;
      }

      bit >>= 1;
    }
  }

  uint cursor = 0x0420;
  for (uint round = 0; true; round++) {
    uint z = 0;
    AnyStep();
    bool ok = bg2fg.pop(z);
    AnyStep();
    if (ok && z <= 255) {
      Poke1(cursor, (byte)z);
      ++cursor;
    }
    AnyStep();
  }
}
#endif

void IN_RAM SpoonNMI() {
  bool ok = true;
  ASSERT_NMI();
  Mark("Assert Halt");
  ASSERT_HALT();
  for (uint i = 0; i < 20; i++) AnyStep();
  ASSERT_NMI();
  Mark("Assert NMI");
  for (uint i = 0; i < 2; i++) AnyStep();
  RELEASE_NMI();
  Mark("Release NMI");
  RELEASE_HALT();
  Mark("Release Halt");
  for (uint i = 0; i < 29; i++) AnyStep();

  Mark("did NMI plus more");

  ASSERT_HALT();
  Mark("AssertHalt, 6");
  for (uint i = 0; i < 6; i++) AnyStep();

  RELEASE_HALT();
  Mark("ReleaseHalt, 2");
  for (uint i = 0; i < 2; i++) AnyStep();

  Mark("LDD #$3637");
  (ReadStep(0, 0xCC));  // CC => LDD #immediate
  (ReadStep(0, 0x36));
  (ReadStep(0, 0x37));

  Mark("STD #$0502");
  (ReadStep(0, 0xFD));  // FD => STD extended
  (ReadStep(0, 0x05));
  (ReadStep(0, 0x02));

  ASSERT_HALT();
  Mark("Assert Halt & RUNOUT 20");
  for (uint i = 0; i < 20; i++) AnyStep();
  SAY('$');
  PrintLog();
  while (1) {
    sleep_us(1);
  }
}

}  // end namespace gspoon
#endif  // _GSPOON_H_
