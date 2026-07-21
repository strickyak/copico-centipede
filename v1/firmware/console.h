#ifndef FIRMWARE_CONSOLE_H_
#define FIRMWARE_CONSOLE_H_

#include "pico/stdlib.h"

namespace console {

// External functions to access the Coco2 PIA0.
// The system using this utility should provide these.
extern unsigned char peek(unsigned short addr);
extern void poke(unsigned short addr, unsigned char val);

#define PIA0_PORT_A 0xFF00
#define PIA0_PORT_B 0xFF02

const unsigned char unshifted_map[8][7] = {
    // PA0  PA1  PA2  PA3  PA4  PA5  PA6
    {'@', 'h', 'p', 'x', '0', '8', 13},  // PB0 (13 = Enter)
    {'a', 'i', 'q', 'y', '1', '9', 12},  // PB1 (12 = Clear)
    {'b', 'j', 'r', 'z', '2', ':', 27},  // PB2 (27 = Break)
    {'c', 'k', 's', 11, '3', ';', 0},    // PB3 (11 = Up)
    {'d', 'l', 't', 10, '4', ',', 0},    // PB4 (10 = Down)
    {'e', 'm', 'u', 8, '5', '-', 0},     // PB5 (8 = Left)
    {'f', 'n', 'v', 9, '6', '.', 0},     // PB6 (9 = Right)
    {'g', 'o', 'w', ' ', '7', '/', 0}  // PB7 (PA6 is Shift, handled separately)
};

const unsigned char shifted_map[8][7] = {
    // PA0  PA1  PA2  PA3  PA4  PA5  PA6
    {'@', 'H', 'P', 'X', '_', '(', 13 | 0x80},  // PB0 (Shift+Enter)
    {'A', 'I', 'Q', 'Y', '!', ')', 12 | 0x80},  // PB1 (Shift+Clear)
    {'B', 'J', 'R', 'Z', '"', '*', 27 | 0x80},  // PB2 (Shift+Break)
    {'C', 'K', 'S', 11, '#', '+', 0},           // PB3
    {'D', 'L', 'T', 10, '$', '<', 0},           // PB4
    {'E', 'M', 'U', 8, '%', '=', 0},            // PB5
    {'F', 'N', 'V', 9, '&', '>', 0},            // PB6
    {'G', 'O', 'W', ' ' | 0x80, '\'', '?', 0}   // PB7 (Shift+Space)
};

// State structure to remember previous scans for debouncing
struct inkey_state {
  unsigned char prev_pressed[8];
  unsigned char debounced_pressed[8];
};

unsigned char Coco2Inkey(struct inkey_state* state) {
  unsigned char curr_pressed_all[8];

  // Scan the matrix
  for (int col = 0; col < 8; col++) {
    // Drive one column low, rest high
    poke(PIA0_PORT_B, (unsigned char)~(1 << col));

    // Read Port A and invert so 1 = pressed, 0 = released. Mask 7 rows.
    curr_pressed_all[col] = ~peek(PIA0_PORT_A) & 0x7F;
  }

  // Check for shift key (PB7, PA6)
  // If bit 6 of curr_pressed_all[7] is 1, shift is pressed.
  int shift_pressed = (curr_pressed_all[7] & (1 << 6)) != 0;

  unsigned char returned_char = 0;

  // Debounce and detect newly pressed keys
  for (int col = 0; col < 8; col++) {
    unsigned char curr = curr_pressed_all[col];
    unsigned char prev = state->prev_pressed[col];

    // Key is debounced if it is pressed in both current and previous scan
    unsigned char debounced = curr & prev;

    // Find which keys transitioned to debounced state
    unsigned char triggered = debounced & ~state->debounced_pressed[col];

    // Update the debounced matrix:
    // Set newly debounced keys, clear keys that are physically released.
    state->debounced_pressed[col] |= debounced;
    state->debounced_pressed[col] &= curr;

    // Remember current physical state for next scan's debounce
    state->prev_pressed[col] = curr;

    // If we haven't found a character to return yet, check triggered keys
    if (returned_char == 0 && triggered != 0) {
      for (int row = 0; row < 7; row++) {
        if (triggered & (1 << row)) {
          // Ignore the shift key itself as a character
          if (col == 7 && row == 6) continue;

          if (shift_pressed) {
            returned_char = shifted_map[col][row];
          } else {
            returned_char = unshifted_map[col][row];
          }
        }
      }
    }
  }

  // De-select all columns when done scanning to leave matrix in a clean state
  poke(PIA0_PORT_B, 0xFF);

  return returned_char;
}

inline unsigned short cursor = 0x400;

inline void putchar(unsigned char ascii) {
  if (ascii == '\r' || ascii == '\n') {
    cursor = (cursor & ~0x1F) + 0x20;
  } else if (ascii == 8 || ascii == 127) {  // backspace
    if (cursor > 0x400) {
      cursor--;
      poke(cursor, 0x20);  // space
    }
  } else if (ascii >= 0x20 && ascii <= 0x5F) {
    poke(cursor, ascii & 0x3F);
    cursor++;
  } else if (ascii >= 0x60 && ascii <= 0x7F) {  // lower case
    poke(cursor, (ascii & ~0x20) & 0x3F);
    cursor++;
  } else if (ascii >= 0x80) {
    poke(cursor, ascii);
    cursor++;
  }

  // Handle scrolling
  if (cursor >= 0x600) {
    for (unsigned short i = 0x400; i < 0x5E0; i++) {
      poke(i, peek(i + 0x20));
    }
    for (unsigned short i = 0x5E0; i < 0x600; i++) {
      poke(i, 0x20);
    }
    cursor = 0x5E0;
  }
}

}  // namespace console

#endif  // FIRMWARE_CONSOLE_H_
