#ifndef _FIRMWARE_PIO_USB_PIPELINE_H_
#define _FIRMWARE_PIO_USB_PIPELINE_H_

#include <string>
#include "../util/circbuf.h"
#include "../util/cobs.h"

extern "C" {
    extern int stdio_usb_in_chars(char* buf, int length);
}

extern CircBuf<unsigned char, 1024> usb_raw_buf;
extern CircBuf<std::string*, 64> usb_packet_buf;

class UsbReceiver {
  CircBuf<unsigned char, 1024>& buf_;
 public:
  UsbReceiver(CircBuf<unsigned char, 1024>& buf) : buf_(buf) {}
  void Tick() {
    int free = 1023 - buf_.NumBuffered(); // Capacity is N-1 = 1023
    if (free > 0) {
      char temp[64];
      int to_read = free < 64 ? free : 64;
      int rc = stdio_usb_in_chars(temp, to_read);
      if (rc > 0) {
        for (int i = 0; i < rc; i++) {
          buf_.Put(temp[i]);
        }
      }
    }
  }
};



extern UsbReceiver usb_receiver;
extern CobsDecoder<1024, 64> cobs_decoder;

inline void PumpUsbCobs() {
    usb_receiver.Tick();
    cobs_decoder.Tick();
}

#endif // _FIRMWARE_PIO_USB_PIPELINE_H_
