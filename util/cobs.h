#ifndef _UTIL_COBS_H_
#define _UTIL_COBS_H_

#include <string>
#include "circbuf.h"

template <uint IN_BUF_LEN, uint OUT_BUF_LEN>
class CobsDecoder {
 private:
  CircBuf<unsigned char, IN_BUF_LEN>& in_buf_;
  CircBuf<std::string*, OUT_BUF_LEN>& out_buf_;

  std::string current_packet_;
  int code_;
  int copy_len_;
  bool expecting_code_;

  void ProcessByte(unsigned char b) {
    if (b == 0) {
      // 0x00 is the frame delimiter.
      if (expecting_code_ && !current_packet_.empty()) {
        // We reached the end of the packet cleanly.
        if (code_ < 0xFF) {
          // The last block appended an implicit zero, but since it's the end 
          // of the packet, this zero should not be part of the payload.
          current_packet_.pop_back();
        }
        
        // If the packet has data, emit it.
        if (!current_packet_.empty()) {
          out_buf_.Put(new std::string(current_packet_));
        }
      }
      
      // If expecting_code_ was false, we hit 0x00 prematurely in the middle 
      // of copying data (a framing error). The packet is discarded cleanly.
      
      // Reset state for the next packet.
      current_packet_.clear();
      expecting_code_ = true;
      code_ = 0;
      copy_len_ = 0;
    } else {
      if (expecting_code_) {
        code_ = b;
        copy_len_ = code_ - 1;
        expecting_code_ = (copy_len_ == 0);
      } else {
        current_packet_.push_back(b);
        copy_len_--;
        if (copy_len_ == 0) {
          expecting_code_ = true;
        }
      }
      
      // If we just finished a block (either copy_len became 0, or was 0 to begin with)
      if (expecting_code_) {
        if (code_ < 0xFF) {
          current_packet_.push_back(0);
        }
      }
    }
  }

 public:
  CobsDecoder(CircBuf<unsigned char, IN_BUF_LEN>& in_buf,
              CircBuf<std::string*, OUT_BUF_LEN>& out_buf)
      : in_buf_(in_buf),
        out_buf_(out_buf),
        code_(0),
        copy_len_(0),
        expecting_code_(true) {}

  void Tick() {
    while (in_buf_.NumBuffered() > 0) {
      // Apply backpressure if the output buffer is full.
      // For a CircBuf of size OUT_BUF_LEN, the maximum capacity is OUT_BUF_LEN-1.
      if (out_buf_.NumBuffered() >= OUT_BUF_LEN-1) {
        break; // Stop taking input to avoid dropping packets.
      }
      
      unsigned char b = in_buf_.Take();
      ProcessByte(b);
    }
  }
};

#endif // _UTIL_COBS_H_
