#ifndef FIRMWARE_CONSOLE_H_
#define FIRMWARE_CONSOLE_H_

#include <stdio.h>

#include "pico/stdlib.h"

namespace console {

void poke(unsigned short, unsigned char) {
    // TODO: send a BG2FG_POKE command to bg2fg
}
byte peek(unsigned short) {
    // TODO: send a BG2FG_PEEK command to bg2fg
    // Pump the background loop enough to get the reply on fg2bg
    byte reply = 0; // todo
    return reply; 
}

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

enum AnsiState { NORMAL, ESCAPE, CSI };
inline AnsiState ansi_state = NORMAL;
inline int csi_params[4];
inline int csi_param_count = 0;
inline unsigned short saved_cursor = 0x400;
inline bool inverse_video = false;

inline unsigned short cursor = 0x400;

inline void putchar(unsigned char ascii) {
  if (ansi_state == ESCAPE) {
    if (ascii == '[') {
      ansi_state = CSI;
      csi_param_count = 0;
      for (int i = 0; i < 4; i++) csi_params[i] = 0;
    } else if (ascii == '7') {
      saved_cursor = cursor;
      ansi_state = NORMAL;
    } else if (ascii == '8') {
      cursor = saved_cursor;
      ansi_state = NORMAL;
    } else {
      printf("Unsupported ESC sequence: ESC %c\n", ascii);
      ansi_state = NORMAL;
    }
    return;
  }

  if (ansi_state == CSI) {
    if (ascii >= '0' && ascii <= '9') {
      csi_params[csi_param_count] =
          csi_params[csi_param_count] * 10 + (ascii - '0');
    } else if (ascii == ';') {
      if (csi_param_count < 3) csi_param_count++;
    } else {
      int n = csi_params[0] == 0 ? 1 : csi_params[0];

      switch (ascii) {
        case 'H':
        case 'f': {
          int r = csi_params[0] == 0 ? 1 : csi_params[0];
          int c =
              (csi_param_count > 0 && csi_params[1] > 0) ? csi_params[1] : 1;
          if (r > 16) r = 16;
          if (c > 32) c = 32;
          cursor = 0x400 + (r - 1) * 32 + (c - 1);
          break;
        }
        case 'A':  // Up
          cursor = (cursor >= 0x400 + n * 32) ? cursor - n * 32
                                              : (cursor & 0x1F) + 0x400;
          break;
        case 'B':  // Down
          cursor = (cursor + n * 32 < 0x600) ? cursor + n * 32
                                             : (cursor & 0x1F) + 0x5E0;
          break;
        case 'C':  // Right
        {
          unsigned short current_row_start = cursor & ~0x1F;
          unsigned short max_c = current_row_start + 31;
          cursor += n;
          if (cursor > max_c) cursor = max_c;
        } break;
        case 'D':  // Left
        {
          unsigned short current_row_start = cursor & ~0x1F;
          if (cursor >= current_row_start + n) {
            cursor -= n;
          } else {
            cursor = current_row_start;
          }
        } break;
        case 's':
          saved_cursor = cursor;
          break;
        case 'u':
          cursor = saved_cursor;
          break;
        case 'm':
          for (int i = 0; i <= csi_param_count; i++) {
            if (csi_params[i] == 0)
              inverse_video = false;
            else if (csi_params[i] == 7)
              inverse_video = true;
          }
          break;
        case 'J':
          if (csi_params[0] == 2) {
            for (unsigned short i = 0x400; i < 0x600; i++) poke(i, 0x20);
            cursor = 0x400;
          }
          break;
        case 'K':
          if (csi_params[0] == 0) {  // Clear to end of line
            unsigned short eol = (cursor & ~0x1F) + 0x20;
            for (unsigned short i = cursor; i < eol; i++) poke(i, 0x20);
          } else if (csi_params[0] == 2) {  // Clear entire line
            unsigned short sol = cursor & ~0x1F;
            unsigned short eol = sol + 0x20;
            for (unsigned short i = sol; i < eol; i++) poke(i, 0x20);
          }
          break;
        default:
          printf("Unsupported CSI sequence: CSI ... %c\n", ascii);
          break;
      }
      ansi_state = NORMAL;
    }
    return;
  }

  if (ascii == 0x1B) {
    ansi_state = ESCAPE;
    return;
  }

  if (ascii == '\r' || ascii == '\n') {
    cursor = (cursor & ~0x1F) + 0x20;
  } else if (ascii == 8 || ascii == 127) {  // backspace
    if (cursor > 0x400) {
      cursor--;
      poke(cursor, 0x20);  // space
    }
  } else {
    unsigned char mapped = 0;
    if (ascii >= 0x20 && ascii <= 0x5F) {
      mapped = ascii & 0x3F;
    } else if (ascii >= 0x60 && ascii <= 0x7F) {  // lower case
      mapped = (ascii & ~0x20) & 0x3F;
    } else if (ascii >= 0x80) {
      mapped = ascii;
    } else {
      return;  // ignore non-printable control chars
    }

    if (inverse_video && mapped < 0x40) {
      mapped |= 0x40;  // Convert to CoCo inverse video
    }

    poke(cursor, mapped);
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
