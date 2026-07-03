mkdir -p build

test -s build/OPIL_BR_2026-04-22.dsk || unzip ../demos/OPIL_BR_2026-04-22.dsk.zip -d build
test -s build/OPIL_EN_2026-04-22.dsk || unzip ../demos/OPIL_EN_2026-04-22.dsk.zip -d build

go build -x -o build/tether ./.
ls -l build/tether
