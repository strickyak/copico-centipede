#ifndef FIRMWARE_PIO_COBS_TX_H_
#define FIRMWARE_PIO_COBS_TX_H_

#include <stdarg.h>
#include <stdio.h>

#include "../util/cobs.h"

// Command to wrap text output
#ifndef C_PUTCHAR
#define C_PUTCHAR 193
#endif

extern "C" {
extern int putchar_raw(int c);
}

inline void cobs_putchar(char c) {
  unsigned char pkt[2] = {C_PUTCHAR, (unsigned char)c};
  CobsEncodeAndTransmit(pkt, 2, [](int ch) { putchar_raw(ch); });
}

inline void cobs_printf(const char* fmt, ...) {
  char buf[256];
  buf[0] = C_PUTCHAR;
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(buf + 1, sizeof(buf) - 1, fmt, args);
  va_end(args);
  if (len > 0) {
    if (len >= sizeof(buf) - 1) len = sizeof(buf) - 2;
    CobsEncodeAndTransmit((const unsigned char*)buf, len + 1,
                          [](int ch) { putchar_raw(ch); });
  }
}

#endif  // FIRMWARE_PIO_COBS_TX_H_
