// C functions to decode our protobufs.

#ifndef PYTHONINE_PB2_H_
#define PYTHONINE_PB2_H_

#include "octet.h"
#include "readbuf.h"

// The low 3 bits in a ttag (the upper 5 bits
// will be the tag number defined in the .proto file).
enum PbKindTag {
  KIND_TAG = 0,
  KIND_INT = 1,
  KIND_STR = 2,
  KIND_MESSAGE = 3,
};

#if 1
#define pb_current ReadBufCurrent
#define pb_next ReadBufNext
#else
extern byte pb_current(struct ReadBuf*);
extern byte pb_next(struct ReadBuf*);
#endif

// Checks the tag, returns the int.
extern word pb_int(struct ReadBuf*);
// Checks the tag, uses cls, sets *len, and returns str.
extern word pb_str(struct ReadBuf*, byte cls, byte* len_out);
// Checks the tag, sets *message to the next address, and advances over the
// message.
extern void pb_message(struct ReadBuf*, word* message);
// Advances over the current tag.
extern void pb_skip(struct ReadBuf*);
// Reads a varint, least significant bits first,
// reading more bits while high bit is set.
extern word pb_get_varint(struct ReadBuf*);

#define BYTES_CLASS 1

#endif
