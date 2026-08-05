#ifndef FIRMWARE_H_
#define FIRMWARE_H_

namespace config {

    struct CentipedeConfig {
        bool    ram_64k;
        bool    rom_disk11;
        bool    floppy_emulation;
        bool    trace_writes;
        bool    trace_reads;
    };
}

#endif //// FIRMWARE_H_
