#ifndef _GSPOON_H_
#define _GSPOON_H_

// This succeeded for a spoon-feeding Proof-Of-Concept that sends NMI
// two seconds after boot to a coco2, capturing the registers
// during the write cycles, keeping control of the coco2 via HALT,
// and then poking "67" on the screen at $0502 by spoon-fed instructions.

// The Gerbil continues to operate the PIO wheel,
// so this is called "gspoon".

namespace gspoon {

static constexpr uint MAX_LOGS = 1000;

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

void Mark(const char* mark) {
  struct LogItem* p = Logs + Log_step;
  p->mark = mark;
  ++Log_step;
}

void Log(char kind, uint abus, byte dbus, uint want_addr, byte feed_data) {
  struct LogItem* p = Logs + Log_step;
  p->kind = kind;
  p->abus = abus;
  p->dbus = dbus;
  p->want_abus = want_addr;
  p->want_dbus = feed_data;
  ++Log_step;
}

template <typename T>
#define PREMISE                                           \
  bool ok = true;                                         \
  const uint signals = GERBIL_GET();                      \
  const bool reading = ((signals & (1u << G_RW)) != 0);   \
  const uint abus = volatile_sio_hw->gpio_hi_in & 0xFFFF; \
  if (want_addr && abus != want_addr) ok = false;         \
  byte dbus = feed_data;

bool IdleStep(uint want_addr = 0, byte feed_data = 0) {
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

bool ReadStep(uint want_addr = 0, byte feed_data = 0) {
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

bool WriteStep(uint want_addr = 0, byte feed_data = 0) {
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

bool AnyStep(uint want_addr = 0, byte feed_data = 0) {
  if (!feed_data) feed_data = 0x7E;
  PREMISE
  if (reading) {  // ------ case READ CYCLE
    GERBIL_DRIVE(feed_data);
  } else {  // ------ case WRITE CYCLE
    dbus = (byte)GERBIL_GET();
  }
  Log('a' - (reading ? 0 : 32), abus, dbus, want_addr, feed_data);
  return ok;
}

#define M(X)             \
  {                      \
    ok = X;              \
    if (!ok) goto ERROR; \
  }

void SpoonNMI() {
  bool ok = true;
  ASSERT_NMI();
  Mark("Assert Halt");
  ASSERT_HALT();
  for (uint i = 0; i < 20; i++) M(AnyStep())
  ASSERT_NMI();
  Mark("Assert NMI");
  for (uint i = 0; i < 2; i++) M(AnyStep())
  RELEASE_NMI();
  Mark("Release NMI");
  RELEASE_HALT();
  Mark("Release Halt");
  for (uint i = 0; i < 29; i++) M(AnyStep())

  Mark("did NMI plus more");

  ASSERT_HALT();
  Mark("AssertHalt, 6");
  for (uint i = 0; i < 6; i++) M(AnyStep())

  RELEASE_HALT();
  Mark("ReleaseHalt, 2");
  for (uint i = 0; i < 2; i++) M(AnyStep())

  Mark("LDD #$3637");
  M(ReadStep(0, 0xCC));  // CC => LDD #immediate
  M(ReadStep(0, 0x36));
  M(ReadStep(0, 0x37));

  Mark("STD #$0502");
  M(ReadStep(0, 0xFD));  // FD => STD extended
  M(ReadStep(0, 0x05));
  M(ReadStep(0, 0x02));

  ASSERT_HALT();
  Mark("Assert Halt & RUNOUT 20");
  for (uint i = 0; i < 20; i++) M(AnyStep())
  SAY('$');
  PrintLog();
  while (1) {
    sleep_us(1);
  }

ERROR:
  SAY('!');
  PrintLog();
  while (1) {
    sleep_us(1);
  }
}

}  // end namespace gspoon
#endif  // _GSPOON_H_
