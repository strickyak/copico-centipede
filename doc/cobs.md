# Constant Overhead Byte Stuffing (COBS)

User:

So the Stream Producer can produce data as "packets of one or more bytes" and write these packets to the COBS encoder, whose output goes across a serial one to a COBS decoder, that outputs "packets of one or more bytes" exactly corresponding to the original packets, divided into packets at exactly the same points.

Gemini:

Yes, exactly! You have perfectly summarized the primary purpose and beauty of COBS.

Serial lines are fundamentally just continuous pipes of bytes—they have no built-in concept of a "packet," "start," or "end."

By passing your data through a COBS encoder before it hits the serial line, you are essentially overlaying a highly robust packetization layer on top of that raw stream.

Because the 0x00 frame delimiter is mathematically guaranteed to never appear anywhere in the encoded payload, the decoder on the other side can blindly listen to the serial stream and slice it into packets every single time it sees a 0x00. When it decodes those slices, it perfectly reconstructs the exact lengths, exact byte sequences, and exact packet boundaries that the Stream Producer originally fed into the encoder.

This makes COBS incredibly popular for microcontrollers communicating over UART or RS-232, because if a wire gets unplugged and plugged back in (corrupting a transmission halfway through), the decoder simply waits for the next 0x00 frame delimiter, immediately regains synchronization, and flawlessly receives the very next packet.

## 1. Overview
Constant Overhead Byte Stuffing (COBS) is an algorithm for encoding a sequence of bytes into a form that contains no zero bytes (`0x00`). 

By ensuring that `0x00` never appears in the encoded payload, `0x00` can be used exclusively as a **frame delimiter**. This allows a receiver to reliably find the start and end of packets even if it loses synchronization, simply by scanning for a `0x00` byte.

## 2. Encoding Rules
The encoding process conceptually breaks the original data into blocks separated by `0x00` bytes. The blocks are at most 254 bytes long. Each block is prefixed by a single **code byte**.

1. **Code Byte (`C`)**: The code byte represents the distance to the next `0x00` byte (or the end of the packet), plus one. Mathematically, `C = distance + 1`.
2. **Value Range**: Because a block contains at most 254 data bytes, the `distance` is at most 254. Therefore, `C` strictly ranges from `1` to `255` (`0x01` to `0xFF`). The code byte `0x00` is never used.
3. **Block Types**:
   - **If `C < 255`:** The code byte is followed by exactly `C - 1` data bytes. The decoder will blindly copy these data bytes and then implicitly append a `0x00` byte.
   - **If `C == 255`:** The code byte is followed by exactly `254` data bytes. The decoder will blindly copy these data bytes, but it will **not** append an implicit `0x00` byte. (This handles continuous sequences of non-zero data longer than 254 bytes).

## 3. Framing
The entire encoded message is terminated by appending a literal `0x00` byte to the end of the stream. This serves as the frame delimiter indicating the packet is complete.

## 4. Decoding Rules
The decoder processes the encoded stream using a very simple state machine:

1. Read the next byte from the stream as the code byte `C`.
   - If `C == 0x00`, you have reached the frame delimiter. The packet is finished.
2. Read and output the next `C - 1` bytes exactly as literal data, without interpreting them.
3. If `C < 255`, append a `0x00` byte to the output.
4. Loop back to step 1.

## 5. Examples
*(Note: These examples show the encoded payload. On the wire, a `0x00` frame delimiter would be appended to the end of each encoded sequence.)*

### Example 1: No Zeros
**Original:** `[0x11, 0x22, 0x33, 0x44]`
* Distance to end is 4. Code byte `C = 4 + 1 = 5`.
**Encoded:** `[0x05, 0x11, 0x22, 0x33, 0x44]`

### Example 2: Contains Zeros
**Original:** `[0x11, 0x00, 0x22, 0x00]`
* Distance to first zero is 1. `C = 2`.
* Distance to second zero is 1. `C = 2`.
* Distance to end of packet is 0. `C = 1`.
**Encoded:** `[0x02, 0x11, 0x02, 0x22, 0x01]`

### Example 3: Starts and Ends with Zeros
**Original:** `[0x00, 0x11, 0x00]`
* Distance to first zero is 0. `C = 1`.
* Distance to second zero is 1. `C = 2`.
* Distance to end of packet is 0. `C = 1`.
**Encoded:** `[0x01, 0x02, 0x11, 0x01]`

### Example 4: Exactly 254 Non-Zero Bytes
**Original:** `[254 bytes of 0x11]`
* Distance is 254. `C = 255` (`0xFF`).
* Distance to end of packet is 0. `C = 1`.
**Encoded:** `[0xFF, (254 bytes of 0x11), 0x01]`

### Example 5: 255 Non-Zero Bytes
**Original:** `[255 bytes of 0x11]`
* Distance is > 254. Max block size is 254. `C = 255` (`0xFF`).
* Remaining distance is 1. `C = 2`.
**Encoded:** `[0xFF, (254 bytes of 0x11), 0x02, (1 byte of 0x11)]`
