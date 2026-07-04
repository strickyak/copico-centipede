import sys

opcodes_page1 = [None]*256
opcodes_page2 = [None]*256
opcodes_page3 = [None]*256

def set_op(page, hexop, name, mode, bytes, cycles, codes):
    if type(hexop) == str:
        hexop = int(hexop, 16)
    op = {"name": name, "mode": mode, "bytes": bytes, "cycles": cycles, "codes": codes}
    if page == 1: opcodes_page1[hexop] = op
    elif page == 2: opcodes_page2[hexop] = op
    elif page == 3: opcodes_page3[hexop] = op

def ALU8_new(page, base_op, name):
    b = "o" if page == 1 else "oo"
    set_op(page, base_op + 0x00, name, "Immediate8", 2 if page==1 else 3, 2 if page==1 else 3, b+"p")
    set_op(page, base_op + 0x10, name, "Direct",     2 if page==1 else 3, 4 if page==1 else 5, b+"p-r")
    set_op(page, base_op + 0x20, name, "Indexed",    2 if page==1 else 3, 4 if page==1 else 5, b+"p-r")
    set_op(page, base_op + 0x30, name, "Extended",   3 if page==1 else 4, 5 if page==1 else 6, b+"pp-r")

def ALU16_new(page, base_op, name):
    b = "o" if page == 1 else "oo"
    set_op(page, base_op + 0x00, name, "Immediate16", 3 if page==1 else 4, 3 if page==1 else 4, b+"pp")
    set_op(page, base_op + 0x10, name, "Direct",      2 if page==1 else 3, 5 if page==1 else 6, b+"p-rr")
    set_op(page, base_op + 0x20, name, "Indexed",     2 if page==1 else 3, 5 if page==1 else 6, b+"p-rr")
    set_op(page, base_op + 0x30, name, "Extended",    3 if page==1 else 4, 6 if page==1 else 7, b+"pp-rr")

def STORE8_new(page, base_op, name):
    b = "o" if page == 1 else "oo"
    set_op(page, base_op + 0x10, name, "Direct",      2 if page==1 else 3, 4 if page==1 else 5, b+"p-w")
    set_op(page, base_op + 0x20, name, "Indexed",     2 if page==1 else 3, 4 if page==1 else 5, b+"p-w")
    set_op(page, base_op + 0x30, name, "Extended",    3 if page==1 else 4, 5 if page==1 else 6, b+"pp-w")

def STORE16_new(page, base_op, name):
    b = "o" if page == 1 else "oo"
    set_op(page, base_op + 0x10, name, "Direct",      2 if page==1 else 3, 5 if page==1 else 6, b+"p-ww")
    set_op(page, base_op + 0x20, name, "Indexed",     2 if page==1 else 3, 5 if page==1 else 6, b+"p-ww")
    set_op(page, base_op + 0x30, name, "Extended",    3 if page==1 else 4, 6 if page==1 else 7, b+"pp-ww")

ALU8_new(1, 0x80, "SUBA")
ALU8_new(1, 0x81, "CMPA")
ALU8_new(1, 0x82, "SBCA")
ALU16_new(1, 0x83, "SUBD")
ALU8_new(1, 0x84, "ANDA")
ALU8_new(1, 0x85, "BITA")
ALU8_new(1, 0x86, "LDA")
STORE8_new(1, 0x87, "STA")
ALU8_new(1, 0x88, "EORA")
ALU8_new(1, 0x89, "ADCA")
ALU8_new(1, 0x8A, "ORA")
ALU8_new(1, 0x8B, "ADDA")
ALU16_new(1, 0x8C, "CMPX")
set_op(1, 0x8D, "BSR", "Relative8", 2, 7, "op--ww-")
set_op(1, 0x8E, "LDX", "Immediate16", 3, 3, "opp")
set_op(1, 0x9E, "LDX", "Direct", 2, 5, "op-rr")
set_op(1, 0xAE, "LDX", "Indexed", 2, 5, "op-rr")
set_op(1, 0xBE, "LDX", "Extended", 3, 6, "opp-rr")
STORE16_new(1, 0x8F, "STX")

