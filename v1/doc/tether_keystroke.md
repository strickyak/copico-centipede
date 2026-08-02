# Tether Keystroke Flow

How a keystroke travels from the Linux terminal through the tether
program, over USB, through the firmware's COBS pipeline, and into the
Tcl interpreter (gspoon.h) or editor (editor.h).

## Keycode Convention (Aug 2, 2026)

Documented at the top of `console.h`.

### ASCII control codes

| Code | Meaning | CoCo key | Tether key |
|------|---------|----------|------------|
| 8 | BS (destructive backspace) | Left arrow | Backspace |
| 9 | TAB | Right arrow | Tab |
| 13 | CR (Enter) | Enter | Enter |
| 27 | ESC | Break | ESC (timeout for ANSI prefix) |
| 127 | DEL | Shift-Break | (not distinguishable) |

### Extended keycodes (128+)

| Code | Meaning | CoCo key | Tether key |
|------|---------|----------|------------|
| 128 | Up | Up arrow | ↑ |
| 129 | Down | Down arrow | ↓ |
| 130 | Cursor Left (non-destructive) | Shift-Left | ← (any) |
| 131 | Cursor Right (non-destructive) | Shift-Right | → (any) |
| 132 | Page Up | Shift-Up | PgUp or Shift-↑ |
| 133 | Page Down | Shift-Down | PgDn or Shift-↓ |

The tether has dedicated Backspace and Tab keys, so Left/Right arrows
always produce 130/131 (non-destructive cursor motion) regardless of
shift state.

The CoCo Left/Right arrows produce 8/9 (BS/TAB) because the original
CoCo2 has no Backspace or Tab keys.  Shift-Left/Right produce 130/131
for non-destructive cursor motion.

---

## 1. Linux Terminal → InkeyRoutine (tether)

**File:** `v1/tether/tconsole.go`, func `InkeyRoutine`

The terminal is set to `cbreak` mode by `SetSttyCbreak()`.
A single persistent goroutine reads bytes from `os.Stdin` into
a buffered `rawCh` channel (capacity 16).

### Translations

- DEL (127) → BS (8)
- LF (10) → CR (13) — Linux terminals send LF for Enter

### ANSI escape sequence parser

The main loop selects between `rawCh` and an ESC timeout timer
(100ms).  When ESC (27) arrives:

1. Start collecting into `escBuf`, arm 100ms timer.
2. If next byte is `[` or `O`, continue collecting.
3. When a letter (`A`–`Z`, `a`–`z`) or `~` arrives, match the
   complete sequence and emit a single keycode:

| ANSI sequence | Keycode | Meaning |
|---------------|---------|---------|
| `ESC[A`, `ESC OA` | 128 | Up |
| `ESC[B`, `ESC OB` | 129 | Down |
| `ESC[C`, `ESC OC` | 131 | Cursor Right |
| `ESC[D`, `ESC OD` | 130 | Cursor Left |
| `ESC[1;2A` | 132 | Page Up (Shift-Up) |
| `ESC[1;2B` | 133 | Page Down (Shift-Down) |
| `ESC[1;2C` | 131 | Cursor Right (same) |
| `ESC[1;2D` | 130 | Cursor Left (same) |
| `ESC[5~` | 132 | Page Up |
| `ESC[6~` | 133 | Page Down |

4. If the timer fires before the sequence completes, send bare
   ESC (27) and process any buffered bytes normally.
5. Unknown sequences are silently discarded.

### Tether command interception

If the user types `~` at the start of a line, `InkeyRoutine`
enters command mode and collects the line locally (not sent to
firmware).  `~~` sends a literal `~`.

---

## 2. InkeyRoutine → RunSelect (tether)

**File:** `v1/tether/tconsole.go`

`InkeyRoutine` sends each translated byte to `inkey chan byte`.

`RunSelect` receives on `inkey` in a `select` loop.
For each byte where `inchar >= 1`:

```go
WriteBytes(channelToPico, inchar)
```

The byte range filter accepts 1–255, allowing the extended
keycodes (128–133) to pass through.

`WriteBytes` sends `[]byte{ch}` to `channelToPico`, which is
`activeSerial.In` (a `chan []byte`).

---

## 3. WriteBytes → COBS Encode → Serial Port (tether)

**File:** `v1/tether/tconsole.go`, `ActiveSerial.loop`

The WRITER loop receives each `[]byte` from `uc.In`:

```go
case packet := <-uc.In:
    encoded := cobs.Encode(packet)
    encoded = append(encoded, 0x00)
    serialPort.Write(encoded)
```

### COBS Encoding (with checksums)

**File:** `v1/tether/cobs/encoder.go`

`cobs.Encode()` checks `cobs.UseChecksums` (default: `true`).
When true, it appends a checksum byte to the payload before COBS
encoding:

