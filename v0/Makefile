GO = go
GO_BUILD = $(GO) build -x
TETHER_SOURCE = $(shell find tether -type f -name \*.go)

all: generated _generate_misc_ _generate_tethers_

generated:
	mkdir -p generated

_generate_misc_ : \
	generated/roms.list \
	generated/coco2.0x8000.rom \
	generated/disk11.0xC000.rom \
	generated/OPIL-EN.dsk \
	generated/OPIL-PT-BR.dsk \
	##

generated/roms.list : generated
	cat demo/bas13.rom.list  demo/extbas11.rom.list demo/disk11.0xC000.rom.list > $@
generated/coco2.0x8000.rom : generated
	cp -vf demo/coco2.0x8000.rom generated/
generated/disk11.0xC000.rom : generated
	cp -vf demo/disk11.0xC000.rom generated/
generated/OPIL-EN.dsk : generated
	cp -vf demo/OPIL-EN.dsk generated/
generated/OPIL-PT-BR.dsk : generated
	cp -vf demo/OPIL-PT-BR.dsk generated/

_generate_tethers_ : \
    generated/tether.linux-amd64.exe \
    generated/tether.linux-386.exe \
    generated/tether.linux-arm-7.exe \
    generated/tether.linux-arm64.exe \
    generated/tether.win-amd64.exe \
    generated/tether.win-386.exe \
    generated/tether.mac-arm64.exe \
    generated/tether.mac-amd64.exe \
    ##

generated/tether.linux-amd64.exe : generated $(TETHER_SOURCE)
	GOOS=linux GOARCH=amd64       $(GO_BUILD) -o $@ ./tether
generated/tether.linux-386.exe : generated $(TETHER_SOURCE)
	GOOS=linux GOARCH=386         $(GO_BUILD) -o $@ ./tether
generated/tether.linux-arm-7.exe : generated $(TETHER_SOURCE)
	GOOS=linux GOARCH=arm GOARM=7 $(GO_BUILD) -o $@ ./tether
generated/tether.linux-arm64.exe : generated $(TETHER_SOURCE)
	GOOS=linux GOARCH=arm64       $(GO_BUILD) -o $@ ./tether
generated/tether.win-amd64.exe : generated $(TETHER_SOURCE)
	GOOS=windows GOARCH=amd64     $(GO_BUILD) -o $@ ./tether
generated/tether.win-386.exe : generated $(TETHER_SOURCE)
	GOOS=windows GOARCH=386       $(GO_BUILD) -o $@ ./tether
generated/tether.mac-arm64.exe : generated $(TETHER_SOURCE)
	GOOS=darwin GOARCH=arm64      $(GO_BUILD) -o $@ ./tether
generated/tether.mac-amd64.exe : generated $(TETHER_SOURCE)
	GOOS=darwin GOARCH=amd64      $(GO_BUILD) -o $@ ./tether

clean:
	rm -rf generated
