#ifndef FIRMWARE_EDITOR_H_
#define FIRMWARE_EDITOR_H_

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "pico/stdlib.h"
#include "gspoon.h" // For tcl_io and inkey_state
#include "vfs.h"
#include "../tcl6.7c/tcl.h"

struct VisualLine {
  int logical_row;
  int start_col;
  int length;
  std::string text;
};

static void compute_layout(const std::vector<std::string>& lines, std::vector<VisualLine>& vlines) {
  vlines.clear();
  for (int r = 0; r < (int)lines.size(); r++) {
    const std::string& line = lines[r];
    if (line.empty()) {
      vlines.push_back({r, 0, 0, ""});
    } else {
      int len = line.length();
      int start = 0;
      while (start < len) {
        int chunk = len - start;
        if (chunk > 39) chunk = 39;
        vlines.push_back({r, start, chunk, line.substr(start, chunk)});
        start += chunk;
      }
    }
  }
}

static int editor_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char** argv) {
  if (argc != 2) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0], " fileName\"", (char*)NULL);
    return TCL_ERROR;
  }
  
  const char* filename = argv[1];
  std::vector<std::string> lines;
  
  // Read file
  struct vfs_info info;
  if (vfs_stat(filename, &info) == 0) {
    vfs_file_t file;
    if (vfs_file_open(&file, filename, LFS_O_RDONLY) == 0) {
      std::string current_line;
      char buf[64];
      while (true) {
        lfs_ssize_t n = vfs_file_read(&file, buf, sizeof(buf));
        if (n <= 0) break;
        for (lfs_ssize_t i = 0; i < n; i++) {
          if (buf[i] == '\n') {
            lines.push_back(current_line);
            current_line.clear();
          } else if (buf[i] != '\r') {
            current_line += buf[i];
          }
        }
      }
      if (!current_line.empty() || lines.empty()) {
        lines.push_back(current_line);
      }
      vfs_file_close(&file);
    }
  }
  
  if (lines.empty()) {
    lines.push_back("");
  }
  
  int edit_row = 0;
  int edit_col = 0;
  int scroll_vrow = 0;
  std::string message = "";
  
  console::inkey_state iks = {};
  
  std::vector<VisualLine> vlines;
  bool dirty = true;
  int ansi_state = 0;
  std::string ansi_buf = "";
  
  while (true) {
    if (dirty) {
      compute_layout(lines, vlines);
      
      // Find cursor visual position
      int cursor_vrow = 0;
      int cursor_vcol = 0;
      for (int i = 0; i < (int)vlines.size(); i++) {
        const VisualLine& vl = vlines[i];
        if (vl.logical_row == edit_row) {
          if (edit_col >= vl.start_col && edit_col <= vl.start_col + vl.length) {
            cursor_vrow = i;
            cursor_vcol = edit_col - vl.start_col;
            if (cursor_vcol == 39 && i + 1 < (int)vlines.size() && vlines[i+1].logical_row == edit_row) {
              continue; // Check the next chunk
            }
            break;
          }
        }
      }
      
      if (cursor_vrow < scroll_vrow) {
        scroll_vrow = cursor_vrow;
      } else if (cursor_vrow >= scroll_vrow + 22) {
        scroll_vrow = cursor_vrow - 21;
      }
      
      // Render
      tcl_io::emit_string("\x1b[H"); // Home
      for (int i = 0; i < 22; i++) {
        int vr = scroll_vrow + i;
        if (vr < (int)vlines.size()) {
          tcl_io::emit_string(vlines[vr].text.c_str());
        }
        tcl_io::emit_string("\x1b[K\r\n"); // Clear to end of line and newline
      }
      
      // Line 23: separator
      tcl_io::emit_string("---------------------------------------\x1b[K\r\n");
      // Line 24: message
      tcl_io::emit_string(message.c_str());
      tcl_io::emit_string("\x1b[K");
      
      // Set cursor
      char cbuf[32];
      snprintf(cbuf, sizeof(cbuf), "\x1b[%d;%df", (cursor_vrow - scroll_vrow) + 1, cursor_vcol + 1);
      tcl_io::emit_string(cbuf);
      
      dirty = false;
      message = "";
    }
    
    byte key = tcl_io::poll_key(&iks);
    if (key == 0) {
      sleep_ms(20);
      continue;
    }
    
    if (key == 19) { // Ctrl-S (Save and quit)
      vfs_file_t out;
      int err = vfs_file_open(&out, filename, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
      if (err < 0) {
        message = "Error: Could not open file for writing!";
        dirty = true;
        continue;
      }
      for (const std::string& ln : lines) {
        vfs_file_write(&out, ln.c_str(), ln.length());
        vfs_file_write(&out, "\n", 1);
      }
      vfs_file_close(&out);
      tcl_io::emit_string("\x1b[2J\x1b[H");
      return TCL_OK;
    }
    
    if (key == 17) { // Ctrl-Q (Quit)
      tcl_io::emit_string("\x1b[2J\x1b[H");
      return TCL_OK;
    }
    
    if (key == 1) { // Ctrl-A
      edit_col = 0;
      dirty = true;
      continue;
    }
    
    if (key == 5) { // Ctrl-E
      edit_col = lines[edit_row].length();
      dirty = true;
      continue;
    }
    
    if (key == 27) { // ESC (ANSI prefix)
      ansi_state = 1;
      ansi_buf = "";
      continue;
    }
    
    if (ansi_state > 0) {
      ansi_buf += (char)key;
      if ((key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z') || key == '~') {
        ansi_state = 0;
        if (ansi_buf == "[A") { // Up
          compute_layout(lines, vlines);
          int cursor_vrow = 0;
          int cursor_vcol = 0;
          for (int i = 0; i < (int)vlines.size(); i++) {
            const VisualLine& vl = vlines[i];
            if (vl.logical_row == edit_row) {
              if (edit_col >= vl.start_col && edit_col <= vl.start_col + vl.length) {
                cursor_vrow = i;
                cursor_vcol = edit_col - vl.start_col;
                if (cursor_vcol == 39 && i + 1 < (int)vlines.size() && vlines[i+1].logical_row == edit_row) continue;
                break;
              }
            }
          }
          if (cursor_vrow > 0) {
            cursor_vrow--;
            edit_row = vlines[cursor_vrow].logical_row;
            edit_col = vlines[cursor_vrow].start_col + cursor_vcol;
            if (edit_col > vlines[cursor_vrow].start_col + vlines[cursor_vrow].length) {
              edit_col = vlines[cursor_vrow].start_col + vlines[cursor_vrow].length;
            }
          }
          dirty = true;
        } else if (ansi_buf == "[B") { // Down
          compute_layout(lines, vlines);
          int cursor_vrow = 0;
          int cursor_vcol = 0;
          for (int i = 0; i < (int)vlines.size(); i++) {
            const VisualLine& vl = vlines[i];
            if (vl.logical_row == edit_row) {
              if (edit_col >= vl.start_col && edit_col <= vl.start_col + vl.length) {
                cursor_vrow = i;
                cursor_vcol = edit_col - vl.start_col;
                if (cursor_vcol == 39 && i + 1 < (int)vlines.size() && vlines[i+1].logical_row == edit_row) continue;
                break;
              }
            }
          }
          if (cursor_vrow < (int)vlines.size() - 1) {
            cursor_vrow++;
            edit_row = vlines[cursor_vrow].logical_row;
            edit_col = vlines[cursor_vrow].start_col + cursor_vcol;
            if (edit_col > vlines[cursor_vrow].start_col + vlines[cursor_vrow].length) {
              edit_col = vlines[cursor_vrow].start_col + vlines[cursor_vrow].length;
            }
          }
          dirty = true;
        } else if (ansi_buf == "[C") { // Right
          if (edit_col < (int)lines[edit_row].length()) {
            edit_col++;
          } else if (edit_row < (int)lines.size() - 1) {
            edit_row++;
            edit_col = 0;
          }
          dirty = true;
        } else if (ansi_buf == "[D") { // Left
          if (edit_col > 0) {
            edit_col--;
          } else if (edit_row > 0) {
            edit_row--;
            edit_col = lines[edit_row].length();
          }
          dirty = true;
        } else if (ansi_buf == "[1~" || ansi_buf == "[H" || ansi_buf == "OH") { // Home
          edit_row = 0;
          edit_col = 0;
          dirty = true;
        } else if (ansi_buf == "[4~" || ansi_buf == "[F" || ansi_buf == "OF") { // End
          edit_row = lines.size() - 1;
          edit_col = lines[edit_row].length();
          dirty = true;
        } else if (ansi_buf == "[5~") { // Page Up
          edit_row -= 20;
          if (edit_row < 0) edit_row = 0;
          if (edit_col > (int)lines[edit_row].length()) edit_col = lines[edit_row].length();
          dirty = true;
        } else if (ansi_buf == "[6~") { // Page Down
          edit_row += 20;
          if (edit_row >= (int)lines.size()) edit_row = lines.size() - 1;
          if (edit_col > (int)lines[edit_row].length()) edit_col = lines[edit_row].length();
          dirty = true;
        }
      } else if (ansi_buf.length() > 5) {
        ansi_state = 0; // abort if too long
      }
      continue;
    }
    
    if (key == '\r' || key == '\n') {
      std::string rem = lines[edit_row].substr(edit_col);
      lines[edit_row] = lines[edit_row].substr(0, edit_col);
      lines.insert(lines.begin() + edit_row + 1, rem);
      edit_row++;
      edit_col = 0;
      dirty = true;
    } else if (key == 8 || key == 127) { // Backspace
      if (edit_col > 0) {
        lines[edit_row].erase(edit_col - 1, 1);
        edit_col--;
        dirty = true;
      } else if (edit_row > 0) {
        edit_col = lines[edit_row - 1].length();
        lines[edit_row - 1] += lines[edit_row];
        lines.erase(lines.begin() + edit_row);
        edit_row--;
        dirty = true;
      }
    } else if (key >= 32 && key < 127) { // Printable
      lines[edit_row].insert(edit_col, 1, (char)key);
      edit_col++;
      dirty = true;
    }
  }
}

#endif // FIRMWARE_EDITOR_H_