```
payload = data + Checksum(data)
```

**Checksum** (`cobs/checksum.go`): one's complement of the
modulo-256 sum: `^sum & 0xFF`.  When appended, the sum of all
bytes (data + checksum) equals `0xFF`.

**Example:** Keystroke `'A'` (0x41)

1. Raw payload: `[0x41]`
2. Checksum: `~0x41 = 0xBE`
3. Payload with checksum: `[0x41, 0xBE]`
4. COBS encode: `[0x03, 0x41, 0xBE]` (code=3: two data bytes)
5. Append frame delimiter: `[0x03, 0x41, 0xBE, 0x00]`

---

## 4. Serial Port → USB CDC → UsbReceiver (firmware)

**File:** `v1/firmware/usb_pipeline.h`, class `UsbReceiver`

The firmware reads raw bytes from the USB CDC interface via
`stdio_usb_in_chars()` into a temporary buffer, then pushes
them into `usb_raw_buf` (a `CircBuf<unsigned char, 1024>`).

`UsbReceiver::Tick()` is called by `PumpUsbCobs()`.

---

## 5. UsbReceiver → CobsDecoder → usb_packet_buf (firmware)

**File:** `v1/util/cobs.h`, class `CobsDecoder<1024, 64>`

`CobsDecoder::Tick()` reads bytes from `usb_raw_buf` and
feeds them to `ProcessByte()`.

`ProcessByte` implements a streaming COBS decoder:
- `0x00` = frame delimiter → finalize current packet
- Otherwise: standard COBS code/data state machine

### Checksum verification

When `COBS_CHECKSUMS` is `1` (the default), after decoding:

```cpp
unsigned char sum = 0;
for (size_t i = 0; i < plen; i++)
    sum += (unsigned char)current_packet_[i];
if ((sum & 0xFF) == 0xFF) {
    current_packet_.pop_back();  // strip checksum
    out_buf_.Put(new std::string(current_packet_));
}
```

On success, a `new std::string` containing the decoded payload
(without checksum) is placed into `usb_packet_buf`
(`CircBuf<std::string*, 64>`).

### Where PumpUsbCobs is called

`PumpUsbCobs()` calls `usb_receiver.Tick()`, then
`cobs_decoder.Tick()`, then `RpcEvaluator::Tick()`.

It is called from:

- **Round-robin scheduler** (`centipede.cpp`):
  Between each coroutine resume (drain, floppy, spoon).
- **drain_task**: When the fg2bg FIFO is empty (idle moments).
- **ccfifo_pop_blocking**: While waiting for foreground data.
- **floppy.h**: During floppy I/O waits.
- **vfs_rpc.h**: While waiting for RPC responses.

The Tcl REPL and editor rely on the round-robin scheduler to
pump bytes.  When the `spoon` coroutine yields (every ~20ms
when idle), the scheduler calls `PumpUsbCobs`.

---

## 6. usb_packet_buf → tcl_io::poll_key → key byte (firmware)

**File:** `v1/firmware/tcl_io.h`, func `poll_key`

`poll_key` tries the CoCo2 keyboard first, then checks USB:

```cpp
std::string* pkt = usb_packet_buf.Yoink([](std::string* s) {
    return s && s->length() > 0 &&
           (unsigned char)(*s)[0] != T_RPC;
});
```

`Yoink` scans the circular buffer for the first packet whose
first byte is NOT `T_RPC` (180).  RPC responses are left in
the buffer for `RpcEvaluator::Tick()` to consume.

If found:

```cpp
unsigned char uch = (unsigned char)(*pkt)[0];
delete pkt;
return uch;
```

The returned byte is a keycode in the 1–255 range, using the
convention documented above.  No translation is performed in
`poll_key`; the tether handles LF→CR and DEL→BS before sending.

---

## 7. poll_key → REPL / Editor / Menu (firmware)

Four consumers call `poll_key`:

### REPL: BackgroundSpoonFeeder

**File:** `v1/firmware/gspoon.h`

The REPL line-input loop handles keycodes directly:

- 128 → history-up
- 129 → history-down
- 130 → cursor-left (non-destructive)
- 131 → cursor-right (non-destructive)
- 8 / 127 → backspace (destructive)
- 13 → Enter (submit line)
- 0x20–0x7F → printable character (inserted at cursor)

### Editor: editor_cmd

**File:** `v1/firmware/editor.h`

The editor handles keycodes directly:

