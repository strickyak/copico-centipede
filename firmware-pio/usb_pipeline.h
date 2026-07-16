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
extern CircBuf<unsigned char, 1024> usb_stream_buf;

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

class PacketUnpacker {
  CircBuf<std::string*, 64>& in_buf_;
  CircBuf<unsigned char, 1024>& out_buf_;
  std::string* current_str = nullptr;
  size_t current_idx = 0;
 public:
  PacketUnpacker(CircBuf<std::string*, 64>& in_buf, CircBuf<unsigned char, 1024>& out_buf)
      : in_buf_(in_buf), out_buf_(out_buf) {}
  
  void Tick() {
    if (!current_str) {
      if (in_buf_.NumBuffered() > 0) {
        current_str = in_buf_.Take();
        current_idx = 0;
#if 0        
        printf("PKT rx %d: ", (int)current_str->length());
        for (size_t i = 0; i < current_str->length(); i++) {
          printf("%02x ", (unsigned char)(*current_str)[i]);
        }
        printf("\n");
#endif
      }
    }

    if (current_str) {
      while (current_idx < current_str->length() && out_buf_.NumBuffered() < 1023) {
        out_buf_.Put((*current_str)[current_idx++]);
      }
      if (current_idx == current_str->length()) {
        delete current_str;
        current_str = nullptr;
      }
    }
  }
};

extern UsbReceiver usb_receiver;
extern CobsDecoder<1024, 64> cobs_decoder;
extern PacketUnpacker packet_unpacker;

inline void PumpUsbCobs() {
    usb_receiver.Tick();
    cobs_decoder.Tick();
    packet_unpacker.Tick();
}

inline void ReadUsbStream(char* p, int needed) {
    while (needed > 0) {
        PumpUsbCobs();
        if (usb_stream_buf.NumBuffered() > 0) {
            *p++ = usb_stream_buf.Take();
            needed--;
        }
    }
}

#endif // _FIRMWARE_PIO_USB_PIPELINE_H_
