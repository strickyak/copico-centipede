#ifndef FIRMWARE_PIO_LITTLEFS_H_
#define FIRMWARE_PIO_LITTLEFS_H_

// Allocate your static buffers to prevent heap fragmentation
uint8_t lfs_read_buf[256];
uint8_t lfs_prog_buf[256];
uint8_t lfs_lookahead_buf[16]; // 16 bytes * 8 = 128 blocks tracked

const struct lfs_config lfs = {
    // Link your hardware glue functions
    .read  = pico_lfs_read,
    .prog  = pico_lfs_prog,
    .erase = pico_lfs_erase,
    .sync  = pico_lfs_sync,

    // Block device configurations for typical RP2350 flash chips
    .read_size      = 1,                // Read granularity can be down to 1 byte
    .prog_size      = 256,              // Radio Shack (DECB & OS-9) sector size
    .block_size     = LFS_BLOCK_SIZE,   // W25Q128 standard sector erase size (4096 bytes)
    .block_count    = LFS_BLOCK_COUNT,  // Example: 512 blocks * 4KB = 2 Megabytes
    .block_cycles   = 500,              // Dynamic wear-leveling threshold before eviction
    .cache_size     = 256,              // Match your program size for performance
    .lookahead_size = 16,

    .read_buffer      = lfs_read_buf,
    .prog_buffer      = lfs_prog_buf,
    .lookahead_buffer = lfs_lookahead_buf,
};

lfs_t lfs_volume;

script::errstring dir_command(const std::vector<std::string>& argv) {
    std::vector<std::string> dirs;
    if (argv.size() < 2) {
        dirs.push_back("@");
    } else {
        for (size_t i = 1; i < argv.size(); i++) {
            dirs.push_back(argv[i]);
        }
    }
    
    for (const auto& d : dirs) {
        vfs_dir_t dir;
        int err = vfs_dir_open(&dir, d);
        if (err) {
            return "Failed to open directory: " + d;
        }
        
        if (dirs.size() > 1) {
            printf("%s:\n", d.c_str());
        }
        
        struct vfs_info info;
        while (true) {
            int res = vfs_dir_read(&dir, &info);
            if (res < 0) {
                vfs_dir_close(&dir);
                return "Error reading directory: " + d;
            }
            if (res == 0) {
                break;
            }
            if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) {
                continue;
            }
            if (info.type == LFS_TYPE_DIR) {
                printf("%s/\n", info.name);
            } else {
                printf("%s\n", info.name);
            }
        }
        
        vfs_dir_close(&dir);
    }
    return "";
}

script::errstring mkdir_command(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return "Usage: mkdir dir...";
    }
    for (size_t i = 1; i < argv.size(); i++) {
        int err = vfs_mkdir(argv[i]);
        if (err < 0) {
            return "Failed to mkdir: " + argv[i];
        }
    }
    return "";
}

script::errstring rmdir_command(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return "Usage: rmdir dir...";
    }
    for (size_t i = 1; i < argv.size(); i++) {
        int err = vfs_remove(argv[i]);
        if (err < 0) {
            return "Failed to rmdir: " + argv[i];
        }
    }
    return "";
}

script::errstring echo_command(const std::vector<std::string>& argv) {
    for (size_t i = 1; i < argv.size(); i++) {
        if (i > 1) printf(" ");
        printf("%s", argv[i].c_str());
    }
    printf("\n");
    return "";
}

script::errstring echo_create_command(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return "Usage: echo-create filename [args...]";
    }
    
    vfs_file_t file;
    int err = vfs_file_open(&file, argv[1], LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
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

script::errstring cat_command(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return "Usage: cat [-n] filename...";
    }
    
    bool print_lines = false;
    size_t start_idx = 1;
    
    if (argv.size() > 1 && argv[1] == "-n") {
        print_lines = true;
        start_idx = 2;
        if (argv.size() < 3) {
            return "Usage: cat [-n] filename...";
        }
    }
    
    int line_num = 1;
    bool at_line_start = true;
    
    for (size_t i = start_idx; i < argv.size(); i++) {
        vfs_file_t file;
        int err = vfs_file_open(&file, argv[i], LFS_O_RDONLY);
        if (err < 0) {
            return "cat: " + argv[i] + ": No such file or directory";
        }
        
        char buf[64];
        while (true) {
            lfs_ssize_t res = vfs_file_read(&file, buf, sizeof(buf));
            if (res < 0) {
                vfs_file_close(&file);
                return "Error reading file: " + argv[i];
            }
            if (res == 0) {
                break;
            }
            for (lfs_ssize_t j = 0; j < res; j++) {
                char ch = buf[j];
                if (at_line_start) {
                    if (print_lines) {
                        printf("%6d  ", line_num++);
                    }
                    at_line_start = false;
                }
                printf("%c", ch);
                if (ch == '\n') {
                    at_line_start = true;
                }
            }
        }
        vfs_file_close(&file);
    }
    return "";
}

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

    script::global_script_commands.push_back({ "dir", dir_command });
    script::global_script_commands.push_back({ "mkdir", mkdir_command });
    script::global_script_commands.push_back({ "rmdir", rmdir_command });
    script::global_script_commands.push_back({ "echo", echo_command });
    script::global_script_commands.push_back({ "echo-create", echo_create_command });
    script::global_script_commands.push_back({ "cat", cat_command });
}

#endif // FIRMWARE_PIO_LITTLEFS_H_
