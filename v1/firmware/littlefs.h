#include "cobs_tx.h"
#ifndef FIRMWARE_PIO_LITTLEFS_H_
#define FIRMWARE_PIO_LITTLEFS_H_

// Allocate your static buffers to prevent heap fragmentation
uint8_t lfs_read_buf[256];
uint8_t lfs_prog_buf[256];
uint8_t lfs_lookahead_buf[16];  // 16 bytes * 8 = 128 blocks tracked

const struct lfs_config lfs = {
    // Link your hardware glue functions
    .read = pico_lfs_read,
    .prog = pico_lfs_prog,
    .erase = pico_lfs_erase,
    .sync = pico_lfs_sync,

    // Block device configurations for typical RP2350 flash chips
    .read_size = 1,    // Read granularity can be down to 1 byte
    .prog_size = 256,  // Radio Shack (DECB & OS-9) sector size
    .block_size =
        LFS_BLOCK_SIZE,  // W25Q128 standard sector erase size (4096 bytes)
    .block_count = LFS_BLOCK_COUNT,  // Example: 512 blocks * 4KB = 2 Megabytes
    .block_cycles = 500,  // Dynamic wear-leveling threshold before eviction
    .cache_size = 256,    // Match your program size for performance
    .lookahead_size = 16,

    .read_buffer = lfs_read_buf,
    .prog_buffer = lfs_prog_buf,
    .lookahead_buffer = lfs_lookahead_buf,
};

lfs_t lfs_volume;
std::string vfs_cwd = "/";

// =========================================================================
// Native Tcl commands — use Tcl_SetResult for output.
// Signature: int cmd(ClientData, Tcl_Interp*, int argc, char* argv[])
// =========================================================================

extern "C" int dir_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  const char* path = (argc >= 2) ? argv[1] : ".";

  vfs_dir_t dir;
  int err = vfs_dir_open(&dir, path);
  if (err) {
    Tcl_SetResult(interp, (char*)"Failed to open directory", TCL_STATIC);
    return TCL_ERROR;
  }

  Tcl_ResetResult(interp);
  struct vfs_info info;
  bool first = true;
  while (true) {
    int res = vfs_dir_read(&dir, &info);
    if (res < 0) {
      vfs_dir_close(&dir);
      Tcl_SetResult(interp, (char*)"Error reading directory", TCL_STATIC);
      return TCL_ERROR;
    }
    if (res == 0) break;
    if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) continue;
    if (!first) {
      Tcl_AppendResult(interp, "\n", (char*)NULL);
    }
    first = false;
    Tcl_AppendResult(interp, info.name, (info.type == LFS_TYPE_DIR) ? "/" : "",
                     (char*)NULL);
  }
  vfs_dir_close(&dir);

  return TCL_OK;
}

extern "C" int mkdir_cmd(ClientData, Tcl_Interp* interp, int argc,
                         char* argv[]) {
  if (argc < 2) {
    Tcl_SetResult(interp, (char*)"Usage: mkdir dir...", TCL_STATIC);
    return TCL_ERROR;
  }
  for (int i = 1; i < argc; i++) {
    int err = vfs_mkdir(argv[i]);
    if (err < 0) {
      Tcl_SetResult(interp, (char*)"Failed to mkdir", TCL_STATIC);
      return TCL_ERROR;
    }
  }
  return TCL_OK;
}

extern "C" int rmdir_cmd(ClientData, Tcl_Interp* interp, int argc,
                         char* argv[]) {
  if (argc < 2) {
    Tcl_SetResult(interp, (char*)"Usage: rmdir dir...", TCL_STATIC);
    return TCL_ERROR;
  }
  for (int i = 1; i < argc; i++) {
    int err = vfs_remove(argv[i]);
    if (err < 0) {
      Tcl_SetResult(interp, (char*)"Failed to remove", TCL_STATIC);
      return TCL_ERROR;
    }
  }
  return TCL_OK;
}

extern "C" int echo_cmd(ClientData, Tcl_Interp* interp, int argc,
                        char* argv[]) {
  Tcl_ResetResult(interp);
  for (int i = 1; i < argc; i++) {
    if (i > 1) Tcl_AppendResult(interp, " ", (char*)NULL);
    Tcl_AppendResult(interp, argv[i], (char*)NULL);
  }
  return TCL_OK;
}

