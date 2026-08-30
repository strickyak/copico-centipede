# rc/mode1.tcl -- executed on restart with Coco on and no keys down.

menu clear
menu fetch Config
menu array Config
menu title "HOME" ;# unused

menu template {
A | ==== Centipede Config ====
B |
C |   [1] Inject 64kB RAM
D | 
E |   [2] Inject Disk Basic Rom "disk11"
F |
G |   [3] Floppy Disks /fd/floppy<N>.dsk
G |   [8] Floppy Disks /pc/floppy<N>.dsk
H |
I |   [4] Trace Memory Write Cycles
K |   [5] Trace Memory Read Cycles
L |
Q |   [9] Launch!
R |
M |   Designed and tested on a Coco2
N |          with 16kB built-in RAM.
P |
S | Use UP/DOWN to navigate.
T | Use SPACE to toggle or execute.
}
menu check    1 ram_64k
menu check    2 rom_disk11
menu check    3 floppy_fd
menu check    8 floppy_pc
menu check    4 trace_writes
menu check    5 trace_reads
menu at-most-one {3 8}

menu action   9 {menu save-and-exit}
menu render
menu store Config
bye