ALU8_new(1, 0xC0, "SUBB")
ALU8_new(1, 0xC1, "CMPB")
ALU8_new(1, 0xC2, "SBCB")
ALU16_new(1, 0xC3, "ADDD")
ALU8_new(1, 0xC4, "ANDB")
ALU8_new(1, 0xC5, "BITB")
ALU8_new(1, 0xC6, "LDB")
STORE8_new(1, 0xC7, "STB")
ALU8_new(1, 0xC8, "EORB")
ALU8_new(1, 0xC9, "ADCB")
ALU8_new(1, 0xCA, "ORB")
ALU8_new(1, 0xCB, "ADDB")
ALU16_new(1, 0xCC, "LDD")
STORE16_new(1, 0xCD, "STD")
ALU16_new(1, 0xCE, "LDU")
STORE16_new(1, 0xCF, "STU")

def MEM_OP(page, base_op, name):
    b = "o" if page == 1 else "oo"
    set_op(page, base_op + 0x00, name, "Direct",      2 if page==1 else 3, 6 if page==1 else 7, b+"p-r-w")
    set_op(page, base_op + 0x40, name+"A", "Inherent",1 if page==1 else 2, 2 if page==1 else 3, b+"-")
    set_op(page, base_op + 0x50, name+"B", "Inherent",1 if page==1 else 2, 2 if page==1 else 3, b+"-")
    set_op(page, base_op + 0x60, name, "Indexed",     2 if page==1 else 3, 6 if page==1 else 7, b+"p-r-w")
    set_op(page, base_op + 0x70, name, "Extended",    3 if page==1 else 4, 7 if page==1 else 8, b+"pp-r-w")

MEM_OP(1, 0x00, "NEG")
MEM_OP(1, 0x03, "COM")
MEM_OP(1, 0x04, "LSR")
MEM_OP(1, 0x06, "ROR")
MEM_OP(1, 0x07, "ASR")
MEM_OP(1, 0x08, "ASL")
MEM_OP(1, 0x09, "ROL")
MEM_OP(1, 0x0A, "DEC")
MEM_OP(1, 0x0C, "INC")

for offset in [0x00, 0x60, 0x70]:
    b = "o"
    if offset == 0x70:
        set_op(1, 0x0D + offset, "TST", "Extended", 3, 6, "opp-r-")
    elif offset == 0x00:
        set_op(1, 0x0D + offset, "TST", "Direct", 2, 6, "op-r-")
    else:
        set_op(1, 0x0D + offset, "TST", "Indexed", 2, 6, "op-r-")
set_op(1, 0x4D, "TSTA", "Inherent", 1, 2, "o-")
set_op(1, 0x5D, "TSTB", "Inherent", 1, 2, "o-")

MEM_OP(1, 0x0F, "CLR")

def LEA_OP(page, base_op, name):
    b = "o" if page == 1 else "oo"
    set_op(page, base_op, name, "Indexed", 2 if page==1 else 3, 4 if page==1 else 5, b+"p--") 

LEA_OP(1, 0x30, "LEAX")
LEA_OP(1, 0x31, "LEAY")
LEA_OP(1, 0x32, "LEAS")
LEA_OP(1, 0x33, "LEAU")

branches = [
    (0x20, "BRA"), (0x21, "BRN"), (0x22, "BHI"), (0x23, "BLS"),
    (0x24, "BCC"), (0x25, "BCS"), (0x26, "BNE"), (0x27, "BEQ"),
    (0x28, "BVC"), (0x29, "BVS"), (0x2A, "BPL"), (0x2B, "BMI"),
    (0x2C, "BGE"), (0x2D, "BLT"), (0x2E, "BGT"), (0x2F, "BLE")
]
for op, name in branches:
    set_op(1, op, name, "Relative8", 2, 3, "op-")
    set_op(2, op, "L"+name, "Relative16", 4, 6, "oopp--")

ALU16_new(2, 0x83, "CMPD")
ALU16_new(2, 0x8C, "CMPY")
set_op(2, 0x8E, "LDY", "Immediate16", 4, 4, "oopp")
set_op(2, 0x9E, "LDY", "Direct", 3, 6, "oop-rr")
set_op(2, 0xAE, "LDY", "Indexed", 3, 6, "oop-rr")
set_op(2, 0xBE, "LDY", "Extended", 4, 7, "oopp-rr")
STORE16_new(2, 0x8F, "STY")
set_op(2, 0xCE, "LDS", "Immediate16", 4, 4, "oopp")
set_op(2, 0xDE, "LDS", "Direct", 3, 6, "oop-rr")
set_op(2, 0xEE, "LDS", "Indexed", 3, 6, "oop-rr")
set_op(2, 0xFE, "LDS", "Extended", 4, 7, "oopp-rr")
STORE16_new(2, 0xCF, "STS")

