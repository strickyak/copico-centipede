# Proto Crawl Buffers

Proto Crawl Buffers (PCB) is a lightweight, simplified binary serialization format inspired by Google Protocol Buffers. Designed to be extremely minimal, it uses a single-byte tag system and relies on a `0` terminator for messages rather than length prefixes.

## 1. Schema Definition Language
The schema format uses `.proto`-like syntax, but it's drastically simplified.

**Syntax Rules:**
- Defines a message with the keyword `message` followed by the message name.
- Fields are defined as `[repeated] <type> <name> = <field_num>`.
- Available types are limited to `int`, `str`, or the name of another `message`.
- `#` begins a line comment.

**Example Schema (`bc.proto`):**
```protobuf
message CodePack
  str bytecode = 1
  repeated InternPack interns = 2

message InternPack
  str s = 11
  repeated int patch = 12
```

## 2. Wire Format Fundamentals
Data is encoded strictly sequentially as a series of fields. Each field starts with a **1-byte tag**, followed by the encoded value. 

### The Tag Byte
The tag is a single 8-bit byte composed of the **field number** (5 bits) and the **kind** (3 bits). 
```c
tag_byte = (field_number << 3) | kind
```

Because the tag is strictly one byte:
- **Field numbers** are restricted to the range `1` to `29`.
- **Kinds** are restricted to `1`, `2`, and `3`. 
- Different types can technically reuse the same field number as long as their "kind" is different (since the wire tag will differ).

### End of Message Terminator
A tag byte of `0x00` (which implies field `0`, kind `0`) serves a special purpose: it **marks the end of a message** (or field list). Because nested messages are not length-prefixed, parsers rely on this `0x00` terminator to know when to exit a nested message context.

The rationale for using a `0x00` terminator, rather than a length at the front of a message, is that on 8-bit platforms
messages may not fit in memory buffers.  Messages can be marshalled and consumed directly between I/O streams
and the actual objects they represent, without ever sitting in a RAM buffer at once.

## 3. Data Kinds and Encoding
There are only three "kinds" of fields on the wire.

### Kind 1: `int` (Integer)
- **Encoding**: Standard Base-128 VarInt (LEB128). The most significant bit (MSB) of each byte indicates if there are further bytes in the integer. The remaining 7 bits contribute to the value, stored little-endian.
- **Size and Sign**: The size is unspecified (but guaranteed at least 16-bits). Implementations typically use the native word size of the platform. Negative numbers are sent as standard two's complement and are not distinguishable on the wire from unsigned integers.

### Kind 2: `str` (String or Byte Array)
- **Encoding**: A VarInt (identical to Kind 1 encoding) representing the length of the string in bytes, immediately followed by the raw string bytes. 

### Kind 3: `message` (Embedded Message)
- **Encoding**: No length prefix is specified. Instead, the `message` tag is immediately followed by the encoded fields of the child message. The child message is terminated when a tag byte of `0x00` is encountered.

## 4. Missing and Repeated Fields

- **Missing Fields**: If a field is omitted during serialization, it will not appear on the wire. Parsers should default non-repeated fields to `0` (for ints) or empty (for strings/messages) when missing.
- **Repeated Fields**: If a field is marked as `repeated` in the schema, it can appear multiple times on the wire. Its encoded values simply appear sequentially in the data stream using the identical tag byte. (There is no concept of "packed" arrays).
