package main

import (
	"bufio"
	"fmt"
	"os"
	"strconv"
	"strings"
)

func main() {
	var mem [0x10000]byte

	rom1, err := os.ReadFile("misc/coco2.0x8000.rom")
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error reading misc/coco2.0x8000.rom: %v\n", err)
		os.Exit(1)
	}
	copy(mem[0x8000:], rom1)

	rom2, err := os.ReadFile("misc/disk11.0xC000.rom")
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error reading misc/disk11.0xC000.rom: %v\n", err)
		os.Exit(1)
	}
	copy(mem[0xC000:], rom2)

	logFile, err := os.Open("tether/_log")
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error reading tether/_log: %v\n", err)
		os.Exit(1)
	}
	defer logFile.Close()

	scanner := bufio.NewScanner(logFile)
	for scanner.Scan() {
		line := scanner.Text()
		if strings.HasPrefix(line, "<") && len(line) >= 8 && line[5] == ' ' {
			addrStr := line[1:5]
			dataStr := line[6:8]

			addr, err1 := strconv.ParseUint(addrStr, 16, 16)
			data, err2 := strconv.ParseUint(dataStr, 16, 8)

			if err1 == nil && err2 == nil {
				if mem[addr] == byte(data) {
					fmt.Printf("%s yes\n", line)
				} else {
					fmt.Printf("%s no %02x\n", line, mem[addr])
				}
			}
		}
	}
	if err := scanner.Err(); err != nil {
		fmt.Fprintf(os.Stderr, "Error reading log: %v\n", err)
	}
}
