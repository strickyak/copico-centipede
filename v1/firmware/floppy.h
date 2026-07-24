#ifndef CENTIPEDE_FIRMWARE_FLOPPY_H_
#define CENTIPEDE_FIRMWARE_FLOPPY_H_

#include <string.h>

#include "cobs_tx.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

byte floppy_latch;
byte floppy_command;
// atomic with release/acquire ordering: ensures floppy_buf writes
// (by background) are visible to foreground when DRQ is observed.
std::atomic<byte> floppy_status{0};
byte floppy_track;
byte floppy_sector;
volatile byte *floppy_ptr;  // volatile: both cores access

byte floppy_buf[256];
#define floppy_limit (256 + floppy_buf)

template <typename T>
struct DontFloppy {
  static void BackgroundFifoFloppyLatch(byte chore_byte) {
    cobs_printf("# Floppy Latch not installed\n");
  }
  static void BackgroundFifoFloppyCommand(Coro &self, uint chore,
                                          byte chore_byte) {
    cobs_printf("# Floppy Command not installed\n");
  }
  static void BackgroundFifoFloppyW256() {
    cobs_printf("# Floppy W256 not installed\n");
  }
  static void ReadScsFloppy(const uint &abus, byte &dbus) {
    cobs_printf("# Floppy (ReadScs) not installed\n");
    dbus = 0xFF;
  }
  static void WriteScsFloppy(const uint &abus, byte &dbus) {
    cobs_printf("# Floppy (WriteScs) not installed\n");
    dbus = 0xFF;
  }
};

template <typename T>
struct DoFloppy {
  static void SendSectorData() {
    CobsEncodeAndTransmit(floppy_buf, 256, putchar_raw);
  }

  static void ReceiveSectorData(Coro &self) {
    std::string *pkt = nullptr;
    while (!pkt) {
      PumpUsbCobs();
      pkt = usb_packet_buf.Yoink([](std::string *s) {
        return s && s->length() > 0 && (byte)(*s)[0] == 173;  // T_DISK_READ
      });
      if (!pkt) coro_yield(&self);  // Let drain task run while we wait
    }

    // Cleverly avoiding the initial metadata
    // (which really should be checked, but our
    // read requests and consumption are currently
    // synchronous)
    // and using just the last 256 bytes.
    if (pkt->length() >= 256) {
      for (int i = 0; i < 256; i++) {
        floppy_buf[i] = (*pkt)[pkt->length() - 256 + i];
      }
    } else {
      T::Fatal("pkt too short", pkt->length());
    }
    delete pkt;
  }

  static void BackgroundFifoFloppyLatch(byte chore_byte) {
    static byte last_latch;
    if (chore_byte != last_latch) {
      cobs_printf(" _%02x ", chore_byte);
      last_latch = chore_byte;
    }
  }

  static void BackgroundFifoFloppyCommand(Coro &self, uint chore,
                                          byte chore_byte) {
    cobs_printf(" f!%02x ", chore_byte);
    switch (chore_byte) {
      case 0x17:  // seek track
        floppy_track = floppy_buf[0];
        break;

      case 0x80:  // read sector
        cobs_printf(" %dr%d/%x", floppy_track, floppy_sector, chore_byte);
        {
          unsigned char pkt[6] = {C_DISK_READ,  'f',          chore_byte,
                                  floppy_latch, floppy_track, floppy_sector};
          CobsEncodeAndTransmit(pkt, 6, putchar_raw);
        }

        ReceiveSectorData(self);
        floppy_ptr = floppy_buf;
        // release: ensures all floppy_buf[] writes are visible to
        // foreground before it sees DRQ via acquire load.
        floppy_status.store(0x02, std::memory_order_release);

        cobs_printf(" ");
        break;

      case 0xA0:  // write sector
        cobs_printf(" %dw%d/%x", floppy_track, floppy_sector, chore_byte);
        {
          unsigned char pkt[6] = {C_DISK_WRITE, 'f',          chore_byte,
                                  floppy_latch, floppy_track, floppy_sector};
          CobsEncodeAndTransmit(pkt, 6, putchar_raw);
        }

        floppy_ptr = floppy_buf;

        break;
    }
  }
  static void BackgroundFifoFloppyW256() {
    SendSectorData();
    floppy_ptr = floppy_buf;

    cobs_printf(" [sent] ");
  }

  static void ReadScsFloppy(const uint &abus, byte &dbus) {
    // SAY('S');
    // CASE special read SCS
    switch (abus & 15) {
      case 0x8:  // ReadStatus
        // acquire: ensures floppy_buf writes are visible if DRQ is set
        dbus = floppy_status.load(std::memory_order_acquire);
        floppy_status.store(
            dbus & 1, std::memory_order_relaxed);  // Clear all except BUSY
        break;
      case 0xB:  // ReadData
        dbus = *floppy_ptr++;
        if ((floppy_latch & 0x80) != 0 && floppy_ptr >= floppy_limit) {
          floppy_ptr = floppy_buf;
          // Assert NMI directly from foreground — don't route through
          // fg2bg FIFO which adds latency at SLOW_SPEED.
          ASSERT_NMI();
          PUSH_TO_BG(FG2BG_NMI, 0, 0);  // For background to log + release
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
        PUSH_TO_BG(FG2BG_FLOPPY_LATCH, 0, dbus);
        break;
      case 0x8:  // WriteCommand
        // Set BUSY only (0x01) — NOT DRQ yet.
        // Background will set DRQ (0x02) after ReceiveSectorData loads data.
        floppy_status.store(((dbus & 0xF0) == 0x80) || ((dbus & 0xF0) == 0xA0)
                                ? 0x01  // BUSY, no DRQ until data is loaded
                                : 0x00,
                            std::memory_order_relaxed);

        floppy_ptr = floppy_buf;  // Reset pointer.
        if (dbus == 0x17)
          floppy_track = floppy_buf[0];  // was losing critical race

        PUSH_TO_BG(FG2BG_FLOPPY_COMMAND, 0, dbus);
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
          PUSH_TO_BG(FG2BG_W_256, 0, 0);
          // Assert NMI directly from foreground.
          ASSERT_NMI();
          PUSH_TO_BG(FG2BG_NMI, 0, 0);  // For background to log + release
        }
        break;
      default:
        break;
    }
  }
};

#endif  // CENTIPEDE_FIRMWARE_FLOPPY_H_
