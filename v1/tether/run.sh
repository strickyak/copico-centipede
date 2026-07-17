echo "Verbose logging & errors are in '_log'" >&2

# You may replace `go run ./.` with one of the pre-compiled
# binaries in the build/ directory.
go run ./. \
    --centipede  \
    --disks=f0:build/OPIL_BR_2026-04-22.dsk \
    --no_modules \
    --abslists ../misc/diskbasic.0x8000.list  \
    ../misc/*.rom  \
    2>_log

    # --n \
