#ifndef FIRMWARE_PIO_VFS_H_
#define FIRMWARE_PIO_VFS_H_

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "cobs_tx.h"

extern "C" {
#include "../littlefs/lfs.h"
}

#include "vfs_rpc.h"

/*
 * Virtual Filesystem (VFS) Filename Conventions:
 *
 * - The filesystem mimics a unified Unix hierarchy with a root `/`.
 * - Absolute paths begin with `/`, otherwise paths are relative to `vfs_cwd`.
 * - The virtual directory `/pc` routes to the TetherFS PC filesystem via RPC.
 * - All other paths reside in the onboard LittleFS.
 */

extern lfs_t lfs_volume;
extern std::string vfs_cwd;

enum class FsType { LittleFS, TetherFS };

inline std::string vfs_normalize_path(const std::string& path) {
  std::string full_path = path;
  if (full_path.empty()) full_path = ".";
  if (full_path[0] != '/') {
    full_path = vfs_cwd + "/" + full_path;
  }

  std::vector<std::string> parts;
  size_t i = 0;
  while (i < full_path.length()) {
    size_t next = full_path.find('/', i);
    std::string part;
    if (next == std::string::npos) {
      part = full_path.substr(i);
      i = full_path.length();
    } else {
      part = full_path.substr(i, next - i);
      i = next + 1;
    }

    if (part == "" || part == ".") {
      continue;
    } else if (part == "..") {
      if (!parts.empty()) parts.pop_back();
    } else {
      parts.push_back(part);
    }
  }

  std::string resolved = "";
  for (const auto& p : parts) {
    resolved += "/" + p;
  }
  if (resolved.empty()) resolved = "/";
  return resolved;
}

inline FsType vfs_get_mount(const std::string& norm_path,
                            std::string& relative_path) {
  if (norm_path == "/pc" || norm_path.find("/pc/") == 0) {
    if (norm_path == "/pc") {
      relative_path = "/";
    } else {
      relative_path = norm_path.substr(3);
    }
    return FsType::TetherFS;
  }
  relative_path = norm_path;
  return FsType::LittleFS;
}

struct vfs_file_t {
  FsType type;
  union {
    lfs_file_t lfs_file;
    int tether_fd;
  };
};

struct vfs_dir_t {
  FsType type;
  union {
    lfs_dir_t lfs_dir;
    int tether_dir;
  };
  bool is_root;
  bool virtual_pc_returned;
};

struct vfs_info {
  int type;  // Matches LFS_TYPE_REG or LFS_TYPE_DIR
  char name[LFS_NAME_MAX + 1];
  lfs_size_t size;
};

inline int vfs_file_open(vfs_file_t* file, const std::string& path, int flags) {
  std::string norm_path = vfs_normalize_path(path);
  std::string real_path;
  FsType type = vfs_get_mount(norm_path, real_path);
  file->type = type;
  if (type == FsType::LittleFS) {
    return lfs_file_open(&lfs_volume, &file->lfs_file, real_path.c_str(),
                         flags);
  } else if (type == FsType::TetherFS) {
    pcb::RpcRequest req;
    req.method = "open";
    req.path = real_path;
    req.flags = flags;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req);
    if (resp.status != 0) return -1;
    file->tether_fd = resp.handle;
    return 0;
  }
  return -1;
}

inline lfs_ssize_t vfs_file_read(vfs_file_t* file, void* buffer,
                                 lfs_size_t size) {
  if (file->type == FsType::LittleFS) {
    return lfs_file_read(&lfs_volume, &file->lfs_file, buffer, size);
  } else if (file->type == FsType::TetherFS) {
    pcb::RpcRequest req;
    req.method = "read";
    req.handle = file->tether_fd;
    req.length = size;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req);
    if (resp.status != 0) return -1;

    lfs_size_t read_size = resp.data.size();
    if (read_size > size) read_size = size;
    if (read_size > 0) {
      memcpy(buffer, resp.data.data(), read_size);
    }
    return read_size;
  }
  return -1;
}

inline lfs_ssize_t vfs_file_write(vfs_file_t* file, const void* buffer,
                                  lfs_size_t size) {
  if (file->type == FsType::LittleFS) {
    return lfs_file_write(&lfs_volume, &file->lfs_file, buffer, size);
  } else if (file->type == FsType::TetherFS) {
    pcb::RpcRequest req;
    req.method = "write";
    req.handle = file->tether_fd;
    req.data.assign(static_cast<const char*>(buffer), size);
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req);
    if (resp.status != 0) return -1;
    return size;
  }
  return -1;
}

inline int vfs_file_close(vfs_file_t* file, Coro* self = nullptr) {
  if (file->type == FsType::LittleFS) {
    return lfs_file_close(&lfs_volume, &file->lfs_file);
  } else if (file->type == FsType::TetherFS) {
    pcb::RpcRequest req;
    req.method = "close";
    req.handle = file->tether_fd;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req, self);
    if (resp.status != 0) return -1;
    return 0;
  }
  return -1;
}