ALU16_new(3, 0x83, "CMPU")
ALU16_new(3, 0x8C, "CMPS")

set_op(1, 0x12, "NOP", "Inherent", 1, 2, "o-")
set_op(1, 0x13, "SYNC", "Inherent", 1, 2, "o-") 
set_op(1, 0x16, "LBRA", "Relative16", 3, 5, "opp--") 
set_op(1, 0x17, "LBSR", "Relative16", 3, 9, "opp--ww--") 
set_op(1, 0x19, "DAA", "Inherent", 1, 2, "o-")
set_op(1, 0x1A, "ORCC", "Immediate8", 2, 3, "op-")
set_op(1, 0x1C, "ANDCC", "Immediate8", 2, 3, "op-")
set_op(1, 0x1D, "SEX", "Inherent", 1, 2, "o-")
set_op(1, 0x1E, "EXG", "ExchangeTransfer", 2, 8, "op------")
set_op(1, 0x1F, "TFR", "ExchangeTransfer", 2, 6, "op----")

set_op(1, 0x34, "PSHS", "PushPull", 2, 5, "op---")
set_op(1, 0x35, "PULS", "PushPull", 2, 5, "op---")
set_op(1, 0x36, "PSHU", "PushPull", 2, 5, "op---")
set_op(1, 0x37, "PULU", "PushPull", 2, 5, "op---")

set_op(1, 0x39, "RTS", "Inherent", 1, 5, "o-rr-")
set_op(1, 0x3B, "RTI", "Inherent", 1, 15, "o-rrrrrrrrrrrr-")
set_op(1, 0x3C, "CWAI", "Immediate8", 2, 20, "op-wwwwwwwwwwww-----")
set_op(1, 0x3D, "MUL", "Inherent", 1, 11, "o----------")
set_op(1, 0x3F, "SWI", "Inherent", 1, 19, "o-wwwwwwwwwwww-----")

set_op(2, 0x3F, "SWI2", "Inherent", 2, 20, "oo-wwwwwwwwwwww-----")
set_op(3, 0x3F, "SWI3", "Inherent", 2, 20, "oo-wwwwwwwwwwww-----")

set_op(1, 0x0E, "JMP", "Direct", 2, 3, "op-")
set_op(1, 0x6E, "JMP", "Indexed", 2, 3, "op-")
set_op(1, 0x7E, "JMP", "Extended", 3, 4, "opp-")
set_op(1, 0x9D, "JSR", "Direct", 2, 7, "op--ww-")
set_op(1, 0xAD, "JSR", "Indexed", 2, 7, "op--ww-")
set_op(1, 0xBD, "JSR", "Extended", 3, 8, "opp--ww-")


go_code = """package lib

import (
    "fmt"
    "strings"
)

type AddrMode int

const (
    Inherent AddrMode = iota
    Immediate8
    Immediate16
    Direct
    Extended
    Indexed
    Relative8
    Relative16
    PushPull
    ExchangeTransfer
)

type OpInfo struct {
    Name       string
    Mode       AddrMode
    BaseBytes  int
    BaseCycles int
    BaseCodes  string
    Valid      bool
}

var opcodesPage1 [256]OpInfo
var opcodesPage2 [256]OpInfo
var opcodesPage3 [256]OpInfo

func init() {
"""

def gen_init(page, ops):
    global go_code
    name = f"opcodesPage{page}"
    for i, op in enumerate(ops):
        if op:
            go_code += f'    {name}[0x{i:02X}] = OpInfo{{"{op["name"]}", {op["mode"]}, {op["bytes"]}, {op["cycles"]}, "{op["codes"]}", true}}\n'

gen_init(1, opcodes_page1)
gen_init(2, opcodes_page2)
gen_init(3, opcodes_page3)

