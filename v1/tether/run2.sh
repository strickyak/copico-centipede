echo "Verbose logging & errors are in '_log'" >&2

cp build/OPIL_BR_2026-04-22.dsk ../tmp/floppy0
hd build/OPIL_BR_2026-04-22.dsk > build/OPIL_BR_2026-04-22.dsk.hd

set -x
date
go run ./. \
    --borges $HOME/modoc/coco-shelf/listings/   \
      --curly_dec=0                             \
      --usb_verbose=0                            \
      --tether_log_usb=0                      \
      --pc=../firmware/pc/                   \
          "$@"  ../misc/*.rom  2>_log
exit $?

#    --disks=f0:../tmp/floppy0                   \
#      -usb_verbose                              \
#      -tether_log_usb                        \
#	################################
#	go run ./. \
#	    --borges $HOME/modoc/coco-shelf/listings/  \
#	    --disks=h0:/tmp/l1_coco.dsk,d0:/tmp/l1_coco.dsk  \
#	    --dos_hack \
#	        ../misc/*.rom  2>_log
#	exit $?
#	    # --n \
#	    # --abslists ../misc/diskbasic.0x8000.list  \
#	    # --no_modules \
#	    # --centipede  \
#	
#	
#	
#	
#	# You may replace `go run ./.` with one of the pre-compiled
#	# binaries in the build/ directory.
#	go run ./. \
#	    --centipede  \
#	    --disks=h0:/tmp/l1_coco.dsk,d0:/tmp/l1_coco.dsk  \
#	    --abslists ../misc/diskbasic.0x8000.list  \
#	    --no_modules \
#	    --dos_hack \
#	        ../misc/*.rom  2>_log
#	exit $?
#	    # --n \
#	    # --abslists ../misc/diskbasic.0x8000.list  \
#	    # --no_modules \
