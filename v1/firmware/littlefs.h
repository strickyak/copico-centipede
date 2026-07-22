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

extern "C" int dir_cmd(ClientData, Tcl_Interp* interp, int argc,
                       char* argv[]) {
  const char* path = (argc >= 2) ? argv[1] : ".";

  vfs_dir_t dir;
  int err = vfs_dir_open(&dir, path);
  if (err) {
    Tcl_SetResult(interp, (char*)"Failed to open directory", TCL_STATIC);
    return TCL_ERROR;
  }

  std::string result;
  struct vfs_info info;
  while (true) {
    int res = vfs_dir_read(&dir, &info);
    if (res < 0) {
      vfs_dir_close(&dir);
      Tcl_SetResult(interp, (char*)"Error reading directory", TCL_STATIC);
      return TCL_ERROR;
    }
    if (res == 0) break;
    if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) continue;
    if (!result.empty()) result += "\n";
    result += info.name;
    if (info.type == LFS_TYPE_DIR) result += "/";
  }
  vfs_dir_close(&dir);

  Tcl_SetResult(interp, const_cast<char*>(result.c_str()), TCL_VOLATILE);
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
  std::string result;
  for (int i = 1; i < argc; i++) {
    if (i > 1) result += " ";
    result += argv[i];
  }
  Tcl_SetResult(interp, const_cast<char*>(result.c_str()), TCL_VOLATILE);
  return TCL_OK;
}

// echo-create stays as a wrapper-based command (writes to file, no output).
script::errstring echo_create_command(const std::vector<std::string>& argv) {
  if (argv.size() < 2) {
    return "Usage: echo-create filename [args...]";
  }

  vfs_file_t file;
  int err =
      vfs_file_open(&file, argv[1], LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
  if (err < 0) {
    return "Failed to create file: " + argv[1];
  }

  for (size_t i = 2; i < argv.size(); i++) {
    if (i > 2) {
      vfs_file_write(&file, " ", 1);
    }
    vfs_file_write(&file, argv[i].c_str(), argv[i].length());
  }
  vfs_file_write(&file, "\n", 1);

  vfs_file_close(&file);
  return "";
}

extern "C" int cat_cmd(ClientData, Tcl_Interp* interp, int argc,
                       char* argv[]) {
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

  std::string result;
  int line_num = 1;
  bool at_line_start = true;

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
      for (lfs_ssize_t j = 0; j < res; j++) {
        char ch = buf[j];
        if (at_line_start && print_lines) {
          char numbuf[16];
          snprintf(numbuf, sizeof(numbuf), "%6d  ", line_num++);
          result += numbuf;
          at_line_start = false;
        }
        result += ch;
        if (ch == '\n') at_line_start = true;
      }
    }
    vfs_file_close(&file);
  }
  // Remove trailing newline if present
  if (!result.empty() && result.back() == '\n') result.pop_back();

  Tcl_SetResult(interp, const_cast<char*>(result.c_str()), TCL_VOLATILE);
  return TCL_OK;
}

extern "C" int cd_cmd(ClientData, Tcl_Interp* interp, int argc,
                      char* argv[]) {
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

extern "C" int pwd_cmd(ClientData, Tcl_Interp* interp, int argc,
                       char* argv[]) {
  Tcl_SetResult(interp, const_cast<char*>(vfs_cwd.c_str()), TCL_VOLATILE);
  return TCL_OK;
}

extern "C" int TclCommandWrapper(ClientData clientData, Tcl_Interp* interp,
                                 int argc, char* argv[]);

void init_lfs() {
  int err = lfs_mount(&lfs_volume, &lfs);
  if (err) {
    printf("Formatting littlefs\n");
    lfs_format(&lfs_volume, &lfs);
    err = lfs_mount(&lfs_volume, &lfs);
    if (err) {
      printf("*** CANNOT FORMAT AND MOUNT littlefs\n");
    }
  } else {
    printf("Mounted littlefs\n");
  }

  // echo-create uses TclCommandWrapper (it needs std::vector glob expansion)
  script::global_script_commands.push_back(
      {"echo-create", echo_create_command});

  global_tcl_interp = Tcl_CreateInterp();

  // Register wrapper-based commands
  for (const auto& cmd : script::global_script_commands) {
    Tcl_CreateCommand(global_tcl_interp, const_cast<char*>(cmd.name.c_str()),
                      TclCommandWrapper, (ClientData)cmd.func, NULL);
  }

  // Register native Tcl commands directly
  Tcl_CreateCommand(global_tcl_interp, (char*)"dir", dir_cmd, NULL, NULL);
  Tcl_CreateCommand(global_tcl_interp, (char*)"mkdir", mkdir_cmd, NULL, NULL);
  Tcl_CreateCommand(global_tcl_interp, (char*)"rmdir", rmdir_cmd, NULL, NULL);
  Tcl_CreateCommand(global_tcl_interp, (char*)"echo", echo_cmd, NULL, NULL);
  Tcl_CreateCommand(global_tcl_interp, (char*)"cat", cat_cmd, NULL, NULL);
  Tcl_CreateCommand(global_tcl_interp, (char*)"cd", cd_cmd, NULL, NULL);
  Tcl_CreateCommand(global_tcl_interp, (char*)"pwd", pwd_cmd, NULL, NULL);
}

#endif  // FIRMWARE_PIO_LITTLEFS_H_