- 128 → cursor up (visual line aware)
- 129 → cursor down (visual line aware)
- 130 → cursor left (wraps to previous line)
- 131 → cursor right (wraps to next line)
- 132 → page up (20 lines)
- 133 → page down (20 lines)
- 8 / 127 → backspace (destructive)
- 13 → Enter (insert newline)
- 9 / 0x20–0x7E → printable (insert at cursor)
- Ctrl-S (19) → save and quit
- Ctrl-Q (17) → quit without saving
- Ctrl-A (1) → beginning of line
- Ctrl-E (5) → end of line

### Menu: menu_cmd

**File:** `v1/firmware/menu.h`

- 128 → move to previous field
- 129 → move to next field
- Space → toggle checkbox
- Letters → hotkey matching
- 8 / 127 → backspace in text fields
- 0x20–0x7E → insert in text fields

### Tcl `inkey` command

**File:** `v1/firmware/gspoon.h`

The `inkey` Tcl command returns whatever `poll_key` returns
as a numeric value.  Applications interpret the keycode using
the same convention.

---

## USB Disconnect and Reconnect

The tether and firmware handle USB disconnect/reconnect
gracefully.  The Tcl session survives disconnection.

### Tether side

`ActiveSerial.loop()` runs in an outer retry loop:

1. `OpenSerial()` attempts to open the USB device.
2. If it fails → logs "UsbClosed", sleeps 200ms, retries.
3. When it succeeds → logs "UsbOpen", starts reader goroutine,
   sends `0x00` (COBS frame reset) + T_HELLO packet.
4. Writer loop runs until read/write error.
5. `serialPort.Close()` → back to step 1.

Keystrokes typed during disconnection are buffered in the
`channelToPico` channel and sent when the port reopens.

`InkeyRoutine` and `RunSelect` are independent of the serial
port lifecycle — they communicate through Go channels.

### Firmware side

The Pico's USB CDC stack handles re-enumeration automatically.
The COBS decoder receives the leading `0x00` from the tether's
reconnect sequence, which aborts any stale partial frame and
resyncs the decoder.  The T_HELLO packet (178) is consumed
harmlessly by `poll_key` (178 > 127, not printable, dropped).

The Tcl session keeps running throughout.  The CoCo keyboard
continues working during USB disconnection.

---

## Function Call Chain (keystroke path)

```
Linux Terminal
  │  raw bytes (cbreak mode)
  ▼
os.Stdin.Read()                         [tconsole.go]
  │  persistent goroutine → rawCh
  ▼
InkeyRoutine()                          [tconsole.go]
  │  ANSI parser (ESC[A → 128, etc.)
  │  LF(10) → CR(13)
  │  DEL(127) → BS(8)
  │  sends keycode to inkey channel
  ▼
RunSelect() select case inkey           [tconsole.go]
  │  filter: inchar >= 1
  ▼
WriteBytes(channelToPico, inchar)       [tconsole.go]
  │  sends []byte{inchar} to activeSerial.In
  ▼
ActiveSerial.loop() WRITER              [tconsole.go]
  │  cobs.Encode(packet)  [cobs/encoder.go]
  │    └─ appends Checksum(data)
  │    └─ encodeRaw(payload)
  │  appends 0x00 frame delimiter
  │  serialPort.Write(encoded)
  ▼
═══════════ USB Serial ═══════════
  ▼
stdio_usb_in_chars()                    [Pico SDK]
  │
  ▼
UsbReceiver::Tick()                     [usb_pipeline.h]
  │  raw bytes → usb_raw_buf
  ▼
CobsDecoder::Tick()                     [util/cobs.h]
  │  ProcessByte() state machine
  │  Checksum verification (sum==0xFF)
  │  strips checksum byte
  │  → usb_packet_buf
  ▼
tcl_io::poll_key()                      [tcl_io.h]
  │  CoCo2 keyboard checked first
  │  Yoink: first packet where [0] != T_RPC
  │  extracts [0] as keycode byte
  │  deletes string*
  ▼
BackgroundSpoonFeeder() / editor_cmd()
  / menu_cmd() / inkey                  [gspoon.h / editor.h / menu.h]
  │  direct keycode dispatch (128–133)
  │  no ESC state machine needed
  ▼
Tcl_Eval() or buffer modification
```

---

## Known Limitations

1. **T_HELLO consumed as keystroke**: On reconnect, the T_HELLO
   packet (178) is matched by `poll_key` (178 ≠ T_RPC) and
   returned as a "keystroke."  It's harmlessly dropped by all
   consumers (178 > 127, not printable), but is wasteful.

2. **T_COMMAND packets lost**: The `CommandEvaluator` is disabled
   (`#if 0`).  If a tether `~command` sends `{179, ...}`,
   `poll_key` consumes it as a keystroke, losing the payload.

3. **Shift-ESC indistinguishable on tether**: Linux terminals
   send the same byte 27 for ESC regardless of shift state.
   The web console could distinguish via JavaScript `shiftKey`
   but currently doesn't.