inline lfs_soff_t vfs_file_seek(vfs_file_t* file, lfs_soff_t offset, int whence, Coro* self = nullptr) {
  if (file->type == FsType::LittleFS) {
    return lfs_file_seek(&lfs_volume, &file->lfs_file, offset, whence);
  } else if (file->type == FsType::TetherFS) {
    pcb::RpcRequest req;
    req.method = "seek";
    req.handle = file->tether_fd;
    req.offset = offset;
    req.whence = whence;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req, self);
    if (resp.status != 0) return -1;
    return resp.size;
  }
  return -1;
}

inline int vfs_dir_open(vfs_dir_t* dir, const std::string& path) {
  std::string norm_path = vfs_normalize_path(path);
  std::string real_path;
  FsType type = vfs_get_mount(norm_path, real_path);
  dir->type = type;
  dir->is_root = (norm_path == "/");
  dir->virtual_pc_returned = false;

  if (type == FsType::LittleFS) {
    return lfs_dir_open(&lfs_volume, &dir->lfs_dir, real_path.c_str());
  } else if (type == FsType::TetherFS) {
    pcb::RpcRequest req;
    req.method = "dir_open";
    req.path = real_path;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req);
    if (resp.status != 0) return -1;
    dir->tether_dir = resp.handle;
    return 0;
  }
  return -1;
}

inline int vfs_dir_read(vfs_dir_t* dir, struct vfs_info* info) {
  if (dir->type == FsType::LittleFS) {
    struct lfs_info lfs_i;
    int res = lfs_dir_read(&lfs_volume, &dir->lfs_dir, &lfs_i);
    if (res > 0) {
      info->type = lfs_i.type;
      info->size = lfs_i.size;
      snprintf(info->name, sizeof(info->name), "%s", lfs_i.name);

      // Prevent duplicate virtual mount if it physically exists
      if (dir->is_root && strcmp(info->name, "pc") == 0) {
        dir->virtual_pc_returned = true;
      }
      return res;
    } else if (res == 0) {
      if (dir->is_root && !dir->virtual_pc_returned) {
        dir->virtual_pc_returned = true;
        info->type = LFS_TYPE_DIR;
        info->size = 0;
        snprintf(info->name, sizeof(info->name), "pc");
        return 1;
      }
      return 0;
    }
    return res;
  } else if (dir->type == FsType::TetherFS) {
    pcb::RpcRequest req;
    req.method = "dir_read";
    req.handle = dir->tether_dir;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req);
    if (resp.status != 0) return -1;
    if (resp.data.empty()) return 0;  // End of directory

    info->type = resp.is_dir ? LFS_TYPE_DIR : LFS_TYPE_REG;
    info->size = resp.size;
    snprintf(info->name, sizeof(info->name), "%s", resp.data.c_str());
    return 1;
  }
  return -1;
}

inline int vfs_dir_close(vfs_dir_t* dir) {
  if (dir->type == FsType::LittleFS) {
    return lfs_dir_close(&lfs_volume, &dir->lfs_dir);
  } else if (dir->type == FsType::TetherFS) {
    pcb::RpcRequest req;
    req.method = "dir_close";
    req.handle = dir->tether_dir;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req);
    if (resp.status != 0) return -1;
    return 0;
  }
  return -1;
}

inline int vfs_mkdir(const std::string& path) {
  std::string norm_path = vfs_normalize_path(path);
  if (norm_path == "/pc" || norm_path == "/") {
    return -1;  // Cannot mkdir virtual mounts
  }
  std::string real_path;
  FsType type = vfs_get_mount(norm_path, real_path);
  if (type == FsType::LittleFS) {
    return lfs_mkdir(&lfs_volume, real_path.c_str());
  } else if (type == FsType::TetherFS) {
    pcb::RpcRequest req;
    req.method = "mkdir";
    req.path = real_path;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req);
    if (resp.status != 0) return -1;
    return 0;
  }
  return -1;
}

inline int vfs_remove(const std::string& path) {
  std::string norm_path = vfs_normalize_path(path);
  if (norm_path == "/pc" || norm_path == "/") {
    return -1;  // Cannot remove virtual mounts
  }
  std::string real_path;
  FsType type = vfs_get_mount(norm_path, real_path);
  if (type == FsType::LittleFS) {
    return lfs_remove(&lfs_volume, real_path.c_str());
  } else if (type == FsType::TetherFS) {
    pcb::RpcRequest req;
    req.method = "remove";
    req.path = real_path;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req);
    if (resp.status != 0) return -1;
    return 0;
  }
  return -1;
}