extern "C" int cp_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc != 3) {
    Tcl_SetResult(interp, (char*)"Usage: cp src dst", TCL_STATIC);
    return TCL_ERROR;
  }
  vfs_file_t src, dst;
  int err = vfs_file_open(&src, argv[1], LFS_O_RDONLY);
  if (err < 0) {
    Tcl_SetResult(interp, (char*)"cp: cannot open source", TCL_STATIC);
    return TCL_ERROR;
  }
  err = vfs_file_open(&dst, argv[2], LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
  if (err < 0) {
    vfs_file_close(&src);
    Tcl_SetResult(interp, (char*)"cp: cannot open destination", TCL_STATIC);
    return TCL_ERROR;
  }
  char buf[256];
  while (true) {
    lfs_ssize_t n = vfs_file_read(&src, buf, sizeof(buf));
    if (n < 0) {
      vfs_file_close(&src);
      vfs_file_close(&dst);
      Tcl_SetResult(interp, (char*)"cp: read error", TCL_STATIC);
      return TCL_ERROR;
    }
    if (n == 0) break;
    lfs_ssize_t w = vfs_file_write(&dst, buf, n);
    if (w < 0) {
      vfs_file_close(&src);
      vfs_file_close(&dst);
      Tcl_SetResult(interp, (char*)"cp: write error", TCL_STATIC);
      return TCL_ERROR;
    }
  }
  vfs_file_close(&src);
  vfs_file_close(&dst);
  return TCL_OK;
}

extern "C" int mv_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc != 3) {
    Tcl_SetResult(interp, (char*)"Usage: mv src dst", TCL_STATIC);
    return TCL_ERROR;
  }
  int err = lfs_rename(&lfs_volume, argv[1], argv[2]);
  if (err < 0) {
    Tcl_SetResult(interp, (char*)"mv: rename failed", TCL_STATIC);
    return TCL_ERROR;
  }
  return TCL_OK;
}

extern "C" int rm_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc < 2) {
    Tcl_SetResult(interp, (char*)"Usage: rm file...", TCL_STATIC);
    return TCL_ERROR;
  }
  for (int i = 1; i < argc; i++) {
    int err = vfs_remove(argv[i]);
    if (err < 0) {
      std::string msg = std::string("rm: cannot remove ") + argv[i];
      Tcl_SetResult(interp, const_cast<char*>(msg.c_str()), TCL_VOLATILE);
      return TCL_ERROR;
    }
  }
  return TCL_OK;
}

extern "C" int cat_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc < 2) {
    Tcl_SetResult(interp, (char*)"Usage: cat [-n] filename...", TCL_STATIC);
    return TCL_ERROR;
  }

  bool print_lines = false;
  int start_idx = 1;

  if (argc > 1 && strcmp(argv[1], "-n") == 0) {
    print_lines = true;
    start_idx = 2;
    if (argc < 3) {
      Tcl_SetResult(interp, (char*)"Usage: cat [-n] filename...", TCL_STATIC);
      return TCL_ERROR;
    }
  }

  Tcl_ResetResult(interp);
  int line_num = 1;
  bool at_line_start = true;
  bool output_started = false;

  for (int i = start_idx; i < argc; i++) {
    vfs_file_t file;
    int err = vfs_file_open(&file, argv[i], LFS_O_RDONLY);
    if (err < 0) {
      std::string msg =
          std::string("cat: ") + argv[i] + ": No such file or directory";
      Tcl_SetResult(interp, const_cast<char*>(msg.c_str()), TCL_VOLATILE);
      return TCL_ERROR;
    }

    char buf[64];
    while (true) {
      lfs_ssize_t res = vfs_file_read(&file, buf, sizeof(buf));
      if (res < 0) {
        vfs_file_close(&file);
        Tcl_SetResult(interp, (char*)"Error reading file", TCL_STATIC);
        return TCL_ERROR;
      }
      if (res == 0) break;

      if (!print_lines) {
        // Fast path for normal cat
        char tmp[65];
        memcpy(tmp, buf, res);
        tmp[res] = '\0';
        Tcl_AppendResult(interp, tmp, (char*)NULL);
        if (res > 0) output_started = true;
      } else {
        // Slow path for -n
        for (lfs_ssize_t j = 0; j < res; j++) {
          char ch = buf[j];
          if (at_line_start) {
            char numbuf[16];
            snprintf(numbuf, sizeof(numbuf), "%6d  ", line_num++);
            Tcl_AppendResult(interp, numbuf, (char*)NULL);
            at_line_start = false;
          }
          char str[2] = {ch, 0};
          Tcl_AppendResult(interp, str, (char*)NULL);
          if (ch == '\n') at_line_start = true;
          output_started = true;
        }
      }
    }
    vfs_file_close(&file);
  }

  // To strip trailing newline if desired, we can manipulate interp->result
  // directly, but standard cat usually outputs exact bytes anyway. If we really
  // wanted to remove it:
  if (output_started) {
    size_t len = strlen(interp->result);
    if (len > 0 && interp->result[len - 1] == '\n') {
      interp->result[len - 1] = '\0';
    }
  }

  return TCL_OK;
}

