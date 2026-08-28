set -ex

mkdir -p build
go build -x -o build/tether ./.
ls -l build/tether

test -s build/OPIL_BR_2026-04-22.dsk || unzip ../demos/OPIL_BR_2026-04-22.dsk.zip -d build
test -s build/OPIL_EN_2026-04-22.dsk || unzip ../demos/OPIL_EN_2026-04-22.dsk.zip -d build

while read EXE ENV
do
    echo $ENV go build -x -o build/$EXE ./. | sh -x
done << END
tether.linux-amd64.exe   GOOS=linux   GOARCH=amd64 
tether.linux-386.exe     GOOS=linux   GOARCH=386    
tether.linux-arm-7.exe   GOOS=linux   GOARCH=arm GOARM=7 
tether.linux-arm64.exe   GOOS=linux   GOARCH=arm64      
tether.win-amd64.exe     GOOS=windows GOARCH=amd64     
tether.win-386.exe       GOOS=windows GOARCH=386       
tether.mac-arm64.exe     GOOS=darwin   GOARCH=arm64   
tether.mac-amd64.exe     GOOS=darwin   GOARCH=amd64  
END
ls -l build/*.exe build/tether
