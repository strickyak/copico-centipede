#ifndef CENTIPEDE_FIRMWARE_FLOPPY_H_
#define CENTIPEDE_FIRMWARE_FLOPPY_H_

byte floppy_latch;
byte floppy_command;
byte floppy_status;
byte floppy_track;
byte floppy_sector;
byte *floppy_ptr;

byte floppy_buf[256];
#define floppy_limit (256 + floppy_buf)

template <typename T>
struct DontFloppy {
  static void BackgroundFifoFloppyLatch(byte chore_byte) {
    printf("# Floppy Latch not installed\n");
  }
  static void BackgroundFifoFloppyCommand(uint chore, byte chore_byte) {
    printf("# Floppy Command not installed\n");
  }
  static void BackgroundFifoFloppyW256() {
    printf("# Floppy W256 not installed\n");
  }
  static void ReadScsFloppy(const uint &abus, byte &dbus) {
    printf("# Floppy (ReadScs) not installed\n");
    dbus = 0xFF;
  }
  static void WriteScsFloppy(const uint &abus, byte &dbus) {
    printf("# Floppy (WriteScs) not installed\n");
    dbus = 0xFF;
  }
};

template <typename T>
struct DoFloppy {
  static void SendSectorData() {
    for (uint i = 0; i < 256; i++) {
      putchar_raw(floppy_buf[i]);
    }
  }

  static void ReceiveSectorData() {
    char c = 0;
    ReadUsbStream(&c, 1);

    if (byte(c) != 0xAD) {
      printf(" ReceiveSectorData: c=%d. \n", c);
      T::Fatal("bad c", (byte)c);
    }

    int needed = 7;
    char *p = (char *)floppy_buf;  // first write with unneeded header
    ReadUsbStream(p, needed);

    needed = 256;
    p = (char *)floppy_buf;  // overwrite with good data
    ReadUsbStream(p, needed);
  }

  static void BackgroundFifoFloppyLatch(byte chore_byte) {
    static byte last_latch;
    if (chore_byte != last_latch) {
      printf(" _%02x ", chore_byte);
      last_latch = chore_byte;
    }
  }

  static void BackgroundFifoFloppyCommand(uint chore, byte chore_byte) {
    printf(" f!%02x ", chore_byte);
    switch (chore_byte) {
      case 0x17:  // seek track
        floppy_track = floppy_buf[0];
        break;

      case 0x80:  // read sector
        printf(" %dr%d", floppy_track, floppy_sector);
        putchar_raw(C_DISK_READ);
        putchar_raw(5 + 128);  // 5 bytes.
        putchar_raw('f');
        putchar_raw(chore);
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
        putchar_raw(0xC4);     // 4 chunks of 64, plus
        putchar_raw(5 + 128);  // 5 more bytes.
        putchar_raw('f');
        putchar_raw(chore);
        putchar_raw(floppy_latch);
        putchar_raw(floppy_track);
        putchar_raw(floppy_sector);

        floppy_ptr = floppy_buf;

        break;
    }
  }
  static void BackgroundFifoFloppyW256() {
    SendSectorData();
    floppy_ptr = floppy_buf;

    printf(" [sent] ");
  }

  static void ReadScsFloppy(const uint &abus, byte &dbus) {
    // SAY('S');
    // CASE special read SCS
    switch (abus & 15) {
      case 0x8:  // ReadStatus
        dbus = floppy_status;
        floppy_status &= 1;  // Clear all except BUSY.
        break;
      case 0xB:  // ReadData
        dbus = *floppy_ptr++;
        if ((floppy_latch & 0x80) != 0 && floppy_ptr >= floppy_limit) {
          floppy_ptr = floppy_buf;
          PUSH_TO_BG(FIFO_NMI, 0, 0);
        }
        break;
      default:
        dbus = ram[abus];
        break;
    }
  }

  static void WriteScsFloppy(const uint &abus, byte &dbus) {
    // WRITE SCS
    switch (abus & 15) {
      case 0x0:  // WriteLatch
        floppy_latch = dbus;
        PUSH_TO_BG(FIFO_FLOPPY_LATCH, 0, dbus);
        break;
      case 0x8:  // WriteCommand
        floppy_status = ((dbus & 0xF0) == 0x80) || ((dbus & 0xF0) == 0xA0)
                            ? 0x02
                            : 0x00;  // YAK

        floppy_ptr = floppy_buf;  // Reset pointer.
        if (dbus == 0x17)
          floppy_track = floppy_buf[0];  // was losing critical race

        PUSH_TO_BG(FIFO_FLOPPY_COMMAND, 0, dbus);
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
          PUSH_TO_BG(FIFO_W_256, 0, 0);
          PUSH_TO_BG(FIFO_NMI, 0, 0);
        }
        break;
      default:
        break;
    }
  }
};

#endif  // CENTIPEDE_FIRMWARE_FLOPPY_H_
