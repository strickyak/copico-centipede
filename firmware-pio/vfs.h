#ifndef FIRMWARE_PIO_VFS_H_
#define FIRMWARE_PIO_VFS_H_

#include <string>
#include <vector>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "../littlefs/lfs.h"
}

/*
 * Virtual Filesystem (VFS) Filename Conventions:
 * 
 * 1. Filenames starting with `/` or `.` are files on the Tether PC. 
 *    (These will be accessed via RPC packets over the USB cable).
 * 2. Filenames starting with [A-Za-z0-9_] are files/directories in the LittleFS on the firmware.
 * 3. The symbol `@` refers to the root directory of the LittleFS (internally `/`).
 *    For example, `etc/motd` is equivalent to `@/etc/motd`.
 * 4. An empty string "" is treated identically to `@`.
 * 5. All other initial letters are currently not allowed.
 */

extern lfs_t lfs_volume;

enum class FsType { LittleFS, TetherFS, Invalid };

inline FsType vfs_parse_path(const std::string& path, std::string& out_path) {
    if (path.empty() || path == "@") {
        out_path = "/";
        return FsType::LittleFS;
    }
    char c = path[0];
    if (c == '/' || c == '.') {
        out_path = path;
        return FsType::TetherFS;
    }
    if (c == '@') {
        out_path = "/" + path.substr(1);
        while (out_path.length() > 1 && out_path[0] == '/' && out_path[1] == '/') {
            out_path = out_path.substr(1);
        }
        return FsType::LittleFS;
    }
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
        out_path = "/" + path;
        return FsType::LittleFS;
    }
    return FsType::Invalid;
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
};

struct vfs_info {
    int type; // Matches LFS_TYPE_REG or LFS_TYPE_DIR
    char name[LFS_NAME_MAX + 1];
    lfs_size_t size;
};

inline int vfs_file_open(vfs_file_t* file, const std::string& path, int flags) {
    std::string real_path;
    FsType type = vfs_parse_path(path, real_path);
    file->type = type;
    if (type == FsType::LittleFS) {
        return lfs_file_open(&lfs_volume, &file->lfs_file, real_path.c_str(), flags);
    } else if (type == FsType::TetherFS) {
        printf("TetherFS: open(%s, %d) not implemented\n", real_path.c_str(), flags);
        return -1;
    }
    return -1; // Invalid
}

inline lfs_ssize_t vfs_file_read(vfs_file_t* file, void* buffer, lfs_size_t size) {
    if (file->type == FsType::LittleFS) {
        return lfs_file_read(&lfs_volume, &file->lfs_file, buffer, size);
    } else if (file->type == FsType::TetherFS) {
        printf("TetherFS: read not implemented\n");
        return -1;
    }
    return -1;
}

inline lfs_ssize_t vfs_file_write(vfs_file_t* file, const void* buffer, lfs_size_t size) {
    if (file->type == FsType::LittleFS) {
        return lfs_file_write(&lfs_volume, &file->lfs_file, buffer, size);
    } else if (file->type == FsType::TetherFS) {
        printf("TetherFS: write not implemented\n");
        return -1;
    }
    return -1;
}

inline int vfs_file_close(vfs_file_t* file) {
    if (file->type == FsType::LittleFS) {
        return lfs_file_close(&lfs_volume, &file->lfs_file);
    } else if (file->type == FsType::TetherFS) {
        printf("TetherFS: close not implemented\n");
        return 0;
    }
    return -1;
}

inline int vfs_dir_open(vfs_dir_t* dir, const std::string& path) {
    std::string real_path;
    FsType type = vfs_parse_path(path, real_path);
    dir->type = type;
    if (type == FsType::LittleFS) {
        return lfs_dir_open(&lfs_volume, &dir->lfs_dir, real_path.c_str());
    } else if (type == FsType::TetherFS) {
        printf("TetherFS: dir_open(%s) not implemented\n", real_path.c_str());
        return -1;
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
        }
        return res;
    } else if (dir->type == FsType::TetherFS) {
        printf("TetherFS: dir_read not implemented\n");
        return -1;
    }
    return -1;
}

inline int vfs_dir_close(vfs_dir_t* dir) {
    if (dir->type == FsType::LittleFS) {
        return lfs_dir_close(&lfs_volume, &dir->lfs_dir);
    } else if (dir->type == FsType::TetherFS) {
        printf("TetherFS: dir_close not implemented\n");
        return 0;
    }
    return -1;
}

inline int vfs_mkdir(const std::string& path) {
    std::string real_path;
    FsType type = vfs_parse_path(path, real_path);
    if (type == FsType::LittleFS) {
        return lfs_mkdir(&lfs_volume, real_path.c_str());
    } else if (type == FsType::TetherFS) {
        printf("TetherFS: mkdir(%s) not implemented\n", real_path.c_str());
        return -1;
    }
    return -1;
}

inline int vfs_remove(const std::string& path) {
    std::string real_path;
    FsType type = vfs_parse_path(path, real_path);
    if (type == FsType::LittleFS) {
        return lfs_remove(&lfs_volume, real_path.c_str());
    } else if (type == FsType::TetherFS) {
        printf("TetherFS: remove(%s) not implemented\n", real_path.c_str());
        return -1;
    }
    return -1;
}

inline int vfs_stat(const std::string& path, struct vfs_info* info) {
    std::string real_path;
    FsType type = vfs_parse_path(path, real_path);
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
        printf("TetherFS: stat(%s) not implemented\n", real_path.c_str());
        return -1;
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

inline void glob_recursive(const std::string& current_path, const std::string& remaining_pattern, std::vector<std::string>& results) {
    if (remaining_pattern.empty()) {
        struct vfs_info info;
        if (vfs_stat(current_path, &info) >= 0) {
            results.push_back(current_path);
        }
        return;
    }

    size_t slash = remaining_pattern.find('/');
    std::string comp = remaining_pattern.substr(0, slash);
    std::string next_rem = (slash == std::string::npos) ? "" : remaining_pattern.substr(slash + 1);

    bool has_wildcard = (comp.find('*') != std::string::npos || comp.find('?') != std::string::npos || comp.find('[') != std::string::npos);

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

    std::string scan_path = current_path;
    if (scan_path.empty()) scan_path = "@";

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
    } else if (pattern[0] == '@') {
        if (pattern.length() > 1 && pattern[1] == '/') {
            current_path = "@/";
            remaining_pattern = pattern.substr(2);
        } else {
            current_path = "@";
            remaining_pattern = pattern.substr(1);
        }
    } else if (pattern[0] == '.') {
        if (pattern.length() > 1 && pattern[1] == '/') {
            current_path = "./";
            remaining_pattern = pattern.substr(2);
        } else if (pattern.length() == 1) {
            current_path = ".";
            remaining_pattern = "";
        }
    }
    
    glob_recursive(current_path, remaining_pattern, results);
    return results;
}

#endif // FIRMWARE_PIO_VFS_H_
