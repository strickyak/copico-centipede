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
    lfs_dir_t dir;
    int err = lfs_dir_open(&lfs_volume, &dir, "/");
    if (err) {
        return "Failed to open root directory";
    }
    
    struct lfs_info info;
    while (true) {
        int res = lfs_dir_read(&lfs_volume, &dir, &info);
        if (res < 0) {
            lfs_dir_close(&lfs_volume, &dir);
            return "Error reading directory";
        }
        if (res == 0) {
            break;
        }
        if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) {
            continue;
        }
        printf("%s\n", info.name);
    }
    
    lfs_dir_close(&lfs_volume, &dir);
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
    script::global_script_commands.push_back({ "echo", echo_command });
}

#endif // FIRMWARE_PIO_LITTLEFS_H_
