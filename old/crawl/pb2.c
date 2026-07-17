// C functions to decode our protobufs.

#include "pb2.h"

#include "octet.h"

word pb_get_varint(struct ReadBuf* bp) {
  word z = (pb_current(bp) & 0x7F);
  byte shift = 0;
  while ((pb_current(bp) & 0x80) == 0x80) {
    shift += 7;
    z = z | ((word)(pb_next(bp) & 0x7F) << shift);
  }
  pb_next(bp);
  return z;
}

word pb_int(struct ReadBuf* bp) {
  assert1((pb_current(bp) & 7) == KIND_INT, "bad pb_int kind %d",
          pb_current(bp) & 7);
  pb_next(bp);
  return pb_get_varint(bp);
}

word pb_str(struct ReadBuf* bp, byte cls, byte* len_out) {
  assert1((pb_current(bp) & 7) == KIND_STR, "bad pb_str kind %d",
          pb_current(bp) & 7);
  pb_next(bp);
  word len_int = pb_get_varint(bp);
  assert(len_int < (word)INF);
  byte len = (byte)len_int;
  word z = oalloc(len, cls);
  byte x = pb_current(bp);
  for (byte i = 0; i < len; i++) {
    oputb(z + i, x);
    x = pb_next(bp);
  }
  if (len_out) *len_out = len;
  return z;
}

void pb_skip(struct ReadBuf* bp) {
  switch (pb_current(bp)) {
    case KIND_TAG:
      pb_next(bp);
      break;
    case KIND_INT: {
      pb_int(bp);
    } break;
    case KIND_STR: {
      byte len = (byte)pb_get_varint(bp);
      for (byte i = 0; i < len; i++) {
        pb_next(bp);
      }
    } break;
    case KIND_MESSAGE: {
      pb_next(bp);
      while (pb_current(bp)) {
        pb_skip(bp);
      }
      pb_next(bp);
    } break;
    default:
      assert1(0, "bad kind %d", pb_current(bp));
  }
}
