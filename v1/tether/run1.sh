echo "Verbose logging & errors are in '_log'" >&2

date
go run ./. \
    --borges $HOME/modoc/coco-shelf/listings/  \
    --disks=f0:build/OPIL_BR_2026-04-22.dsk    \
      -usb_verbose                             \
        ../misc/*.rom  2>_log
exit $?

################################
go run ./. \
    --borges $HOME/modoc/coco-shelf/listings/  \
    --disks=h0:/tmp/l1_coco.dsk,d0:/tmp/l1_coco.dsk  \
    --dos_hack \
        ../misc/*.rom  2>_log
exit $?
    # --n \
    # --abslists ../misc/diskbasic.0x8000.list  \
    # --no_modules \
    # --centipede  \




# You may replace `go run ./.` with one of the pre-compiled
# binaries in the build/ directory.
go run ./. \
    --centipede  \
    --disks=h0:/tmp/l1_coco.dsk,d0:/tmp/l1_coco.dsk  \
    --abslists ../misc/diskbasic.0x8000.list  \
    --no_modules \
    --dos_hack \
        ../misc/*.rom  2>_log
exit $?
    # --n \
    # --abslists ../misc/diskbasic.0x8000.list  \
    # --no_modules \