go_code += """
}

func Decode(mem []byte) (disasm string, numBytes int, numCycles int, cycleCodes string, ok bool) {
    if len(mem) == 0 {
        return "", 0, 0, "", false
    }

    opByte := mem[0]
    page := 1
    offset := 1

    var table *[256]OpInfo
    if opByte == 0x10 {
        page = 2
        if len(mem) < 2 {
            return "", 0, 0, "", false
        }
        opByte = mem[1]
        offset = 2
        table = &opcodesPage2
    } else if opByte == 0x11 {
        page = 3
        if len(mem) < 2 {
            return "", 0, 0, "", false
        }
        opByte = mem[1]
        offset = 2
        table = &opcodesPage3
    } else {
        table = &opcodesPage1
    }

    info := table[opByte]
    if !info.Valid {
        return "", 0, 0, "", false
    }

    numBytes = info.BaseBytes
    numCycles = info.BaseCycles
    cycleCodes = info.BaseCodes
    
    if len(mem) < numBytes && info.Mode != Indexed && info.Mode != PushPull {
        return "", 0, 0, "", false
    }

    switch info.Mode {
    case Inherent:
        disasm = info.Name
    case Immediate8:
        val := mem[offset]
        disasm = fmt.Sprintf("%s #$%02X", info.Name, val)
    case Immediate16:
        val := (uint16(mem[offset]) << 8) | uint16(mem[offset+1])
        disasm = fmt.Sprintf("%s #$%04X", info.Name, val)
    case Direct:
        val := mem[offset]
        disasm = fmt.Sprintf("%s <$%02X", info.Name, val)
    case Extended:
        val := (uint16(mem[offset]) << 8) | uint16(mem[offset+1])
        disasm = fmt.Sprintf("%s >$%04X", info.Name, val)
    case Relative8:
        val := int8(mem[offset])
        disasm = fmt.Sprintf("%s %d", info.Name, val)
    case Relative16:
        val := int16((uint16(mem[offset]) << 8) | uint16(mem[offset+1]))
        disasm = fmt.Sprintf("%s %d", info.Name, val)
    case ExchangeTransfer:
        pb := mem[offset]
        r1 := getRegName((pb >> 4) & 0xF)
        r2 := getRegName(pb & 0xF)
        disasm = fmt.Sprintf("%s %s,%s", info.Name, r1, r2)
    case PushPull:
        if len(mem) < numBytes {
            return "", 0, 0, "", false
        }
        pb := mem[offset]
        regs := []string{}
        bits := []string{"CC", "A", "B", "DP", "X", "Y", "U_S", "PC"}
        if info.Name == "PSHU" || info.Name == "PULU" {
            bits[6] = "S"
        } else {
            bits[6] = "U"
        }
        
        count := 0
        for i := 0; i < 8; i++ {
            if (pb & (1 << i)) != 0 {
                regs = append(regs, bits[i])
                if bits[i] == "X" || bits[i] == "Y" || bits[i] == "U_S" || bits[i] == "U" || bits[i] == "S" || bits[i] == "PC" {
                    count += 2
                } else {
                    count++
                }
            }
        }
        
        disasm = fmt.Sprintf("%s %s", info.Name, strings.Join(regs, ","))
        numCycles += count
        
        char := "w"
        if info.Name == "PULS" || info.Name == "PULU" {
            char = "r"
        }
        if len(cycleCodes) > 0 {
            // cycle string op--- should become op---www
            // wait, we just append count times of r/w, and subtract one -
            // let's do: baseCodes is op---
            // We'll replace the last '-' with string
            if len(cycleCodes) > 0 {
                cycleCodes = cycleCodes[:len(cycleCodes)-1] + strings.Repeat(char, count) + "-"
            }
        }
    case Indexed:
        if len(mem) < offset+1 {
            return "", 0, 0, "", false
        }
        pb := mem[offset]
        idxDisasm, extraBytes, extraCycles, extraCodes := decodeIndexed(pb, mem, offset)
        if extraBytes < 0 { return "", 0, 0, "", false }
        disasm = fmt.Sprintf("%s %s", info.Name, idxDisasm)
        numBytes += extraBytes
        numCycles += extraCycles
        cycleCodes = insertIndexedCodes(cycleCodes, extraCodes, page)
    }

    if len(mem) < numBytes {
        return "", 0, 0, "", false
    }

    return disasm, numBytes, numCycles, cycleCodes, true
}

func getRegName(r uint8) string {
    switch r {
    case 0: return "D"
    case 1: return "X"
    case 2: return "Y"
    case 3: return "U"
    case 4: return "S"
    case 5: return "PC"
    case 8: return "A"
    case 9: return "B"
    case 10: return "CC"
    case 11: return "DP"
    default: return "INV"
    }
}

func decodeIndexed(pb byte, mem []byte, offset int) (string, int, int, string) {
    regBits := (pb >> 5) & 3
    var reg string
    switch regBits {
    case 0: reg = "X"
    case 1: reg = "Y"
    case 2: reg = "U"
    case 3: reg = "S"
    }

    if (pb & 0x80) == 0 {
        val := int8(pb & 0x1F)
        if val > 15 { val -= 32 }
        return fmt.Sprintf("%d,%s", val, reg), 0, 1, "-"
    }

    indirect := (pb & 0x10) != 0
    mod := pb & 0x0F
    
    extraBytes := 0
    extraCycles := 0
    extraCodes := ""
    disasm := ""

    switch mod {
    case 0x00: // ,R+
        disasm = fmt.Sprintf(",%s+", reg)
        extraCycles = 2
        extraCodes = "--"
    case 0x01: // ,R++
        disasm = fmt.Sprintf(",%s++", reg)
        extraCycles = 3
        extraCodes = "---"
    case 0x02: // ,-R
        disasm = fmt.Sprintf(",-%s", reg)
        extraCycles = 2
        extraCodes = "--"
    case 0x03: // ,--R
        disasm = fmt.Sprintf(",--%s", reg)
        extraCycles = 3
        extraCodes = "---"
    case 0x04: // ,R
        disasm = fmt.Sprintf(",%s", reg)
        extraCycles = 0
        extraCodes = ""
    case 0x05: // B,R
        disasm = fmt.Sprintf("B,%s", reg)
        extraCycles = 1
        extraCodes = "-"
    case 0x06: // A,R
        disasm = fmt.Sprintf("A,%s", reg)
        extraCycles = 1
        extraCodes = "-"
    case 0x08: // 8-bit offset
        extraBytes = 1
        if len(mem) < offset+2 { return "", -1, 0, "" }
        val := int8(mem[offset+1])
        disasm = fmt.Sprintf("%d,%s", val, reg)
        extraCycles = 1
        extraCodes = "p-"
    case 0x09: // 16-bit offset
        extraBytes = 2
        if len(mem) < offset+3 { return "", -1, 0, "" }
        val := int16((uint16(mem[offset+1]) << 8) | uint16(mem[offset+2]))
        disasm = fmt.Sprintf("%d,%s", val, reg)
        extraCycles = 4
        extraCodes = "pp--"
    case 0x0B: // D,R
        disasm = fmt.Sprintf("D,%s", reg)
        extraCycles = 4
        extraCodes = "----"
    case 0x0C: // 8-bit PCR
        extraBytes = 1
        if len(mem) < offset+2 { return "", -1, 0, "" }
        val := int8(mem[offset+1])
        disasm = fmt.Sprintf("%d,PCR", val)
        extraCycles = 1
        extraCodes = "p-"
    case 0x0D: // 16-bit PCR
        extraBytes = 2
        if len(mem) < offset+3 { return "", -1, 0, "" }
        val := int16((uint16(mem[offset+1]) << 8) | uint16(mem[offset+2]))
        disasm = fmt.Sprintf("%d,PCR", val)
        extraCycles = 5
        extraCodes = "pp---"
    case 0x0F: // Extended indirect
        extraBytes = 2
        if len(mem) < offset+3 { return "", -1, 0, "" }
        val := uint16((uint16(mem[offset+1]) << 8) | uint16(mem[offset+2]))
        disasm = fmt.Sprintf(">$%04X", val)
        extraCycles = 1
        extraCodes = "pp-"
    default:
        return "", -1, 0, ""
    }

    if indirect {
        if mod == 0x00 || mod == 0x02 {
            return "", -1, 0, ""
        }
        disasm = "[" + disasm + "]"
        extraCycles += 3
        extraCodes += "rr-"
    }

    return disasm, extraBytes, extraCycles, extraCodes
}

func insertIndexedCodes(base string, extra string, page int) string {
    idx := 2
    if page > 1 {
        idx = 3
    }
    if len(base) >= idx {
        return base[:idx] + extra + base[idx:]
    }
    return base + extra
}
"""

with open("lib/decode_m6809e.go", "w") as f:
    f.write(go_code)
