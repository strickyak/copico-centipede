# USB Protocol between Firmware and Tether

Based on the source code in `firmware/centipede.cpp` and `tether/tconsole.go`, the two programs communicate over USB using a custom, packet-based binary protocol.

## Protocol Structure
The protocol categorizes byte messages into three main groups based on their byte value.

### 1. Raw Data / Keystrokes (Bytes 1 - 127)
Values from 1 to 127 are treated as raw data.
- **From PC to Pico:** Typed ASCII keystrokes are sent directly without any wrapping command packet.
- **From Pico to PC:** These are interpreted as character output to the console.

### 2. Long Form Codes (Commands 128 - 191)
These commands use a variable-length encoding to explicitly specify the payload length. The size is denoted by the byte(s) immediately following the command byte.
- **1-byte Size:** If the byte following the command is between `128` and `191` (binary `10xxxxxx`), the size is contained in the lower 6 bits (i.e. `length_byte & 0x3F`). This supports payload lengths from 0 to 63 bytes.
- **2-byte Size:** If the byte following the command is between `192` and `255` (binary `11xxxxxx`), the size spans two bytes. The length is calculated using the lower 6 bits of both bytes: `size = ((byte1 & 0x3F) * 64) + (byte2 & 0x3F)`. This supports payload lengths up to 4095 bytes.
- **Format:** `[Command Byte] [Length Byte(s)] [Payload Data...]`

### 3. Short Form Codes (Commands 192 - 255)
These commands have an implicit length. To save bandwidth on frequent commands, the **length of the payload is encoded directly into the low nybble (lower 4 bits)** of the command byte itself (`Command & 0x0F`).
- **Format:** `[Command Byte] [Payload Data...]` (where the payload is exactly `Command & 0x0F` bytes long).

---

## Messages Sent

### From Pico to PC (Tether)
The Pico acts as the primary sender of debugging and state information to the PC.

| Command | Value | Type | Length | Description |
|---|---|---|---|---|
| `C_PUTCHAR` | 193 | Short | 1 byte | Sends a single character to be printed to the console. Payload: `[Data]`. |
| `C_LOGGING` | 130-139 | Long | Variable | Sends a string to be logged at varying log levels. |
| `C_CYCLE` | 200 | Short | 8 bytes | Details a single machine cycle. Payload: `[4 bytes cycle count] [1 byte flags/kind] [1 byte data] [2 bytes address]`. |
| `C_CYCLE_RD3` | 211 | Short | 3 bytes | Used by the Centipede firmware for a read cycle. Payload: `[Address Hi] [Address Lo] [Data]`. |
| `C_RAM2_WRITE`| 195 | Short | 3 bytes | Tells the PC that a 16-bit address memory write occurred. Payload: `[Address Hi] [Address Lo] [Data]`. |
| `C_RAM5_WRITE`| 198 | Short | 6 bytes | Used to denote memory writes with physical and logical mapping data. |
| `C_DISK_READ` | 173 | Long | Variable | A request to read a sector from a floppy disk image. Payload: `[f] [cmd] [latch] [track] [sector]`. |
| `C_DISK_WRITE`| 174 | Long | Variable | A request to write data to a disk image. The PC expects to read `size` bytes of sector data. |
| `C_RAM_CONFIG`| 164 | Long | Variable | Informs the PC of the Pico's architecture mode (e.g., Coco1, Coco3). |
| `C_DUMP_RAM` | 167 | Long | Variable | (And related `C_DUMP_LINE`, `C_DUMP_PHYS`) Used for streaming larger blocks of memory back to the PC. |

### From PC (Tether) to Pico
The PC predominantly listens, but responds to disk commands, memory load requests, and user keyboard inputs.

| Command | Value | Type | Description |
|---|---|---|---|
| **Keystrokes** | 1-127 | Raw | User keyboard inputs sent directly to the Pico. |
| `C_REBOOT` | 192 | Short | (Length 0) Sent by the PC to force a reboot on the Pico (triggered by pressing Control-Underscore `^_`). Sent in bursts. |
| `C_PRE_LOAD` | 163 | Long | Used by the PC to poke pre-loaded memory bytes into the Pico. |

#### Special Disk Response
Upon receiving a `C_DISK_READ` request from the Pico, the PC does not respond with a typical command code. Instead, it blasts raw disk data to the Pico. The Pico expects a specific synchronization byte `0xAD` followed by exactly 256 bytes of the requested disk sector.
