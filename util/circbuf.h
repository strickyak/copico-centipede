#ifndef _UTIL_CIRCBUF_H_
#define _UTIL_CIRCBUF_H_

using byte = unsigned char;
using uint = unsigned int;

template <typename T, uint N>
class CircBuf {
 private:
  T buf[N];
  uint nextIn, nextOut;

 public:
  void Reset() { nextIn = nextOut = 0; }

  CircBuf() { Reset(); }

  uint NumBuffered() {
    if (nextOut <= nextIn) {
      return nextIn - nextOut;
    } else {
      return N + nextIn - nextOut;
    }
  }

  T Take() {
    T z = buf[nextOut];
    ++nextOut;
    if (nextOut == N) nextOut = 0;
    return z;
  }

  void Put(T x) {
    buf[nextIn] = x;
    ++nextIn;
    if (nextIn == N) nextIn = 0;
  }
};
#endif // _UTIL_CIRCBUF_H_