extern "C" int wc_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc < 2) {
    Tcl_SetResult(interp, (char*)"Usage: wc file...", TCL_STATIC);
    return TCL_ERROR;
  }

  Tcl_ResetResult(interp);
  int total_lines = 0;
  int total_words = 0;
  int total_chars = 0;
  bool output_started = false;

  for (int i = 1; i < argc; i++) {
    vfs_file_t file;
    int err = vfs_file_open(&file, argv[i], LFS_O_RDONLY);
    if (err < 0) {
      std::string msg =
          std::string("wc: ") + argv[i] + ": No such file or directory";
      Tcl_SetResult(interp, const_cast<char*>(msg.c_str()), TCL_VOLATILE);
      return TCL_ERROR;
    }

    int lines = 0;
    int words = 0;
    int chars = 0;
    bool in_word = false;

    char buf[64];
    while (true) {
      lfs_ssize_t res = vfs_file_read(&file, buf, sizeof(buf));
      if (res < 0) {
        vfs_file_close(&file);
        Tcl_SetResult(interp, (char*)"Error reading file", TCL_STATIC);
        return TCL_ERROR;
      }
      if (res == 0) break;
      for (lfs_ssize_t j = 0; j < res; j++) {
        char ch = buf[j];
        chars++;
        if (ch == '\n') lines++;
        if (std::isspace(static_cast<unsigned char>(ch))) {
          in_word = false;
        } else if (!in_word) {
          in_word = true;
          words++;
        }
      }
    }
    vfs_file_close(&file);

    total_lines += lines;
    total_words += words;
    total_chars += chars;

    if (output_started) Tcl_AppendResult(interp, "\n", (char*)NULL);
    char outbuf[128];
    snprintf(outbuf, sizeof(outbuf), "%7d %7d %7d %s", lines, words, chars,
             argv[i]);
    Tcl_AppendResult(interp, outbuf, (char*)NULL);
    output_started = true;
  }

  if (argc > 2) {
    if (output_started) Tcl_AppendResult(interp, "\n", (char*)NULL);
    char outbuf[128];
    snprintf(outbuf, sizeof(outbuf), "%7d %7d %7d total", total_lines,
             total_words, total_chars);
    Tcl_AppendResult(interp, outbuf, (char*)NULL);
  }

  return TCL_OK;
}

extern "C" int cd_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc < 2) {
    vfs_cwd = "/";
  } else {
    std::string new_cwd = vfs_normalize_path(argv[1]);
    struct vfs_info info;
    if (vfs_stat(new_cwd, &info) < 0 || info.type != LFS_TYPE_DIR) {
      std::string msg = std::string("cd: ") + argv[1] + ": Not a directory";
      Tcl_SetResult(interp, const_cast<char*>(msg.c_str()), TCL_VOLATILE);
      return TCL_ERROR;
    }
    vfs_cwd = new_cwd;
  }
  return TCL_OK;
}

extern "C" int pwd_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  Tcl_SetResult(interp, const_cast<char*>(vfs_cwd.c_str()), TCL_VOLATILE);
  return TCL_OK;
}