inline int vfs_stat(const std::string& path, struct vfs_info* info) {
  std::string norm_path = vfs_normalize_path(path);
  if (norm_path == "/pc") {
    info->type = LFS_TYPE_DIR;
    info->size = 0;
    snprintf(info->name, sizeof(info->name), "pc");
    return 0;
  }

  std::string real_path;
  FsType type = vfs_get_mount(norm_path, real_path);
  if (type == FsType::LittleFS) {
    struct lfs_info lfs_i;
    int res = lfs_stat(&lfs_volume, real_path.c_str(), &lfs_i);
    if (res >= 0) {
      info->type = lfs_i.type;
      info->size = lfs_i.size;
      snprintf(info->name, sizeof(info->name), "%s", lfs_i.name);
    }
    return res;
  } else if (type == FsType::TetherFS) {
    pcb::RpcRequest req;
    req.method = "stat";
    req.path = real_path;
    req.serial = rpc::next_serial++;
    pcb::RpcResponse resp = rpc::vfs_rpc_call(req);
    if (resp.status != 0) return -1;
    info->type = resp.is_dir ? LFS_TYPE_DIR : LFS_TYPE_REG;
    info->size = resp.size;
    snprintf(info->name, sizeof(info->name), "%s", resp.data.c_str());
    return 0;
  }
  return -1;
}

// ----------------------------------------------------------------------------
// Globbing implementation
// ----------------------------------------------------------------------------

inline bool match_pattern(const char* pattern, const char* str) {
  while (*pattern && *str) {
    if (*pattern == '*') {
      while (*pattern == '*') pattern++;
      if (!*pattern) return true;
      while (*str) {
        if (match_pattern(pattern, str)) return true;
        str++;
      }
      return false;
    } else if (*pattern == '?') {
      pattern++;
      str++;
    } else if (*pattern == '[') {
      pattern++;
      bool invert = false;
      if (*pattern == '!' || *pattern == '^') {
        invert = true;
        pattern++;
      }
      bool matched = false;
      while (*pattern && *pattern != ']') {
        if (pattern[1] == '-' && pattern[2] && pattern[2] != ']') {
          if (*str >= pattern[0] && *str <= pattern[2]) matched = true;
          pattern += 3;
        } else {
          if (*str == *pattern) matched = true;
          pattern++;
        }
      }
      if (*pattern == ']') pattern++;
      if (matched == invert) return false;
      str++;
    } else {
      if (*pattern != *str) return false;
      pattern++;
      str++;
    }
  }
  while (*pattern == '*') pattern++;
  return *pattern == '\0' && *str == '\0';
}

inline void glob_recursive(const std::string& current_path,
                           const std::string& remaining_pattern,
                           std::vector<std::string>& results) {
  if (remaining_pattern.empty()) {
    struct vfs_info info;
    // current_path is relative to CWD if it didn't start with /
    std::string scan_path = current_path.empty() ? "." : current_path;
    if (vfs_stat(scan_path, &info) >= 0) {
      results.push_back(scan_path);
    }
    return;
  }

  size_t slash = remaining_pattern.find('/');
  std::string comp = remaining_pattern.substr(0, slash);
  std::string next_rem =
      (slash == std::string::npos) ? "" : remaining_pattern.substr(slash + 1);

  bool has_wildcard = (comp.find('*') != std::string::npos ||
                       comp.find('?') != std::string::npos ||
                       comp.find('[') != std::string::npos);

  if (!has_wildcard) {
    std::string next_path;
    if (current_path.empty()) {
      next_path = comp;
      if (slash == 0) next_path = "/";
    } else {
      if (current_path.back() == '/') {
        next_path = current_path + comp;
      } else {
        next_path = current_path + "/" + comp;
      }
    }

    if (comp == "" && slash != std::string::npos) {
      // Consecutive slashes or trailing slash
      glob_recursive(next_path, next_rem, results);
      return;
    }

    glob_recursive(next_path, next_rem, results);
    return;
  }

  std::string scan_path = current_path.empty() ? "." : current_path;

  vfs_dir_t dir;
  if (vfs_dir_open(&dir, scan_path) < 0) {
    return;
  }

  struct vfs_info info;
  while (vfs_dir_read(&dir, &info) > 0) {
    if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) continue;

    if (match_pattern(comp.c_str(), info.name)) {
      std::string next_path;
      if (current_path.empty()) {
        next_path = info.name;
      } else {
        if (current_path.back() == '/') {
          next_path = current_path + info.name;
        } else {
          next_path = current_path + "/" + info.name;
        }
      }

      if (!next_rem.empty()) {
        if (info.type != LFS_TYPE_DIR) continue;
      }

      glob_recursive(next_path, next_rem, results);
    }
  }
  vfs_dir_close(&dir);
}

inline std::vector<std::string> glob(const std::string& pattern) {
  std::vector<std::string> results;

  if (pattern.empty()) {
    results.push_back("");
    return results;
  }

  std::string current_path = "";
  std::string remaining_pattern = pattern;

  if (pattern[0] == '/') {
    current_path = "/";
    remaining_pattern = pattern.substr(1);
  }

  glob_recursive(current_path, remaining_pattern, results);
  return results;
}

#endif  // FIRMWARE_PIO_VFS_H_
