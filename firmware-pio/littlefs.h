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
}

#endif // FIRMWARE_PIO_LITTLEFS_H_