// "fs" — shell-like wrapper that globs arguments and supports >file
// redirection.
//
// Usage: fs cat *.txt >output.txt
//        fs dir /data >listing
//        fs echo hello world
//
// 1. Scans for ">" redirect: either ">filename" or "> filename"
// 2. Globs remaining arguments (no-match keeps original word, like sh)
// 3. Builds a Tcl command string and Tcl_Eval's it
// 4. On success with redirect: writes result to the file
// 5. Returns the Tcl result and error code
extern "C" int fs_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc < 2) {
    Tcl_SetResult(interp, (char*)"Usage: fs command [args...] [>file]",
                  TCL_STATIC);
    return TCL_ERROR;
  }

  // Pass 1: extract redirect and collect raw arguments
  std::string redirect_file;
  std::vector<std::string> raw_args;

  for (int i = 1; i < argc; i++) {
    const char* arg = argv[i];
    if (arg[0] == '>' && arg[1] != '\0') {
      // ">filename" form
      redirect_file = &arg[1];
    } else if (arg[0] == '>' && arg[1] == '\0') {
      // ">" "filename" form
      if (i + 1 < argc) {
        redirect_file = argv[++i];
      } else {
        Tcl_SetResult(interp, (char*)"> requires a filename", TCL_STATIC);
        return TCL_ERROR;
      }
    } else {
      raw_args.push_back(arg);
    }
  }

  if (raw_args.empty()) {
    Tcl_SetResult(interp, (char*)"fs: no command specified", TCL_STATIC);
    return TCL_ERROR;
  }

  // Pass 2: glob each argument (except the command name)
  std::vector<std::string> expanded;
  expanded.push_back(raw_args[0]);  // Don't glob the command name
  for (size_t i = 1; i < raw_args.size(); i++) {
    std::vector<std::string> matches = glob(raw_args[i]);
    if (matches.empty()) {
      expanded.push_back(raw_args[i]);  // No match → keep original
    } else {
      for (const auto& m : matches) {
        expanded.push_back(m);
      }
    }
  }

  // Pass 3: build a Tcl command string (list-style quoting)
  std::string tcl_cmd;
  for (size_t i = 0; i < expanded.size(); i++) {
    if (i > 0) tcl_cmd += " ";
    // Brace-quote each word to protect special characters
    tcl_cmd += "{";
    tcl_cmd += expanded[i];
    tcl_cmd += "}";
  }

  // Pass 4: evaluate
  int rc = Tcl_Eval(interp, const_cast<char*>(tcl_cmd.c_str()), 0, (char**)0);

  // Pass 5: if redirect and success, write result to file
  if (rc == TCL_OK && !redirect_file.empty()) {
    const char* result = interp->result;
    vfs_file_t file;
    int err = vfs_file_open(&file, redirect_file,
                            LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err < 0) {
      Tcl_SetResult(interp, (char*)"fs: failed to open redirect file",
                    TCL_STATIC);
      return TCL_ERROR;
    }
    if (result && result[0]) {
      vfs_file_write(&file, result, strlen(result));
      vfs_file_write(&file, "\n", 1);
    }
    vfs_file_close(&file);
  }

  // Result and error code are already set by Tcl_Eval
  return rc;
}

extern "C" int TclCommandWrapper(ClientData clientData, Tcl_Interp* interp,
                                 int argc, char* argv[]);

void init_lfs() {
  int err = lfs_mount(&lfs_volume, &lfs);
  if (err) {
    cobs_printf("Formatting littlefs\n");
    lfs_format(&lfs_volume, &lfs);
    err = lfs_mount(&lfs_volume, &lfs);
    if (err) {
      cobs_printf("*** CANNOT FORMAT AND MOUNT littlefs\n");
    }
  } else {
    cobs_printf("Mounted littlefs\n");
  }

  global_tcl_interp = Tcl_CreateInterp();

  // Register native Tcl commands directly
  Tcl_CreateCommand(global_tcl_interp, (char*)"dir", dir_cmd, NULL, NULL);
  Tcl_CreateCommand(global_tcl_interp, (char*)"mkdir", mkdir_cmd, NULL, NULL);
  Tcl_CreateCommand(global_tcl_interp, (char*)"rmdir", rmdir_cmd, NULL, NULL);
  Tcl_CreateCommand(global_tcl_interp, (char*)"echo", echo_cmd, NULL, NULL);
  Tcl_CreateCommand(global_tcl_interp, (char*)"cp", cp_cmd, NULL, NULL);
  Tcl_CreateCommand(global_tcl_interp, (char*)"mv", mv_cmd, NULL, NULL);
  Tcl_CreateCommand(global_tcl_interp, (char*)"rm", rm_cmd, NULL, NULL);
  Tcl_CreateCommand(global_tcl_interp, (char*)"cat", cat_cmd, NULL, NULL);
  Tcl_CreateCommand(global_tcl_interp, (char*)"wc", wc_cmd, NULL, NULL);
  Tcl_CreateCommand(global_tcl_interp, (char*)"cd", cd_cmd, NULL, NULL);
  Tcl_CreateCommand(global_tcl_interp, (char*)"pwd", pwd_cmd, NULL, NULL);
  Tcl_CreateCommand(global_tcl_interp, (char*)"fs", fs_cmd, NULL, NULL);
}

#endif  // FIRMWARE_PIO_LITTLEFS_H_
