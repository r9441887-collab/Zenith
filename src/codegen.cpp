#include "codegen.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <set>

#pragma pack(push, 1)

struct DOSHeader {
    uint16_t e_magic = 0x5A4D;
    uint16_t e_cblp = 0x90;
    uint16_t e_cp = 3;
    uint16_t e_crlc = 0;
    uint16_t e_cparhdr = 4;
    uint16_t e_minalloc = 0;
    uint16_t e_maxalloc = 0xFFFF;
    uint16_t e_ss = 0;
    uint16_t e_sp = 0xB8;
    uint16_t e_csum = 0;
    uint16_t e_ip = 0;
    uint16_t e_cs = 0;
    uint16_t e_lfarlc = 0x40;
    uint16_t e_ovno = 0;
    uint16_t e_res[4] = {0};
    uint16_t e_oemid = 0;
    uint16_t e_oeminfo = 0;
    uint16_t e_res2[10] = {0};
    uint32_t e_lfanew = 0x80;
};

struct IMAGE_FILE_HEADER {
    uint16_t Machine = 0x8664;
    uint16_t NumberOfSections = 3;
    uint32_t TimeDateStamp = 0;
    uint32_t PointerToSymbolTable = 0;
    uint32_t NumberOfSymbols = 0;
    uint16_t SizeOfOptionalHeader = 240;
    uint16_t Characteristics = 0x0023;
};

struct IMAGE_DATA_DIRECTORY {
    uint32_t VirtualAddress;
    uint32_t Size;
};

struct IMAGE_OPTIONAL_HEADER64 {
    uint16_t Magic = 0x020B;
    uint8_t MajorLinkerVersion = 0;
    uint8_t MinorLinkerVersion = 0;
    uint32_t SizeOfCode = 0;
    uint32_t SizeOfInitializedData = 0;
    uint32_t SizeOfUninitializedData = 0;
    uint32_t AddressOfEntryPoint = 0;
    uint32_t BaseOfCode = 0x1000;
    uint64_t ImageBase = 0x140000000;
    uint32_t SectionAlignment = 0x1000;
    uint32_t FileAlignment = 0x200;
    uint16_t MajorOperatingSystemVersion = 6;
    uint16_t MinorOperatingSystemVersion = 0;
    uint16_t MajorImageVersion = 0;
    uint16_t MinorImageVersion = 0;
    uint16_t MajorSubsystemVersion = 6;
    uint16_t MinorSubsystemVersion = 0;
    uint32_t Win32VersionValue = 0;
    uint32_t SizeOfImage = 0x4000;
    uint32_t SizeOfHeaders = 0x200;
    uint32_t CheckSum = 0;
    uint16_t Subsystem = 0;
    uint16_t DllCharacteristics = 0x0160;
    uint64_t SizeOfStackReserve = 0x100000;
    uint64_t SizeOfStackCommit = 0x1000;
    uint64_t SizeOfHeapReserve = 0x100000;
    uint64_t SizeOfHeapCommit = 0x1000;
    uint32_t LoaderFlags = 0;
    uint32_t NumberOfRvaAndSizes = 16;
    IMAGE_DATA_DIRECTORY DataDirectory[16] = {};
};

struct IMAGE_SECTION_HEADER {
    char Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations = 0;
    uint32_t PointerToLinenumbers = 0;
    uint16_t NumberOfRelocations = 0;
    uint16_t NumberOfLinenumbers = 0;
    uint32_t Characteristics;
};

#pragma pack(pop)

Codegen::Codegen(Program& prog) : prog(prog) {}

void Codegen::emit8(uint8_t b) { code.push_back(b); }

void Codegen::emit16(uint16_t v) {
    code.push_back(v & 0xFF);
    code.push_back((v >> 8) & 0xFF);
}

void Codegen::emit32(uint32_t v) {
    code.push_back(v & 0xFF);
    code.push_back((v >> 8) & 0xFF);
    code.push_back((v >> 16) & 0xFF);
    code.push_back((v >> 24) & 0xFF);
}

void Codegen::emit64(uint64_t v) {
    emit32((uint32_t)(v & 0xFFFFFFFF));
    emit32((uint32_t)((v >> 32) & 0xFFFFFFFF));
}

int Codegen::allocReg() {
    for (int i = 0; i < 4; i++) {
        if (!(regsUsed & (1 << i))) {
            regsUsed |= (1 << i);
            return i;
        }
    }
    return -1;
}

void Codegen::freeReg(int r) {
    if (r >= 0) regsUsed &= ~(1 << r);
}

int Codegen::allocXmmReg() {
    for (int i = 0; i < 8; i++) {
        if (!(xmmRegsUsed & (1 << i))) {
            xmmRegsUsed |= (1 << i);
            return i;
        }
    }
    return -1;
}

void Codegen::freeXmmReg(int r) {
    if (r >= 0) xmmRegsUsed &= ~(1 << r);
}

void Codegen::emitMovReg(int dst, int src) {
    if (dst == src) return;
    if (src == 0 && dst == 1) { emit8(0x48); emit8(0x8B); emit8(0xC8); }
    else if (src == 0 && dst == 2) { emit8(0x48); emit8(0x8B); emit8(0xD0); }
    else if (src == 0 && dst == 3) { emit8(0x48); emit8(0x8B); emit8(0xD8); }
    else if (src == 1 && dst == 0) { emit8(0x48); emit8(0x8B); emit8(0xC1); }
    else if (src == 2 && dst == 0) { emit8(0x48); emit8(0x8B); emit8(0xC2); }
    else if (src == 3 && dst == 0) { emit8(0x48); emit8(0x8B); emit8(0xC3); }
    else if (src == 1 && dst == 2) { emit8(0x48); emit8(0x8B); emit8(0xD1); }
    else if (src == 2 && dst == 1) { emit8(0x48); emit8(0x8B); emit8(0xCA); }
    else if (src == 1 && dst == 3) { emit8(0x48); emit8(0x8B); emit8(0xD9); }
    else if (src == 2 && dst == 3) { emit8(0x48); emit8(0x8B); emit8(0xDA); }
    else if (src == 3 && dst == 1) { emit8(0x48); emit8(0x8B); emit8(0xCB); }
    else if (src == 3 && dst == 2) { emit8(0x48); emit8(0x8B); emit8(0xD3); }
    else {
        if (src == 3) { emit8(0x53); } // push rbx
        else { emit8(0x50 + src); }
        if (dst == 3) { emit8(0x5B); } // pop rbx
        else { emit8(0x58 + dst); }
    }
}

void Codegen::emitMovRegImm(int r, uint32_t val) {
    if (val <= 0x7FFFFFFF) {
        // Positive value: mov r32, imm32 (zero-extends to 64-bit)
        if (r == 0) { emit8(0xB8); emit32(val); }
        else if (r == 1) { emit8(0xB9); emit32(val); }
        else if (r == 2) { emit8(0xBA); emit32(val); }
        else if (r == 3) { emit8(0xBB); emit32(val); }
        else if (r == 4) { emit8(0xBC); emit32(val); }
        else if (r == 5) { emit8(0xBD); emit32(val); }
        else if (r == 6) { emit8(0xBE); emit32(val); }
        else if (r == 7) { emit8(0xBF); emit32(val); }
    } else {
        // Negative value: mov r64, imm64 (sign-extends via full 64-bit immediate)
        int64_t sval = (int64_t)(int32_t)val;
        uint8_t rex = 0x48 | (r >> 3);
        emit8(rex); emit8(0xB8 + (r & 7));
        emit32((uint32_t)(sval & 0xFFFFFFFF));
        emit32((uint32_t)((sval >> 32) & 0xFFFFFFFF));
    }
}

void Codegen::emitLoadRegFromBP(int r, int offset) {
    uint8_t rm;
    if (r == 0) rm = 0x45;
    else if (r == 1) rm = 0x4D;
    else if (r == 2) rm = 0x55;
    else if (r == 3) rm = 0x5D;
    else return;
    if (offset >= -128 && offset <= 127) {
        emit8(0x8B); emit8(rm); emit8((uint8_t)(int8_t)offset);
    } else {
        emit8(0x8B); emit8((rm & 0x3F) | 0x80); emit32((uint32_t)(int32_t)offset);
    }
}

void Codegen::emitStoreToBP(int offset) {
    if (offset >= -128 && offset <= 127) {
        emit8(0x89); emit8(0x45); emit8((uint8_t)(int8_t)offset);
    } else {
        emit8(0x89); emit8(0x85); emit32((uint32_t)(int32_t)offset);
    }
}

void Codegen::emitStoreRegToBP(int r, int offset) {
    uint8_t rm;
    if (r == 0) rm = 0x45;
    else if (r == 1) rm = 0x4D;
    else if (r == 2) rm = 0x55;
    else if (r == 3) rm = 0x5D;
    else return;
    if (offset >= -128 && offset <= 127) {
        emit8(0x89); emit8(rm); emit8((uint8_t)(int8_t)offset);
    } else {
        emit8(0x89); emit8((rm & 0x3F) | 0x80); emit32((uint32_t)(int32_t)offset);
    }
}

void Codegen::emitStoreToBP64(int offset) {
    if (offset >= -128 && offset <= 127) {
        emit8(0x48); emit8(0x89); emit8(0x45); emit8((uint8_t)(int8_t)offset);
    } else {
        emit8(0x48); emit8(0x89); emit8(0x85); emit32((uint32_t)(int32_t)offset);
    }
}

void Codegen::emitLoadRegFromBP64(int r, int offset) {
    uint8_t rex = 0x48;
    uint8_t rm;
    if (r == 0) rm = 0x45;
    else if (r == 1) rm = 0x4D;
    else if (r == 2) rm = 0x55;
    else if (r == 3) rm = 0x5D;
    else return;
    if (offset >= -128 && offset <= 127) {
        emit8(rex); emit8(0x8B); emit8(rm); emit8((uint8_t)(int8_t)offset);
    } else {
        emit8(rex); emit8(0x8B); emit8((rm & 0x3F) | 0x80); emit32((uint32_t)(int32_t)offset);
    }
}

void Codegen::emitAdd(int dst, int src) {
    if (dst == 0 && src == 1) { emit8(0x48); emit8(0x03); emit8(0xC1); }
    else if (dst == 0 && src == 2) { emit8(0x48); emit8(0x03); emit8(0xC2); }
    else if (dst == 0 && src == 3) { emit8(0x48); emit8(0x03); emit8(0xC3); }
    else if (dst == 1 && src == 0) { emit8(0x48); emit8(0x03); emit8(0xC8); }
    else if (dst == 2 && src == 0) { emit8(0x48); emit8(0x03); emit8(0xD0); }
    else if (dst == 3 && src == 0) { emit8(0x48); emit8(0x03); emit8(0xD8); }
    else if (dst == 1 && src == 2) { emit8(0x48); emit8(0x01); emit8(0xD1); }
    else if (dst == 2 && src == 1) { emit8(0x48); emit8(0x01); emit8(0xCA); }
    else if (dst == 1 && src == 3) { emit8(0x48); emit8(0x01); emit8(0xD9); }
    else if (dst == 2 && src == 3) { emit8(0x48); emit8(0x01); emit8(0xDA); }
    else if (dst == 3 && src == 1) { emit8(0x48); emit8(0x01); emit8(0xCB); }
    else if (dst == 3 && src == 2) { emit8(0x48); emit8(0x01); emit8(0xD3); }
    else if (dst == 3 && src == 3) { emit8(0x48); emit8(0x01); emit8(0xDB); }
    else { emit8(0x48); emit8(0x01); emit8(0xC0 + dst + src * 8); }
}

void Codegen::emitSub(int dst, int src) {
    if (dst == 0 && src == 1) { emit8(0x48); emit8(0x2B); emit8(0xC1); }
    else if (dst == 0 && src == 2) { emit8(0x48); emit8(0x2B); emit8(0xC2); }
    else if (dst == 0 && src == 3) { emit8(0x48); emit8(0x2B); emit8(0xC3); }
    else if (dst == 1 && src == 0) { emit8(0x48); emit8(0x2B); emit8(0xC8); }
    else if (dst == 1 && src == 3) { emit8(0x48); emit8(0x2B); emit8(0xCB); }
    else if (dst == 2 && src == 0) { emit8(0x48); emit8(0x2B); emit8(0xD0); }
    else if (dst == 2 && src == 3) { emit8(0x48); emit8(0x2B); emit8(0xD3); }
    else if (dst == 3 && src == 0) { emit8(0x48); emit8(0x2B); emit8(0xD8); }
    else if (dst == 3 && src == 1) { emit8(0x48); emit8(0x2B); emit8(0xD9); }
    else if (dst == 3 && src == 2) { emit8(0x48); emit8(0x2B); emit8(0xDA); }
    else if (dst == 3 && src == 3) { emit8(0x48); emit8(0x2B); emit8(0xDB); }
    else { emit8(0x48); emit8(0x2B); emit8(0xC0 + src + dst * 8); }
}

void Codegen::emitImul(int dst, int src) {
    if (dst == 0 && src == 1) { emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xC1); }
    else if (dst == 0 && src == 2) { emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xC2); }
    else if (dst == 0 && src == 3) { emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xC3); }
    else if (dst == 1 && src == 0) { emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xC8); }
    else if (dst == 1 && src == 3) { emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xCB); }
    else if (dst == 2 && src == 0) { emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xD0); }
    else if (dst == 2 && src == 3) { emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xD3); }
    else if (dst == 3 && src == 0) { emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xD8); }
    else if (dst == 3 && src == 1) { emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xD9); }
    else if (dst == 3 && src == 2) { emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xDA); }
    else if (dst == 3 && src == 3) { emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xDB); }
    else { emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xC0 + src + dst * 8); }
}

// ============== SSE Float Instructions ==============

void Codegen::emitMovssXmm(int xmmDst, int xmmSrc) {
    // movss xmmDst, xmmSrc: F3 0F 10 /r (dst = dst reg, src = r/m)
    // We encode: movss xmmDst, xmmSrc (register to register)
    uint8_t rex = 0x41; // REX prefix for extended registers
    bool rexNeeded = (xmmDst >= 8) || (xmmSrc >= 8);
    uint8_t modrm = 0xC0 | (xmmDst << 3) | xmmSrc;
    if (xmmDst < 8 && xmmSrc < 8) {
        emit8(0xF3); emit8(0x0F); emit8(0x10); emit8(modrm);
    } else {
        uint8_t rexByte = 0x40;
        if (xmmDst >= 8) rexByte |= 0x04;
        if (xmmSrc >= 8) rexByte |= 0x01;
        emit8(0xF3); emit8(rexByte); emit8(0x0F); emit8(0x10);
        emit8(0xC0 | ((xmmDst & 0x07) << 3) | (xmmSrc & 0x07));
    }
}

void Codegen::emitMovssXmmFromMem(int xmmDst, int gpReg, int offset) {
    uint8_t rex = 0x40;
    if (xmmDst >= 8) rex |= 0x04;
    uint8_t modrm;
    if (gpReg == 4) {
        emit8(0xF3); if (rex != 0x40) emit8(rex); emit8(0x0F); emit8(0x10);
        if (offset == 0) {
            emit8(0x04 | (xmmDst << 3)); emit8(0x24);
        } else if (offset >= -128 && offset <= 127) {
            emit8(0x44 | (xmmDst << 3)); emit8(0x24); emit8((uint8_t)(int8_t)offset);
        } else {
            emit8(0x84 | (xmmDst << 3)); emit8(0x24); emit32((uint32_t)(int32_t)offset);
        }
        return;
    }
    if (gpReg == 5) {
        if (offset == 0) {
            emit8(0xF3); if (rex != 0x40) emit8(rex); emit8(0x0F); emit8(0x10);
            emit8(0x45 | (xmmDst << 3)); emit8(0x00);
        } else if (offset >= -128 && offset <= 127) {
            emit8(0xF3); if (rex != 0x40) emit8(rex); emit8(0x0F); emit8(0x10);
            emit8(0x45 | (xmmDst << 3)); emit8((uint8_t)(int8_t)offset);
        } else {
            emit8(0xF3); if (rex != 0x40) emit8(rex); emit8(0x0F); emit8(0x10);
            emit8(0x85 | (xmmDst << 3)); emit32((uint32_t)(int32_t)offset);
        }
        return;
    }
    uint8_t regEnc = gpReg;
    modrm = (xmmDst << 3) | regEnc;
    if (offset == 0 && gpReg != 5) {
        emit8(0xF3); if (rex != 0x40) emit8(rex); emit8(0x0F); emit8(0x10); emit8(modrm);
    } else if (offset >= -128 && offset <= 127) {
        emit8(0xF3); if (rex != 0x40) emit8(rex); emit8(0x0F); emit8(0x10); emit8(modrm | 0x40); emit8((uint8_t)(int8_t)offset);
    } else {
        emit8(0xF3); if (rex != 0x40) emit8(rex); emit8(0x0F); emit8(0x10); emit8(modrm | 0x80); emit32((uint32_t)(int32_t)offset);
    }
}

void Codegen::emitMovssXmmToMem(int xmmDst, int gpReg, int offset) {
    uint8_t rex = 0x40;
    if (xmmDst >= 8) rex |= 0x04;
    uint8_t modrm;
    if (gpReg == 4) {
        emit8(0xF3); if (rex != 0x40) emit8(rex); emit8(0x0F); emit8(0x11);
        if (offset == 0) {
            emit8(0x04 | (xmmDst << 3)); emit8(0x24);
        } else if (offset >= -128 && offset <= 127) {
            emit8(0x44 | (xmmDst << 3)); emit8(0x24); emit8((uint8_t)(int8_t)offset);
        } else {
            emit8(0x84 | (xmmDst << 3)); emit8(0x24); emit32((uint32_t)(int32_t)offset);
        }
        return;
    }
    if (gpReg == 5) {
        if (offset == 0) {
            emit8(0xF3); if (rex != 0x40) emit8(rex); emit8(0x0F); emit8(0x11);
            emit8(0x45 | (xmmDst << 3)); emit8(0x00);
        } else if (offset >= -128 && offset <= 127) {
            emit8(0xF3); if (rex != 0x40) emit8(rex); emit8(0x0F); emit8(0x11);
            emit8(0x45 | (xmmDst << 3)); emit8((uint8_t)(int8_t)offset);
        } else {
            emit8(0xF3); if (rex != 0x40) emit8(rex); emit8(0x0F); emit8(0x11);
            emit8(0x85 | (xmmDst << 3)); emit32((uint32_t)(int32_t)offset);
        }
        return;
    }
    uint8_t regEnc = gpReg;
    modrm = (xmmDst << 3) | regEnc;
    if (offset == 0 && gpReg != 5) {
        emit8(0xF3); if (rex != 0x40) emit8(rex); emit8(0x0F); emit8(0x11); emit8(modrm);
    } else if (offset >= -128 && offset <= 127) {
        emit8(0xF3); if (rex != 0x40) emit8(rex); emit8(0x0F); emit8(0x11); emit8(modrm | 0x40); emit8((uint8_t)(int8_t)offset);
    } else {
        emit8(0xF3); if (rex != 0x40) emit8(rex); emit8(0x0F); emit8(0x11); emit8(modrm | 0x80); emit32((uint32_t)(int32_t)offset);
    }
}

void Codegen::emitAddss(int xmmDst, int xmmSrc) {
    // addss xmmDst, xmmSrc: F3 0F 58 /r
    uint8_t modrm = 0xC0 | (xmmDst << 3) | xmmSrc;
    if (xmmDst < 8 && xmmSrc < 8) {
        emit8(0xF3); emit8(0x0F); emit8(0x58); emit8(modrm);
    } else {
        uint8_t rex = 0x40;
        if (xmmDst >= 8) rex |= 0x04;
        if (xmmSrc >= 8) rex |= 0x01;
        emit8(0xF3); emit8(rex); emit8(0x0F); emit8(0x58); emit8(0xC0 | (xmmDst & 0x7) << 3 | (xmmSrc & 0x7));
    }
}

void Codegen::emitSubss(int xmmDst, int xmmSrc) {
    uint8_t modrm = 0xC0 | (xmmDst << 3) | xmmSrc;
    if (xmmDst < 8 && xmmSrc < 8) {
        emit8(0xF3); emit8(0x0F); emit8(0x5C); emit8(modrm);
    } else {
        uint8_t rex = 0x40;
        if (xmmDst >= 8) rex |= 0x04;
        if (xmmSrc >= 8) rex |= 0x01;
        emit8(0xF3); emit8(rex); emit8(0x0F); emit8(0x5C); emit8(0xC0 | (xmmDst & 0x7) << 3 | (xmmSrc & 0x7));
    }
}

void Codegen::emitMulss(int xmmDst, int xmmSrc) {
    uint8_t modrm = 0xC0 | (xmmDst << 3) | xmmSrc;
    if (xmmDst < 8 && xmmSrc < 8) {
        emit8(0xF3); emit8(0x0F); emit8(0x59); emit8(modrm);
    } else {
        uint8_t rex = 0x40;
        if (xmmDst >= 8) rex |= 0x04;
        if (xmmSrc >= 8) rex |= 0x01;
        emit8(0xF3); emit8(rex); emit8(0x0F); emit8(0x59); emit8(0xC0 | (xmmDst & 0x7) << 3 | (xmmSrc & 0x7));
    }
}

void Codegen::emitDivss(int xmmDst, int xmmSrc) {
    uint8_t modrm = 0xC0 | (xmmDst << 3) | xmmSrc;
    if (xmmDst < 8 && xmmSrc < 8) {
        emit8(0xF3); emit8(0x0F); emit8(0x5E); emit8(modrm);
    } else {
        uint8_t rex = 0x40;
        if (xmmDst >= 8) rex |= 0x04;
        if (xmmSrc >= 8) rex |= 0x01;
        emit8(0xF3); emit8(rex); emit8(0x0F); emit8(0x5E); emit8(0xC0 | (xmmDst & 0x7) << 3 | (xmmSrc & 0x7));
    }
}

void Codegen::emitUcomiss(int xmmA, int xmmB) {
    // ucomiss xmmA, xmmB: 0F 2E /r
    uint8_t modrm = 0xC0 | (xmmA << 3) | xmmB;
    if (xmmA < 8 && xmmB < 8) {
        emit8(0x0F); emit8(0x2E); emit8(modrm);
    } else {
        uint8_t rex = 0x40;
        if (xmmA >= 8) rex |= 0x04;
        if (xmmB >= 8) rex |= 0x01;
        emit8(rex); emit8(0x0F); emit8(0x2E); emit8(0xC0 | (xmmA & 0x7) << 3 | (xmmB & 0x7));
    }
}

void Codegen::emitCvtsi2ss(int xmmDst, int gpSrc) {
    // cvtsi2ss xmmDst, r64: F3 REX.W 0F 2A /r (64-bit)
    uint8_t modrm = 0xC0 | (xmmDst << 3) | gpSrc;
    if (xmmDst < 8 && gpSrc < 8) {
        emit8(0xF3); emit8(0x48); emit8(0x0F); emit8(0x2A); emit8(modrm);
    } else {
        uint8_t rex = 0x48;
        if (xmmDst >= 8) rex |= 0x04;
        if (gpSrc >= 8) rex |= 0x01;
        emit8(0xF3); emit8(rex); emit8(0x0F); emit8(0x2A); emit8(0xC0 | (xmmDst & 0x7) << 3 | (gpSrc & 0x7));
    }
}

void Codegen::emitMovdGpFromXmm(int gpDst, int xmmSrc) {
    // movd gp, xmm: 66 REX.W 0F 7E /r (copy raw float bits to gp)
    uint8_t regField = gpDst & 0x7;
    uint8_t xmmField = xmmSrc & 0x7;
    uint8_t modrm = 0xC0 | (regField << 3) | xmmField;
    uint8_t rex = 0x48;
    if (gpDst >= 8) rex |= 0x04;
    if (xmmSrc >= 8) rex |= 0x01;
    emit8(0x66); emit8(rex); emit8(0x0F); emit8(0x7E); emit8(modrm);
}

void Codegen::emitCvtss2si(int gpDst, int xmmSrc) {
    // cvtss2si r64, xmmSrc: F3 REX.W 0F 2D /r (64-bit)
    uint8_t modrm = 0xC0 | (gpDst << 3) | xmmSrc;
    if (gpDst < 8 && xmmSrc < 8) {
        emit8(0xF3); emit8(0x48); emit8(0x0F); emit8(0x2D); emit8(modrm);
    } else {
        uint8_t rex = 0x48;
        if (gpDst >= 8) rex |= 0x04;
        if (xmmSrc >= 8) rex |= 0x01;
        emit8(0xF3); emit8(rex); emit8(0x0F); emit8(0x2D); emit8(0xC0 | (gpDst & 0x7) << 3 | (xmmSrc & 0x7));
    }
}

void Codegen::emitMovssXmmImm(int xmmDst, float val) {
    // Load float immediate into XMM via memory
    // We'll push the constant into .rdata and use movss from there
    // For simplicity, use cvtsi2ss if val is a whole number, or push constant to stack
    // Better: put float constant in .rdata and reference via RIP-relative
    // But for code simplicity, convert int to float for now
    int intVal = (int)val;
    if ((float)intVal == val) {
        // Load integer into eax, then convert to float
        int saved = regsUsed;
        regsUsed = 0;
        int r = allocReg();
        emitMovRegImm(r, (uint32_t)intVal);
        emitCvtsi2ss(xmmDst, r);
        freeReg(r);
        regsUsed = (uint8_t)saved;
    } else {
        // Push constant value to stack, then movss from stack
        int32_t intBits;
        memcpy(&intBits, &val, sizeof(int32_t));
        // sub rsp, 4
        emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x04);
        // mov dword [rsp], intBits
        emit8(0xC7); emit8(0x04); emit8(0x24); emit32((uint32_t)intBits);
        // movss xmmDst, [rsp]
        emitMovssXmmFromMem(xmmDst, 4, 0);
        // add rsp, 4
        emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x04);
    }
}

// ============== Labels ==============

int Codegen::newLabel() { return nextLabel++; }

void Codegen::emitLabel(int label) {
    size_t pos = code.size();
    if (pos >= labelPositions.size()) labelPositions.resize(pos + 1, -1);
    if ((size_t)label >= labelPositions.size()) labelPositions.resize(label + 1, -1);
    labelPositions[label] = (int)pos;
}

void Codegen::emitJmp(int label) {
    emit8(0xE9);
    jmpFixups.push_back({(int)code.size(), label});
    emit32(0);
}

void Codegen::emitJcc(const std::string& cond, int label) {
    uint8_t jccOp;
    if (cond == "==") jccOp = 0x84;
    else if (cond == "!=") jccOp = 0x85;
    else if (cond == "<")  jccOp = 0x8C;
    else if (cond == ">")  jccOp = 0x8F;
    else if (cond == "<=") jccOp = 0x8E;
    else if (cond == ">=") jccOp = 0x8D;
    else jccOp = 0x84;

    emit8(0x0F); emit8(jccOp);
    jmpFixups.push_back({(int)code.size(), label});
    emit32(0);
}

VarInfo* Codegen::getVarInfo(const std::string& name) {
    auto it = varInfos.find(name);
    if (it != varInfos.end()) return &it->second;
    return nullptr;
}

bool Codegen::isFloatExpr(Expr* expr) {
    if (dynamic_cast<FloatExpr*>(expr)) return true;
    if (auto n = dynamic_cast<NumberExpr*>(expr)) return false;
    if (auto id = dynamic_cast<IdentExpr*>(expr)) {
        auto vi = getVarInfo(id->name);
        return vi && vi->type.kind == TypeKind::Float;
    }
    if (auto memb = dynamic_cast<MemberExpr*>(expr)) {
        if (auto objId = dynamic_cast<IdentExpr*>(memb->object.get())) {
            auto vi = getVarInfo(objId->name);
            if (vi && vi->type.kind == TypeKind::Struct) {
                auto slIt = structLayouts.find(vi->type.structName);
                if (slIt != structLayouts.end()) {
                    auto fTypeIt = slIt->second.fieldTypes.find(memb->member);
                    return fTypeIt != slIt->second.fieldTypes.end() && fTypeIt->second.kind == TypeKind::Float;
                }
            }
        }
        return false;
    }
    if (auto bin = dynamic_cast<BinaryExpr*>(expr)) {
        return isFloatExpr(bin->left.get()) || isFloatExpr(bin->right.get());
    }
    if (auto arr = dynamic_cast<ArrayAccessExpr*>(expr)) {
        if (auto objId = dynamic_cast<IdentExpr*>(arr->array.get())) {
            auto vi = getVarInfo(objId->name);
            return vi && vi->type.kind == TypeKind::Float;
        }
        return false;
    }
    return false;
}

void Codegen::emitFloatStoreToBP(int xmm, int offset) {
    if (offset >= -128 && offset <= 127) {
        emit8(0xF3); emit8(0x0F); emit8(0x11);
        emit8(0x45 | (xmm << 3));
        emit8((uint8_t)(int8_t)offset);
    } else {
        emit8(0xF3); emit8(0x0F); emit8(0x11);
        emit8(0x85 | (xmm << 3));
        emit32((uint32_t)(int32_t)offset);
    }
}

void Codegen::emitFloatLoadFromBP(int xmm, int offset) {
    if (offset >= -128 && offset <= 127) {
        emit8(0xF3); emit8(0x0F); emit8(0x10);
        emit8(0x45 | (xmm << 3));
        emit8((uint8_t)(int8_t)offset);
    } else {
        emit8(0xF3); emit8(0x0F); emit8(0x10);
        emit8(0x85 | (xmm << 3));
        emit32((uint32_t)(int32_t)offset);
    }
}

void Codegen::emitLeaR10FromBP(int offset) {
    emit8(0x4C); emit8(0x8D);
    if (offset >= -128 && offset <= 127) {
        emit8(0x55); emit8((uint8_t)(int8_t)offset);
    } else {
        emit8(0x95); emit32((uint32_t)(int32_t)offset);
    }
}

void Codegen::emitLoadFromAddr(int gpDst, int addrReg, int offset) {
    if (offset == 0) {
        if (addrReg == 5) {
            if (gpDst == 0) { emit8(0x48); emit8(0x8B); emit8(0x45); emit8(0x00); }
            else if (gpDst == 1) { emit8(0x48); emit8(0x8B); emit8(0x4D); emit8(0x00); }
            else if (gpDst == 2) { emit8(0x48); emit8(0x8B); emit8(0x55); emit8(0x00); }
            else if (gpDst == 3) { emit8(0x48); emit8(0x8B); emit8(0x5D); emit8(0x00); }
        } else {
            if (gpDst == 0) { emit8(0x48); emit8(0x8B); emit8(0x00 + addrReg); }
            else if (gpDst == 1) { emit8(0x48); emit8(0x8B); emit8(0x08 + addrReg); }
            else if (gpDst == 2) { emit8(0x48); emit8(0x8B); emit8(0x10 + addrReg); }
            else if (gpDst == 3) { emit8(0x48); emit8(0x8B); emit8(0x18 + addrReg); }
        }
    } else if (offset >= -128 && offset <= 127) {
        uint8_t base = 0x40 | (addrReg & 7);
        uint8_t modrm = 0x40 | ((gpDst & 3) << 3) | (addrReg & 7);
        if (addrReg == 5) { modrm = 0x45 | ((gpDst & 3) << 3); }
        else if (addrReg == 4) { emit8(0x48); emit8(0x8B); emit8(modrm); emit8(0x24); emit8((uint8_t)(int8_t)offset); return; }
        emit8(0x48); emit8(0x8B); emit8(modrm); emit8((uint8_t)(int8_t)offset);
    } else {
        uint8_t modrm = 0x80 | ((gpDst & 3) << 3) | (addrReg & 7);
        if (addrReg == 5) { modrm = 0x85 | ((gpDst & 3) << 3); }
        else if (addrReg == 4) { emit8(0x48); emit8(0x8B); emit8(modrm); emit8(0x24); emit32((uint32_t)(int32_t)offset); return; }
        emit8(0x48); emit8(0x8B); emit8(modrm); emit32((uint32_t)(int32_t)offset);
    }
}

void Codegen::emitStoreToAddr(int gpSrc, int addrReg, int offset) {
    if (offset == 0) {
        if (addrReg == 5) {
            if (gpSrc == 0) { emit8(0x48); emit8(0x89); emit8(0x45); emit8(0x00); }
            else if (gpSrc == 1) { emit8(0x48); emit8(0x89); emit8(0x4D); emit8(0x00); }
            else if (gpSrc == 2) { emit8(0x48); emit8(0x89); emit8(0x55); emit8(0x00); }
            else if (gpSrc == 3) { emit8(0x48); emit8(0x89); emit8(0x5D); emit8(0x00); }
        } else {
            if (gpSrc == 0) { emit8(0x48); emit8(0x89); emit8(0x00 + addrReg); }
            else if (gpSrc == 1) { emit8(0x48); emit8(0x89); emit8(0x08 + addrReg); }
            else if (gpSrc == 2) { emit8(0x48); emit8(0x89); emit8(0x10 + addrReg); }
            else if (gpSrc == 3) { emit8(0x48); emit8(0x89); emit8(0x18 + addrReg); }
        }
    } else if (offset >= -128 && offset <= 127) {
        uint8_t modrm = 0x40 | ((gpSrc & 3) << 3) | (addrReg & 7);
        if (addrReg == 5) { modrm = 0x45 | ((gpSrc & 3) << 3); }
        else if (addrReg == 4) { emit8(0x48); emit8(0x89); emit8(modrm); emit8(0x24); emit8((uint8_t)(int8_t)offset); return; }
        emit8(0x48); emit8(0x89); emit8(modrm); emit8((uint8_t)(int8_t)offset);
    } else {
        uint8_t modrm = 0x80 | ((gpSrc & 3) << 3) | (addrReg & 7);
        if (addrReg == 5) { modrm = 0x85 | ((gpSrc & 3) << 3); }
        else if (addrReg == 4) { emit8(0x48); emit8(0x89); emit8(modrm); emit8(0x24); emit32((uint32_t)(int32_t)offset); return; }
        emit8(0x48); emit8(0x89); emit8(modrm); emit32((uint32_t)(int32_t)offset);
    }
}

void Codegen::emitLoadQwordDisp8(int dstReg, int baseReg, int disp) {
    // ModRM: mod=01 (disp8), reg=dstReg, rm=baseReg
    emit8(0x48); emit8(0x8B);
    emit8((uint8_t)(0x40 | (dstReg << 3) | baseReg));
    emit8((uint8_t)disp);
}

void Codegen::emitStoreQwordDisp8(int srcReg, int baseReg, int disp) {
    // ModRM: mod=01 (disp8), reg=srcReg, rm=baseReg
    emit8(0x48); emit8(0x89);
    emit8((uint8_t)(0x40 | (srcReg << 3) | baseReg));
    emit8((uint8_t)disp);
}

void Codegen::emitIncQwordDisp8(int baseReg, int disp) {
    emit8(0x48); emit8(0xFF);
    emit8((uint8_t)(0x40 | baseReg));  // mod=01, /0, rm=baseReg
    emit8((uint8_t)disp);
}

void Codegen::emitMovQwordDisp8Imm32(int baseReg, int disp, int32_t imm) {
    // MOV r/m64, imm32 (sign-extended): REX.W + C7 /0
    emit8(0x48); emit8(0xC7);
    emit8((uint8_t)(0x40 | baseReg));  // mod=01, /0, rm=baseReg
    emit8((uint8_t)disp);
    emit32((uint32_t)imm);
}

int Codegen::emitFloatExpr(Expr* expr) {
    if (auto f = dynamic_cast<FloatExpr*>(expr)) {
        int x = allocXmmReg();
        if (x < 0) x = 0;
        emitMovssXmmImm(x, (float)f->value);
        return x;
    }
    if (auto n = dynamic_cast<NumberExpr*>(expr)) {
        int x = allocXmmReg();
        if (x < 0) x = 0;
        int saved = regsUsed;
        regsUsed = 0;
        int r = allocReg();
        emitMovRegImm(r, (uint32_t)(n->value & 0xFFFFFFFF));
        emitCvtsi2ss(x, r);
        freeReg(r);
        regsUsed = (uint8_t)saved;
        return x;
    }
    auto emitMovssXmmFromBP = [this](int xmmDst, int offset) {
        if (offset >= -128 && offset <= 127) {
            emit8(0xF3); emit8(0x0F); emit8(0x10);
            emit8(0x45 | (xmmDst << 3));
            emit8((uint8_t)(int8_t)offset);
        } else {
            emit8(0xF3); emit8(0x0F); emit8(0x10);
            emit8(0x85 | (xmmDst << 3));
            emit32((uint32_t)(int32_t)offset);
        }
    };
    if (auto id = dynamic_cast<IdentExpr*>(expr)) {
        auto vi = getVarInfo(id->name);
        if (vi) {
            int x = allocXmmReg();
            if (x < 0) x = 0;
            emitMovssXmmFromBP(x, vi->offset);
            return x;
        }
        int x = allocXmmReg(); if (x < 0) x = 0;
        emitMovssXmmImm(x, 0.0f);
        return x;
    }
    if (auto memb = dynamic_cast<MemberExpr*>(expr)) {
        if (auto objId = dynamic_cast<IdentExpr*>(memb->object.get())) {
            auto vi = getVarInfo(objId->name);
            if (vi) {
                auto slIt = structLayouts.find(vi->type.structName);
                if (slIt != structLayouts.end()) {
                    auto& layout = slIt->second;
                    auto fIt = layout.fieldOffsets.find(memb->member);
                    if (fIt != layout.fieldOffsets.end()) {
                        int x = allocXmmReg(); if (x < 0) x = 0;
                        int totalOffset = vi->offset + fIt->second;
                        emitMovssXmmFromBP(x, totalOffset);
                        return x;
                    }
                }
            }
        }
    }
    if (auto bin = dynamic_cast<BinaryExpr*>(expr)) {
        return emitBinaryExpr(bin, true);
    }
    int x = allocXmmReg(); if (x < 0) x = 0;
    emitMovssXmmImm(x, 0.0f);
    return x;
}

int Codegen::emitBinaryExpr(BinaryExpr* bin, bool isFloat) {
    if (!isFloat) {
        int leftReg = emitExpr(bin->left.get());
        int tempReg = allocReg();
        if (tempReg < 0) {
            emit8(0x50 + leftReg);
            int rightReg = emitExpr(bin->right.get());
            int popReg = allocReg();
            if (popReg < 0) {
                popReg = (rightReg == 0) ? 1 : 0;
            }
            emit8(0x58 + popReg);
            if (bin->op == "+") {
                emitAdd(popReg, rightReg);
                freeReg(rightReg);
                return popReg;
            } else if (bin->op == "-") {
                emitSub(popReg, rightReg);
                freeReg(rightReg);
                return popReg;
            } else if (bin->op == "*") {
                emitImul(popReg, rightReg);
                freeReg(rightReg);
                return popReg;
            } else if (bin->op == "/") {
                // idiv rax by rcx: rax = left, rcx = right, rdx = 0
                if (rightReg == 0) { emit8(0x50); emit8(0x59); }
                else if (rightReg == 1) { emit8(0x51); emit8(0x59); }
                else if (rightReg == 2) { emit8(0x52); emit8(0x59); }
                else if (rightReg == 3) { emit8(0x53); emit8(0x59); }
                else { emit8(0x50); emit8(0x59); }
                emitMovReg(0, popReg);
                emit8(0x48); emit8(0x99);  // cqo
                emit8(0x48); emit8(0xF7); emit8(0xF9);  // idiv rcx
                freeReg(popReg);
                freeReg(rightReg);
                regsUsed = 1; // only RAX live
                return 0;
            } else if (bin->op == "==" || bin->op == "!=" ||
                       bin->op == "<"  || bin->op == ">"  ||
                       bin->op == "<=" || bin->op == ">=") {
                emit8(0x48); emit8(0x39); emit8(0xC0 + popReg + rightReg * 8);
                emitMovRegImm(rightReg, 0);
                int endLabel = newLabel();
                if (bin->op == "==") { emit8(0x0F); emit8(0x85); }
                else if (bin->op == "!=") { emit8(0x0F); emit8(0x84); }
                else if (bin->op == "<") { emit8(0x0F); emit8(0x8D); }
                else if (bin->op == ">") { emit8(0x0F); emit8(0x8E); }
                else if (bin->op == "<=") { emit8(0x0F); emit8(0x8F); }
                else if (bin->op == ">=") { emit8(0x0F); emit8(0x8C); }
                jmpFixups.push_back({(int)code.size(), endLabel});
                emit32(0);
                emitMovRegImm(rightReg, 1);
                emitLabel(endLabel);
                freeReg(popReg);
                return rightReg;
            } else {
                freeReg(popReg);
                freeReg(rightReg);
                return rightReg;
            }
        }
        emitMovReg(tempReg, leftReg);
        freeReg(leftReg);
        int rightReg = emitExpr(bin->right.get());

        if (bin->op == "+") {
            emitAdd(rightReg, tempReg);
            freeReg(tempReg);
            return rightReg;
        } else if (bin->op == "-") {
            emitSub(tempReg, rightReg);
            emitMovReg(rightReg, tempReg);
            freeReg(tempReg);
            return rightReg;
        } else if (bin->op == "*") {
            emitImul(rightReg, tempReg);
            freeReg(tempReg);
            return rightReg;
        } else if (bin->op == "/") {
            if (rightReg == 0) emit8(0x50);
            else if (rightReg == 1) emit8(0x51);
            else if (rightReg == 2) emit8(0x52);
            else if (rightReg == 3) emit8(0x53);
            emitMovReg(0, tempReg);
            emit8(0x48); emit8(0x99);  // cqo (sign-extend RAX to RDX:RAX, 64-bit)
            emit8(0x59);  // pop rcx
            emit8(0x48); emit8(0xF7); emit8(0xF9);  // idiv rcx (64-bit signed divide)
            freeReg(rightReg);
            freeReg(tempReg);
            return 0;
        } else if (bin->op == "==" || bin->op == "!=" ||
                   bin->op == "<"  || bin->op == ">"  ||
                   bin->op == "<=" || bin->op == ">=") {
            emit8(0x48); emit8(0x39); emit8(0xC0 + tempReg + rightReg * 8);
            emitMovRegImm(rightReg, 0);
            int endLabel = newLabel();
            if (bin->op == "==") { emit8(0x0F); emit8(0x85); }
            else if (bin->op == "!=") { emit8(0x0F); emit8(0x84); }
            else if (bin->op == "<") { emit8(0x0F); emit8(0x8D); }
            else if (bin->op == ">") { emit8(0x0F); emit8(0x8E); }
            else if (bin->op == "<=") { emit8(0x0F); emit8(0x8F); }
            else if (bin->op == ">=") { emit8(0x0F); emit8(0x8C); }
            jmpFixups.push_back({(int)code.size(), endLabel});
            emit32(0);
            emitMovRegImm(rightReg, 1);
            emitLabel(endLabel);
            freeReg(tempReg);
            return rightReg;
        } else {
            freeReg(tempReg);
            return rightReg;
        }
    } else {
        int leftXmm = emitFloatExpr(bin->left.get());
        int rightXmm = emitFloatExpr(bin->right.get());

        int resultXmm = rightXmm;
        if (bin->op == "+") {
            emitAddss(leftXmm, rightXmm);
            resultXmm = leftXmm;
            freeXmmReg(rightXmm);
        } else if (bin->op == "-") {
            int tempXmm = allocXmmReg();
            if (tempXmm < 0) {
                tempXmm = leftXmm;
                emitSubss(tempXmm, rightXmm);
                freeXmmReg(rightXmm);
            } else {
                emitMovssXmm(tempXmm, leftXmm);
                emitSubss(tempXmm, rightXmm);
                freeXmmReg(leftXmm);
                freeXmmReg(rightXmm);
            }
            resultXmm = tempXmm;
        } else if (bin->op == "*") {
            emitMulss(leftXmm, rightXmm);
            resultXmm = leftXmm;
            freeXmmReg(rightXmm);
        } else if (bin->op == "/") {
            int tempXmm = allocXmmReg();
            if (tempXmm < 0) {
                tempXmm = leftXmm;
                emitDivss(tempXmm, rightXmm);
                freeXmmReg(rightXmm);
            } else {
                emitMovssXmm(tempXmm, leftXmm);
                emitDivss(tempXmm, rightXmm);
                freeXmmReg(leftXmm);
                freeXmmReg(rightXmm);
            }
            resultXmm = tempXmm;
        } else if (bin->op == "==" || bin->op == "!=" ||
                   bin->op == "<"  || bin->op == ">"  ||
                   bin->op == "<=" || bin->op == ">=") {
            emitUcomiss(leftXmm, rightXmm);
            freeXmmReg(leftXmm);
            freeXmmReg(rightXmm);
            int r = allocReg();
            if (r < 0) r = 0;
        if (bin->op == "==") {
            emit8(0x0F); emit8(0x94); emit8(0xC0 + r);
            emit8(0x0F); emit8(0xB6); emit8(0xC0 + r);
        } else if (bin->op == "!=") {
            emit8(0x0F); emit8(0x95); emit8(0xC0 + r);
            emit8(0x0F); emit8(0xB6); emit8(0xC0 + r);
        } else if (bin->op == "<") {
            emit8(0x0F); emit8(0x92); emit8(0xC0 + r);
            emit8(0x0F); emit8(0xB6); emit8(0xC0 + r);
        } else if (bin->op == ">") {
            emit8(0x0F); emit8(0x97); emit8(0xC0 + r);
            emit8(0x0F); emit8(0xB6); emit8(0xC0 + r);
        } else if (bin->op == "<=") {
            emit8(0x0F); emit8(0x96); emit8(0xC0 + r);
            emit8(0x0F); emit8(0xB6); emit8(0xC0 + r);
        } else if (bin->op == ">=") {
            emit8(0x0F); emit8(0x93); emit8(0xC0 + r);
            emit8(0x0F); emit8(0xB6); emit8(0xC0 + r);
        }
            return r;
        }
        int r = allocReg(); if (r < 0) r = 0;
        emitCvtss2si(r, resultXmm);
        freeXmmReg(resultXmm);
        return r;
    }
}

// ============== Expression codegen ==============

int Codegen::emitExpr(Expr* expr) {
    if (auto num = dynamic_cast<NumberExpr*>(expr)) {
        int r = allocReg();
        if (r < 0) {
            emitMovRegImm(0, (uint32_t)(num->value & 0xFFFFFFFF));
            return 0;
        }
        emitMovRegImm(r, (uint32_t)(num->value & 0xFFFFFFFF));
        return r;
    }
    if (auto flt = dynamic_cast<FloatExpr*>(expr)) {
        int r = allocReg();
        if (r < 0) r = 0;
        float fval = (float)flt->value;
        uint32_t bits;
        memcpy(&bits, &fval, sizeof(bits));
        emitMovRegImm(r, bits);
        return r;
    }
    if (auto id = dynamic_cast<IdentExpr*>(expr)) {
        auto vi = getVarInfo(id->name);
        if (vi) {
            int r = allocReg();
            if (r < 0) r = 0;
            if (vi->type.kind == TypeKind::Float) {
                emitLoadRegFromBP(r, vi->offset);
            } else {
                emitLoadRegFromBP64(r, vi->offset);
            }
            return r;
        }
        // Check if it's a known function — emit function reference (pointer)
        bool isFunc = funcOffsets.count(id->name) > 0;
        if (!isFunc) {
            for (auto& f : prog.functions) {
                if (f->name == id->name && !f->isExtern) { isFunc = true; break; }
            }
        }
        if (isFunc) {
            emit8(0x48); emit8(0x8D); emit8(0x05);
            size_t fixupPos = code.size();
            emit32(0);
            funcRefFixups.push_back({fixupPos, id->name});
            // Value is already in rax (LEA result)
            return 0;
        }
        std::cerr << "Error: undefined variable '" << id->name << "'\n";
        int r = allocReg(); if (r < 0) r = 0;
        emitMovRegImm(r, 0);
        return r;
    }
    if (auto memb = dynamic_cast<MemberExpr*>(expr)) {
        if (auto objId = dynamic_cast<IdentExpr*>(memb->object.get())) {
            auto vi = getVarInfo(objId->name);
            if (vi) {
                auto slIt = structLayouts.find(vi->type.structName);
                if (slIt != structLayouts.end()) {
                    auto& layout = slIt->second;
                    auto fIt = layout.fieldOffsets.find(memb->member);
                    auto fTypeIt = layout.fieldTypes.find(memb->member);
                    if (fIt != layout.fieldOffsets.end()) {
                        bool isFloatField = (fTypeIt != layout.fieldTypes.end() && fTypeIt->second.kind == TypeKind::Float);
                        bool isBoolField = (fTypeIt != layout.fieldTypes.end() && fTypeIt->second.kind == TypeKind::Bool);
                if (isFloatField || isBoolField) {
                    int r = allocReg();
                    if (r < 0) r = 0;
                    int totalOffset = vi->offset + fIt->second;
                    emitLoadRegFromBP(r, totalOffset);
                    return r;
                }
                        int r = allocReg();
                        if (r < 0) r = 0;
                        emitLoadRegFromBP64(r, vi->offset + fIt->second);
                        return r;
                    }
                }
            }
        }
        int r = allocReg(); if (r < 0) r = 0;
        emitMovRegImm(r, 0);
        return r;
    }
    if (auto str = dynamic_cast<StringExpr*>(expr)) {
        int idx = -1;
        for (size_t i = 0; i < stringPool.size(); i++) {
            if (stringPool[i] == str->value) { idx = (int)i; break; }
        }
        if (idx < 0) {
            idx = (int)stringPool.size();
            stringPool.push_back(str->value);
        }
        int r = allocReg(); if (r < 0) r = 0;
        emit8(0x48);
        uint8_t modrm;
        if (r == 0) modrm = 0x05;
        else if (r == 1) modrm = 0x0D;
        else if (r == 2) modrm = 0x15;
        else modrm = 0x1D;
        emit8(modrm);
        size_t fixupPos = code.size();
        emit32(0);
        strFixups.push_back({fixupPos, idx});
        return r;
    }
    if (auto arr = dynamic_cast<ArrayAccessExpr*>(expr)) {
        auto objId = dynamic_cast<IdentExpr*>(arr->array.get());
        if (objId) {
            auto vi = getVarInfo(objId->name);
            if (vi) {
                int elementSize = (vi->type.kind == TypeKind::Float) ? 4 : 8;
                emitLeaR10FromBP(vi->offset);
                int idxReg = emitExpr(arr->index.get());
                if (idxReg != 0) { emitMovReg(0, idxReg); freeReg(idxReg); idxReg = 0; }
                emit8(0x48); emit8(0x69); emit8(0xC0); emit32(elementSize);
                emit8(0x49); emit8(0x01); emit8(0xC2);
                if (vi->type.kind == TypeKind::Float) {
                    freeReg(0);
                    int x = allocXmmReg(); if (x < 0) x = 0;
                    emit8(0xF3); emit8(0x41); emit8(0x0F); emit8(0x10); emit8(0x02);
                    int r = allocReg(); if (r < 0) r = 0;
                    emitCvtss2si(r, x);
                    freeXmmReg(x);
                    return r;
                } else {
                    emit8(0x49); emit8(0x8B); emit8(0x02);
                    freeReg(0);
                    int r = allocReg(); if (r != 0) { emitMovReg(r, 0); freeReg(0); }
                    return r >= 0 ? r : 0;
                }
            }
        }
        int r = allocReg(); if (r < 0) r = 0;
        emitMovRegImm(r, 0);
        return r;
    }
    if (auto bin = dynamic_cast<BinaryExpr*>(expr)) {
        return emitBinaryExpr(bin, false);
    }
    if (auto unary = dynamic_cast<UnaryExpr*>(expr)) {
        if (unary->op == "!") {
            int r = emitExpr(unary->operand.get());
            emit8(0x48); emit8(0x83); emit8(0xF0); emit8(0x01); // xor rax, 1
            return r;
        }
        if (unary->op == "-") {
            int r = emitExpr(unary->operand.get());
            emit8(0x48); emit8(0xF7); emit8(0xD8 | (r & 7)); // neg reg
            return r;
        }
        return emitExpr(unary->operand.get());
    }
    if (auto call = dynamic_cast<CallExpr*>(expr)) {
        if (call->name == "alloc" && call->args.size() == 1) {
            int saved = regsUsed;
            regsUsed = 0;
            int sizeReg = emitExpr(call->args[0].get());
            if (sizeReg != 1) { emitMovReg(1, sizeReg); freeReg(sizeReg); sizeReg = 1; }
            emit8(0x48); emit8(0x8D); emit8(0x05);
            heapFixups.push_back({code.size(), heapAreaRVA}); emit32(0);
            emit8(0x48); emit8(0x8B); emit8(0x15);
            heapFixups.push_back({code.size(), heapOffsetRVA}); emit32(0);
            emit8(0x49); emit8(0x89); emit8(0xC8);
            emit8(0x48); emit8(0x03); emit8(0xCA);
            emit8(0x48); emit8(0x81); emit8(0xF9);
            emit32(64 * 1024);
            int failLabel = newLabel();
            emit8(0x0F); emit8(0x87);
            jmpFixups.push_back({(int)code.size(), failLabel});
            emit32(0);
            emit8(0x48); emit8(0x89); emit8(0x0D);
            heapFixups.push_back({code.size(), heapOffsetRVA}); emit32(0);
            emit8(0x48); emit8(0x03); emit8(0xC2);
            int doneLabel = newLabel();
            emitJmp(doneLabel);
            emitLabel(failLabel);
            emit8(0x48); emit8(0x31); emit8(0xC0);
            emitLabel(doneLabel);
            freeReg(1); freeReg(2);
            regsUsed = (uint8_t)saved;
            int r = allocReg(); if (r != 0) { emitMovReg(r, 0); freeReg(0); }
            return r >= 0 ? r : 0;
        }
        if (call->name == "free" && call->args.size() == 1) {
            int r = emitExpr(call->args[0].get());
            freeReg(r);
            regsUsed = 0;
            int result = allocReg(); if (result < 0) result = 0;
            emitMovRegImm(result, 0);
            return result;
        }

        // ============== Arena Allocator Builtins ==============
        // Arena header: [capacity:8][used:8], data at handle+16

        auto emitHeapAlloc = [this](int sizeReg) {
            if (sizeReg != 1) { emitMovReg(1, sizeReg); freeReg(sizeReg); }
            // rcx = size
            emitMovReg(0, 1);
            emit8(0x48); emit8(0x89); emit8(0xC1); // mov rcx, rax
            emit8(0x48); emit8(0x8D); emit8(0x05);
            heapFixups.push_back({code.size(), heapAreaRVA}); emit32(0);
            emit8(0x48); emit8(0x8B); emit8(0x15);
            heapFixups.push_back({code.size(), heapOffsetRVA}); emit32(0);
            emit8(0x49); emit8(0x89); emit8(0xC0); // r8 = rax (old offset)
            emit8(0x48); emit8(0x03); emit8(0xCA); // rcx = old_offset + size
            emit8(0x48); emit8(0x81); emit8(0xF9); emit32(64 * 1024);
            int fl = newLabel();
            emit8(0x0F); emit8(0x87); jmpFixups.push_back({(int)code.size(), fl}); emit32(0);
            emit8(0x48); emit8(0x89); emit8(0x0D);
            heapFixups.push_back({code.size(), heapOffsetRVA}); emit32(0);
            emit8(0x48); emit8(0x03); emit8(0xC2); // rax = heapArea + old_offset
            int dl = newLabel(); emitJmp(dl);
            emitLabel(fl); emit8(0x48); emit8(0x31); emit8(0xC0); // xor rax, rax (fail=0)
            emitLabel(dl);
            freeReg(1); freeReg(2);
        };

        // arenaCreate(capacity) -> handle
        // Header: [capacity:8][used:8], data at handle+16
        if (call->name == "arenaCreate" && call->args.size() == 1) {
            int saved = regsUsed;
            regsUsed = 0;
            int capReg = emitExpr(call->args[0].get());
            // Save capacity in reg 3 (rbx) — safe across emitHeapAlloc
            if (capReg != 3) { emitMovReg(3, capReg); freeReg(capReg); }
            // rcx = capacity + 16 (total allocation size)
            emitMovReg(1, 3);
            emit8(0x48); emit8(0x83); emit8(0xC1); emit8(16);
            emitHeapAlloc(1);  // rax = block pointer (or 0 on fail)
            // [rax+0] = capacity (from rbx)
            emitStoreQwordDisp8(3, 0, 0);
            // [rax+8] = 0 (used = 0)
            emitMovQwordDisp8Imm32(0, 8, 0);
            // rax = handle (return value)
            freeReg(3);
            regsUsed = (uint8_t)saved;
            int r = allocReg();
            if (r != 0) { emitMovReg(r, 0); freeReg(0); }
            return r >= 0 ? r : 0;
        }

        // arenaAlloc(arena, size) -> ptr
        if (call->name == "arenaAlloc" && call->args.size() == 2) {
            int saved = regsUsed;
            regsUsed = 0;
            int aReg = emitExpr(call->args[0].get());
            int sReg = emitExpr(call->args[1].get());
            // Fix: ensure handle in rbx(3), size in rcx(1) or rdx(2), not in rax
            if (aReg == 0) {
                if (sReg == 3) { emitMovReg(2, 3); sReg = 2; }
                emitMovReg(3, 0); aReg = 3;
            } else if (sReg == 3) {
                int tmp = (aReg == 1) ? 2 : 1;
                emitMovReg(tmp, 3); sReg = tmp;
                emitMovReg(3, aReg); freeReg(aReg); aReg = 3;
            } else if (sReg == 0) {
                int tmp = (aReg == 2) ? 1 : 2;
                emitMovReg(tmp, 0); sReg = tmp;
                if (aReg != 3) { emitMovReg(3, aReg); freeReg(aReg); }
                aReg = 3;
            } else {
                if (aReg != 3) { emitMovReg(3, aReg); freeReg(aReg); }
                aReg = 3;
            }
            // ptr = rbx + 16 + [rbx+8]; then [rbx+8] += sReg
            // Load used into rax
            emitLoadQwordDisp8(0, 3, 8);
            // ptr = rax + rbx + 16
            emitAdd(0, 3);
            emit8(0x48); emit8(0x83); emit8(0xC0); emit8(16);
            // rax = ptr
            int pReg = allocReg();
            if (pReg != 0) { emitMovReg(pReg, 0); freeReg(0); }
            // Update used: load [rbx+8], add sReg, store back
            emitLoadQwordDisp8(0, 3, 8);
            emitAdd(0, sReg);
            emitStoreQwordDisp8(0, 3, 8);
            freeReg(3); freeReg(sReg);
            regsUsed = (uint8_t)saved;
            int r = allocReg();
            if (r != pReg) { emitMovReg(r, pReg); freeReg(pReg); }
            return r;
        }

        // arenaReset(arena) -> void
        if (call->name == "arenaReset" && call->args.size() == 1) {
            int saved = regsUsed;
            regsUsed = 0;
            int a = emitExpr(call->args[0].get());
            emitMovQwordDisp8Imm32(a, 8, 0);
            freeReg(a);
            regsUsed = 1;
            emitMovRegImm(0, 0);
            return 0;
        }

        // arenaDestroy(arena) -> void (no-op for bump allocator)
        if (call->name == "arenaDestroy" && call->args.size() == 1) {
            int r = emitExpr(call->args[0].get());
            freeReg(r);
            regsUsed = 1;
            emitMovRegImm(0, 0);
            return 0;
        }

        // ============== Pool Allocator Builtins ==============
        // Pool header: [blockSize:8][count:8][nextIdx:8], data at handle+24
        // Monotonic next-index allocator: O(1) alloc, reset to free all

        // poolCreate(blockSize, count) -> handle
        // Header: [blockSize:8][count:8][nextIdx:8], data at handle+24
        if (call->name == "poolCreate" && call->args.size() == 2) {
            int saved = regsUsed;
            regsUsed = 0;
            int bs = emitExpr(call->args[0].get());
            int cnt = emitExpr(call->args[1].get());
            // Save capacity in reg 3 (rbx) and count on stack
            if (bs != 3) { emitMovReg(3, bs); freeReg(bs); }
            if (cnt != 0) { emitMovReg(0, cnt); freeReg(cnt); }
            emit8(0x50); // push rax (count)
            emit8(0x58); // pop rcx (count in rcx)
            // rax = blockSize * count + 24
            emitMovReg(0, 3);
            emitImul(0, 1);  // rax *= rcx (count)
            emit8(0x48); emit8(0x83); emit8(0xC0); emit8(24);
            // Push count again for header store after alloc
            emit8(0x51); // push rcx (count)
            emitHeapAlloc(0);  // rax = block pointer
            // Store header fields
            emitStoreQwordDisp8(3, 0, 0);  // [rax+0] = blockSize (rbx)
            emit8(0x59); // pop rcx (count)
            emitStoreQwordDisp8(1, 0, 8);  // [rax+8] = count (rcx)
            emitMovQwordDisp8Imm32(0, 16, 0); // [rax+16] = 0 (nextIdx)
            freeReg(3);
            regsUsed = (uint8_t)saved;
            int r = allocReg();
            if (r != 0) { emitMovReg(r, 0); freeReg(0); }
            return r >= 0 ? r : 0;
        }

        // poolAlloc(pool) -> ptr
        if (call->name == "poolAlloc" && call->args.size() == 1) {
            int saved = regsUsed;
            regsUsed = 0;
            int pReg = emitExpr(call->args[0].get());
            // Fix: move handle to rbx to avoid clobbering
            if (pReg != 3) { emitMovReg(3, pReg); freeReg(pReg); }
            regsUsed |= (1 << 3);
            // Load nextIdx into rax
            emitLoadQwordDisp8(0, 3, 16);
            regsUsed |= (1 << 0); // rax holds nextIdx
            // Load count into free reg
            int cReg = allocReg();
            emitLoadQwordDisp8(cReg, 3, 8);
            // cmp rax, cReg (nextIdx vs count)
            emit8(0x48); emit8(0x39); emit8((uint8_t)(0xC0 + cReg)); // cmp rax, cReg
            freeReg(cReg);
            // jae overflow (return 0)
            int overflow = newLabel();
            emit8(0x0F); emit8(0x83); // jae
            jmpFixups.push_back({(int)code.size(), overflow}); emit32(0);
            // ptr = rbx + 24 + nextIdx * blockSize
            int bsReg = allocReg();
            emitLoadQwordDisp8(bsReg, 3, 0); // bsReg = blockSize
            emit8(0x48); emit8(0x0F); emit8(0xAF); emit8((uint8_t)(0xC0 + bsReg)); // imul rax, bsReg
            freeReg(bsReg);
            emitAdd(0, 3); // rax += rbx (base)
            emit8(0x48); emit8(0x83); emit8(0xC0); emit8(24); // rax += 24
            // rax = ptr
            int ptrR = allocReg();
            if (ptrR != 0) { emitMovReg(ptrR, 0); freeReg(0); }
            // nextIdx++: load, inc, store
            emitLoadQwordDisp8(0, 3, 16);
            emit8(0x48); emit8(0xFF); emit8(0xC0); // inc rax
            emitStoreQwordDisp8(0, 3, 16);
            freeReg(3);
            // Jump over overflow handler
            int done = newLabel();
            emitJmp(done);
            emitLabel(overflow);
            // Return 0
            emitMovRegImm(ptrR >= 0 ? ptrR : 0, 0);
            emitLabel(done);
            regsUsed = (uint8_t)saved;
            int r = allocReg();
            if (ptrR >= 0 && r != ptrR) { emitMovReg(r, ptrR); freeReg(ptrR); }
            return r >= 0 ? r : 0;
        }

        // poolFree(pool, ptr) -> void (no-op, use poolReset to free all)
        if (call->name == "poolFree" && call->args.size() == 2) {
            int r1 = emitExpr(call->args[0].get());
            int r2 = emitExpr(call->args[1].get());
            freeReg(r1); freeReg(r2);
            regsUsed = 1;
            emitMovRegImm(0, 0);
            return 0;
        }

        // poolReset(pool) -> void (reset nextIdx to 0, freeing all blocks)
        if (call->name == "poolReset" && call->args.size() == 1) {
            int saved = regsUsed;
            regsUsed = 0;
            int p = emitExpr(call->args[0].get());
            emitMovQwordDisp8Imm32(p, 16, 0);
            freeReg(p);
            regsUsed = 1;
            emitMovRegImm(0, 0);
            return 0;
        }

        // poolDestroy(pool) -> void (no-op)
        if (call->name == "poolDestroy" && call->args.size() == 1) {
            int r = emitExpr(call->args[0].get());
            freeReg(r);
            regsUsed = 1;
            emitMovRegImm(0, 0);
            return 0;
        }

        // ============== Slot Allocator Builtins ==============
        // Header: [maxCount:8][dataSize:8][freeHead:8][aliveCount:8] at handle+0
        // Slots at handle+32, stride = 16 + dataSize
        // Each slot: [generation:8][nextFree:8][data:dataSize]
        // Handle = (generation << 32) | slotIndex
        // rbx(3) = slot handle throughout

        // Helper: compute slotAddr = rbx + 32 + idx*stride into dstReg
        // idxReg has the index. Uses rax, rcx as temps. stride stored in memory.
        // After: dstReg = slotAddr, idxReg destroyed, rax/rcx trashed

        // slotCreate(maxCount, dataSize) -> slot
        if (call->name == "slotCreate" && call->args.size() == 2) {
            int saved = regsUsed;
            regsUsed = 0;
            int mcReg = emitExpr(call->args[0].get());
            int dsReg = emitExpr(call->args[1].get());
            // rbx = maxCount
            if (mcReg != 3) { emitMovReg(3, mcReg); freeReg(mcReg); }
            // rcx = dataSize
            if (dsReg != 1) { emitMovReg(1, dsReg); freeReg(dsReg); }
            // Save dataSize on stack, compute total size in rcx
            emit8(0x51); // push rcx (dataSize)
            emit8(0x48); emit8(0x83); emit8(0xC1); emit8(16); // stride = dataSize+16
            emitImul(1, 3); // rcx *= maxCount
            emit8(0x48); emit8(0x83); emit8(0xC1); emit8(32); // +32 header
            // Heap alloc via RIP-relative (same as alloc builtin)
            emit8(0x48); emit8(0x8D); emit8(0x05);
            heapFixups.push_back({code.size(), heapAreaRVA}); emit32(0);
            emit8(0x48); emit8(0x8B); emit8(0x15);
            heapFixups.push_back({code.size(), heapOffsetRVA}); emit32(0);
            emit8(0x49); emit8(0x89); emit8(0xC8);
            emit8(0x48); emit8(0x03); emit8(0xCA);
            emit8(0x48); emit8(0x81); emit8(0xF9); emit32(64 * 1024);
            int scFail = newLabel();
            emit8(0x0F); emit8(0x87);
            jmpFixups.push_back({(int)code.size(), scFail}); emit32(0);
            emit8(0x48); emit8(0x89); emit8(0x0D);
            heapFixups.push_back({code.size(), heapOffsetRVA}); emit32(0);
            emit8(0x48); emit8(0x03); emit8(0xC2);
            int scDone = newLabel();
            emitJmp(scDone);
            emitLabel(scFail);
            emit8(0x48); emit8(0x31); emit8(0xC0);
            emitLabel(scDone);
            // rax = block, stack has [dataSize]
            emit8(0x59); // pop rcx (dataSize)
            emitStoreQwordDisp8(3, 0, 0); // [rax+0] = maxCount
            emitStoreQwordDisp8(1, 0, 8); // [rax+8] = dataSize
            emitMovQwordDisp8Imm32(0, 16, -1); // freeHead = -1
            emitMovQwordDisp8Imm32(0, 24, 0);  // aliveCount = 0
            freeReg(3);
            regsUsed = (uint8_t)saved;
            int r = allocReg();
            if (r != 0) { emitMovReg(r, 0); freeReg(0); }
            return r >= 0 ? r : 0;
        }

        // slotSpawn(slot) -> handle
        if (call->name == "slotSpawn" && call->args.size() == 1) {
            int saved = regsUsed;
            regsUsed = 0;
            int sReg = emitExpr(call->args[0].get());
            if (sReg != 3) { emitMovReg(3, sReg); freeReg(sReg); }
            regsUsed |= (1 << 3);

            int fullLabel = newLabel();
            int doneLabel = newLabel();
            int freeListLabel = newLabel();

            // rax = freeHead
            emitLoadQwordDisp8(0, 3, 16);
            // cmp rax, -1
            emit8(0x48); emit8(0x83); emit8(0xF8); emit8((uint8_t)(int8_t)-1);
            // jne freeListLabel
            emitJcc("!=", freeListLabel);

            // === SEQUENTIAL PATH (freeHead == -1) ===
            emitLoadQwordDisp8(1, 3, 0);  // rcx = maxCount
            emitLoadQwordDisp8(2, 3, 24); // rdx = aliveCount = idx
            // cmp rdx, rcx; jae full
            emit8(0x48); emit8(0x39); emit8(0xCA); // cmp rdx, rcx
            emitJcc(">=", fullLabel);

            emit8(0x52); // push rdx (save idx)
            // stride = [rbx+8] + 16
            emitLoadQwordDisp8(0, 3, 8);
            emit8(0x48); emit8(0x83); emit8(0xC0); emit8(16);
            // slotAddr = rbx + idx*stride + 32
            emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xD0); // imul rdx, rax
            emitAdd(2, 3);
            emit8(0x48); emit8(0x83); emit8(0xC2); emit8(32);
            // Load gen, bump
            emitLoadQwordDisp8(0, 2, 0); // rax = gen
            emitIncQwordDisp8(2, 0);     // gen++
            // Pack: (gen << 32) | idx
            emit8(0x48); emit8(0xC1); emit8(0xE0); emit8(0x20); // shl rax, 32
            emit8(0x59); // pop rcx (idx)
            emitAdd(0, 1); // rax += idx
            emitIncQwordDisp8(3, 24); // aliveCount++
            emitJmp(doneLabel);

            // === FREE LIST PATH ===
            emitLabel(freeListLabel);
            // rax = freeHead = idx
            emit8(0x50); // push rax (save idx)
            // stride = [rbx+8] + 16
            emitLoadQwordDisp8(1, 3, 8);
            emit8(0x48); emit8(0x83); emit8(0xC1); emit8(16);
            // slotAddr = rbx + idx*stride + 32
            emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xC1); // imul rax, rcx
            emitAdd(0, 3);
            emit8(0x48); emit8(0x83); emit8(0xC0); emit8(32);
            // Update free list
            emitLoadQwordDisp8(1, 0, 8); // rcx = nextFree
            emitStoreQwordDisp8(1, 3, 16); // [rbx+16] = nextFree
            // Load gen (no bump)
            emitLoadQwordDisp8(2, 0, 0); // rdx = gen
            // Pack: (gen << 32) | idx
            emit8(0x48); emit8(0xC1); emit8(0xE2); emit8(0x20); // shl rdx, 32
            emit8(0x59); // pop rcx (idx)
            emitMovReg(0, 2); // rax = gen<<32
            emitAdd(0, 1);    // rax += idx
            emitIncQwordDisp8(3, 24); // aliveCount++
            emitJmp(doneLabel);

            // === FULL ===
            emitLabel(fullLabel);
            emit8(0x48); emit8(0x31); emit8(0xC0); // xor rax, rax

            emitLabel(doneLabel);
            freeReg(3);
            regsUsed = (uint8_t)saved;
            int r = allocReg();
            if (r != 0) { emitMovReg(r, 0); freeReg(0); }
            return r >= 0 ? r : 0;
        }

        // slotKill(slot, handle) -> void
        if (call->name == "slotKill" && call->args.size() == 2) {
            int saved = regsUsed;
            regsUsed = 0;
            int sReg = emitExpr(call->args[0].get());
            int hReg = emitExpr(call->args[1].get());
            if (sReg != 3) { emitMovReg(3, sReg); freeReg(sReg); }
            if (hReg != 0) { emitMovReg(0, hReg); freeReg(hReg); }
            // Extract idx: mov eax, eax
            emit8(0x89); emit8(0xC0);
            emitMovReg(2, 0); // rdx = idx
            // stride = [rbx+8] + 16
            emitLoadQwordDisp8(1, 3, 8);
            emit8(0x48); emit8(0x83); emit8(0xC1); emit8(16);
            // slotAddr = rbx + idx*stride + 32
            emitImul(0, 1); // rax *= rcx
            emitAdd(0, 3);  // rax += rbx
            emit8(0x48); emit8(0x83); emit8(0xC0); emit8(32);
            // Bump generation: inc [rax+0]
            emitIncQwordDisp8(0, 0);
            // Push to free list: [rax+8] = oldFreeHead; [rbx+16] = idx
            emitLoadQwordDisp8(1, 3, 16); // rcx = oldFreeHead
            emitStoreQwordDisp8(1, 0, 8); // [rax+8] = oldFreeHead
            emitStoreQwordDisp8(2, 3, 16); // [rbx+16] = idx
            // Dec aliveCount: dec [rbx+24]
            emit8(0x48); emit8(0xFF); emit8(0x4B); emit8(0x18);
            freeReg(3);
            regsUsed = (uint8_t)saved;
            emitMovRegImm(0, 0);
            return 0;
        }

        // slotGetI64(slot, handle, byteOffset) -> int
        if (call->name == "slotGetI64" && call->args.size() == 3) {
            int saved = regsUsed;
            regsUsed = 0;
            int sReg = emitExpr(call->args[0].get());
            int hReg = emitExpr(call->args[1].get());
            int bReg = emitExpr(call->args[2].get());
            if (sReg != 3) { emitMovReg(3, sReg); freeReg(sReg); }
            if (hReg != 0) { emitMovReg(0, hReg); freeReg(hReg); }
            emit8(0x89); emit8(0xC0); // mov eax, eax (idx)
            if (bReg != 2) { emitMovReg(2, bReg); freeReg(bReg); }
            // stride
            emitLoadQwordDisp8(1, 3, 8);
            emit8(0x48); emit8(0x83); emit8(0xC1); emit8(16);
            // slotAddr+byteOffset
            emitImul(0, 1);
            emitAdd(0, 3);
            emit8(0x48); emit8(0x83); emit8(0xC0); emit8(32);
            emitAdd(0, 2); // + byteOffset
            // Load value
            emitLoadQwordDisp8(1, 0, 0); // rcx = value
            freeReg(3);
            regsUsed = (uint8_t)saved;
            int r = allocReg();
            if (r != 1) { emitMovReg(r, 1); freeReg(1); }
            return r >= 0 ? r : 0;
        }

        // slotSetI64(slot, handle, byteOffset, value) -> void
        if (call->name == "slotSetI64" && call->args.size() == 4) {
            int saved = regsUsed;
            regsUsed = 0;
            int sReg = emitExpr(call->args[0].get());
            int hReg = emitExpr(call->args[1].get());
            int bReg = emitExpr(call->args[2].get());
            int vReg = emitExpr(call->args[3].get());
            // save value BEFORE clobbering rbx with slot handle
            emit8(0x50 + vReg); // push value
            if (sReg != 3) { emitMovReg(3, sReg); freeReg(sReg); }
            if (hReg != 0) { emitMovReg(0, hReg); freeReg(hReg); }
            emit8(0x89); emit8(0xC0); // mov eax, eax (idx)
            if (bReg != 2) { emitMovReg(2, bReg); freeReg(bReg); }
            // stride
            emitLoadQwordDisp8(1, 3, 8);
            emit8(0x48); emit8(0x83); emit8(0xC1); emit8(16);
            // addr
            emitImul(0, 1);
            emitAdd(0, 3);
            emit8(0x48); emit8(0x83); emit8(0xC0); emit8(32);
            emitAdd(0, 2);
            // Store
            emit8(0x59); // pop rcx (value)
            emitStoreQwordDisp8(1, 0, 0); // [rax] = value
            freeReg(3);
            regsUsed = (uint8_t)saved;
            emitMovRegImm(0, 0);
            return 0;
        }

        // slotGetF32(slot, handle, byteOffset) -> i64 (float bits)
        if (call->name == "slotGetF32" && call->args.size() == 3) {
            int saved = regsUsed;
            regsUsed = 0;
            int sReg = emitExpr(call->args[0].get());
            int hReg = emitExpr(call->args[1].get());
            int bReg = emitExpr(call->args[2].get());
            if (sReg != 3) { emitMovReg(3, sReg); freeReg(sReg); }
            if (hReg != 0) { emitMovReg(0, hReg); freeReg(hReg); }
            emit8(0x89); emit8(0xC0); // mov eax, eax (idx)
            if (bReg != 2) { emitMovReg(2, bReg); freeReg(bReg); }
            // stride
            emitLoadQwordDisp8(1, 3, 8);
            emit8(0x48); emit8(0x83); emit8(0xC1); emit8(16);
            // addr
            emitImul(0, 1);
            emitAdd(0, 3);
            emit8(0x48); emit8(0x83); emit8(0xC0); emit8(32);
            emitAdd(0, 2);
            // Load float bits: movss xmm0, [rax+0] then movd rcx, xmm0
            int x = allocXmmReg();
            emitMovssXmmFromMem(x, 0, 0);
            emitMovdGpFromXmm(1, x); // rcx = float bits as int
            freeXmmReg(x);
            freeReg(3);
            regsUsed = (uint8_t)saved;
            int r = allocReg();
            if (r != 1) { emitMovReg(r, 1); freeReg(1); }
            return r >= 0 ? r : 0;
        }

        // slotSetF32(slot, handle, byteOffset, value) -> void
        if (call->name == "slotSetF32" && call->args.size() == 4) {
            int saved = regsUsed;
            regsUsed = 0;
            int sReg = emitExpr(call->args[0].get());
            int hReg = emitExpr(call->args[1].get());
            int bReg = emitExpr(call->args[2].get());
            int vReg = emitExpr(call->args[3].get());
            if (sReg != 3) { emitMovReg(3, sReg); freeReg(sReg); }
            // Convert value to float, save xmm on stack
            int x = allocXmmReg();
            emitCvtsi2ss(x, vReg); // xmm = (float)value
            // sub rsp, 4 for float storage
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x04);
            emitMovssXmmToMem(x, 4, 0); // movss [rsp], xmm
            freeXmmReg(x);
            freeReg(vReg);
            if (hReg != 0) { emitMovReg(0, hReg); freeReg(hReg); }
            emit8(0x89); emit8(0xC0); // mov eax, eax (idx)
            if (bReg != 2) { emitMovReg(2, bReg); freeReg(bReg); }
            // stride
            emitLoadQwordDisp8(1, 3, 8);
            emit8(0x48); emit8(0x83); emit8(0xC1); emit8(16);
            // addr
            emitImul(0, 1);
            emitAdd(0, 3);
            emit8(0x48); emit8(0x83); emit8(0xC0); emit8(32);
            emitAdd(0, 2);
            // Load float from stack into xmm, store to addr
            int x2 = allocXmmReg();
            emitMovssXmmFromMem(x2, 4, 0); // xmm from [rsp]
            emitMovssXmmToMem(x2, 0, 0);   // movss [rax], xmm
            freeXmmReg(x2);
            // add rsp, 4
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x04);
            freeReg(3);
            regsUsed = (uint8_t)saved;
            emitMovRegImm(0, 0);
            return 0;
        }

        // slotCount(slot) -> int
        if (call->name == "slotCount" && call->args.size() == 1) {
            int saved = regsUsed;
            regsUsed = 0;
            int sReg = emitExpr(call->args[0].get());
            if (sReg != 3) { emitMovReg(3, sReg); freeReg(sReg); }
            emitLoadQwordDisp8(0, 3, 24); // rax = aliveCount
            freeReg(3);
            regsUsed = (uint8_t)saved;
            int r = allocReg();
            if (r != 0) { emitMovReg(r, 0); freeReg(0); }
            return r >= 0 ? r : 0;
        }

        // slotReset(slot) -> void
        if (call->name == "slotReset" && call->args.size() == 1) {
            int saved = regsUsed;
            regsUsed = 0;
            int sReg = emitExpr(call->args[0].get());
            if (sReg != 3) { emitMovReg(3, sReg); freeReg(sReg); }
            emitMovQwordDisp8Imm32(3, 16, -1); // freeHead = -1
            emitMovQwordDisp8Imm32(3, 24, 0);  // aliveCount = 0
            freeReg(3);
            regsUsed = (uint8_t)saved;
            emitMovRegImm(0, 0);
            return 0;
        }

        // slotDestroy(slot) -> void (no-op)
        if (call->name == "slotDestroy" && call->args.size() == 1) {
            int r = emitExpr(call->args[0].get());
            freeReg(r);
            regsUsed = 1;
            emitMovRegImm(0, 0);
            return 0;
        }

        // sleep(seconds) -> void
        if (call->name == "sleep" && call->args.size() == 1) {
            int saved = regsUsed;
            regsUsed = 0;
            int sReg = emitExpr(call->args[0].get());
            if (sReg != 0) { emitMovReg(0, sReg); freeReg(sReg); }
            // rax = seconds; ms = rax * 1000
            emit8(0x48); emit8(0x69); emit8(0xC0); emit32(1000); // imul rax, rax, 1000
            // sub rsp, 0x20; mov ecx, eax; call Sleep; add rsp, 0x20
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20); // sub rsp, 32
            emit8(0x89); emit8(0xC1); // mov ecx, eax
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "Sleep", "kernel32.dll"});
            emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20); // add rsp, 32
            freeReg(3);
            regsUsed = (uint8_t)saved;
            emitMovRegImm(0, 0);
            return 0;
        }

        // pause() -> void: print message and wait for Enter
        if (call->name == "pause" && call->args.size() == 0) {
            int saved = regsUsed;
            regsUsed = 0;

            // Find or add "Press any key to continue . . .\r\n"
            std::string pauseMsg = "Press any key to continue . . .\r\n";
            int pauseIdx = -1;
            for (size_t i = 0; i < stringPool.size(); i++) {
                if (stringPool[i] == pauseMsg) { pauseIdx = (int)i; break; }
            }
            if (pauseIdx < 0) {
                pauseIdx = (int)stringPool.size();
                stringPool.push_back(pauseMsg);
            }

            // --- Get stdout handle ---
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20); // sub rsp, 32
            emit8(0xB9); emit32((uint32_t)-11); // mov ecx, -11 (STD_OUTPUT_HANDLE)
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "GetStdHandle", "kernel32.dll"});
            emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20); // add rsp, 32
            emit8(0x50); // push rax (save stdout)

            // --- WriteFile(stdout, msg, len, NULL, NULL) ---
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x28); // sub rsp, 40
            emit8(0x48); emit8(0x8B); emit8(0x8C); emit8(0x24); emit32(40); // mov rcx, [rsp+40] = stdout
            emit8(0x48); emit8(0x8D); emit8(0x15); // lea rdx, [rip+msg]
            strFixups.push_back({code.size(), pauseIdx});
            emit32(0);
            emit8(0x41); emit8(0xB8); emit32((uint32_t)pauseMsg.size()); // mov r8d, len
            emit8(0x45); emit8(0x31); emit8(0xC9); // xor r9d, r9d
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(0); // mov qword [rsp+32], 0
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "WriteFile", "kernel32.dll"});
            emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x28); // add rsp, 40

            // --- Get stdin handle ---
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x28); // sub rsp, 40
            emit8(0xB9); emit32((uint32_t)-10); // mov ecx, -10 (STD_INPUT_HANDLE)
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "GetStdHandle", "kernel32.dll"});
            emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x28); // add rsp, 40
            // rax = stdin

            // --- ReadFile(stdin, buf, 1, &read, NULL) ---
            // [rsp+0..31] shadow, [rsp+32] overlapped, [rsp+40] buf, [rsp+48] bytesRead
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x38); // sub rsp, 56
            emit8(0x48); emit8(0x89); emit8(0xC1); // mov rcx, rax (stdin)
            emit8(0x48); emit8(0x8D); emit8(0x54); emit8(0x24); emit8(0x28); // lea rdx, [rsp+40] buf
            emit8(0x41); emit8(0xB8); emit32(1); // mov r8d, 1
            emit8(0x4C); emit8(0x8D); emit8(0x4C); emit8(0x24); emit8(0x30); // lea r9, [rsp+48] bytesRead
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(0); // mov qword [rsp+32], 0
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "ReadFile", "kernel32.dll"});
            emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x38); // add rsp, 56

            emit8(0x58); // pop rax (balance stdout push)

            freeReg(3);
            regsUsed = (uint8_t)saved;
            emitMovRegImm(0, 0);
            return 0;
        }

        if (call->name == "print" && call->args.size() == 1) {
            int saved = regsUsed;
            regsUsed = 0;

            int newlineIdx = -1;
            for (size_t i = 0; i < stringPool.size(); i++) {
                if (stringPool[i] == "\r\n") { newlineIdx = (int)i; break; }
            }

            // --- Step 1: GetStdHandle(STD_OUTPUT_HANDLE = -11) ---
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);  // sub rsp, 32
            emit8(0xB9); emit32((uint32_t)-11);                   // mov ecx, -11
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "GetStdHandle", "kernel32.dll"});
            emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);  // add rsp, 32
            emit8(0x50);  // push rax  (save handle on stack)

            if (auto strExpr = dynamic_cast<StringExpr*>(call->args[0].get())) {
                int strIdx = -1;
                for (size_t i = 0; i < stringPool.size(); i++) {
                    if (stringPool[i] == strExpr->value) { strIdx = (int)i; break; }
                }
                if (strIdx < 0) {
                    strIdx = (int)stringPool.size();
                    stringPool.push_back(strExpr->value);
                }
                int len = (int)strExpr->value.size();

                // --- WriteFile(handle, str, len, NULL, NULL) ---
                emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x28);  // sub rsp, 40
                emit8(0x48); emit8(0x8B); emit8(0x8C); emit8(0x24); emit32(40);  // mov rcx, [rsp+40]
                emit8(0x48); emit8(0x8D); emit8(0x15);  // lea rdx, [rip+disp32]
                strFixups.push_back({code.size(), strIdx});
                emit32(0);
                emit8(0x41); emit8(0xB8); emit32(len);  // mov r8d, len
                emit8(0x45); emit8(0x31); emit8(0xC9);  // xor r9d, r9d
                emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(0);
                emit8(0xFF); emit8(0x15);
                importCallFixups.push_back({code.size(), "WriteFile", "kernel32.dll"});
                emit32(0);
                emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x28);  // add rsp, 40

                // WriteFile(handle, "\r\n", 2, NULL, NULL)
                emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x28);
                emit8(0x48); emit8(0x8B); emit8(0x8C); emit8(0x24); emit32(40);  // mov rcx, [rsp+40]
                emit8(0x48); emit8(0x8D); emit8(0x15);  // lea rdx, [rip+disp32]
                strFixups.push_back({code.size(), newlineIdx});
                emit32(0);
                emit8(0x41); emit8(0xB8); emit32(2);  // mov r8d, 2
                emit8(0x45); emit8(0x31); emit8(0xC9);  // xor r9d, r9d
                emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(0);
                emit8(0xFF); emit8(0x15);
                importCallFixups.push_back({code.size(), "WriteFile", "kernel32.dll"});
                emit32(0);
                emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x28);

                emit8(0x58);  // pop rax (balance push)

            } else {
                // --- Int case: convert to string on stack, then WriteFile ---
                int exprReg = emitExpr(call->args[0].get());
                if (exprReg != 0) { emitMovReg(0, exprReg); freeReg(exprReg); }
                else freeReg(0);

                emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);  // sub rsp, 32 (buffer)

                int notZero = newLabel();
                emit8(0x48); emit8(0x85); emit8(0xC0);  // test rax, rax
                emit8(0x0F); emit8(0x85);
                jmpFixups.push_back({(int)code.size(), notZero});
                emit32(0);
                emit8(0xC6); emit8(0x04); emit8(0x24); emit8(0x30);  // mov byte [rsp], '0'
                emit8(0x41); emit8(0xB8); emit8(0x01); emit8(0x00); emit8(0x00); emit8(0x00);  // mov r8d, 1
                emit8(0x49); emit8(0x89); emit8(0xE2);  // mov r10, rsp
                int afterZero = newLabel();
                emitJmp(afterZero);
                emitLabel(notZero);

                emit8(0x49); emit8(0x89); emit8(0xE2);  // mov r10, rsp
                emit8(0x49); emit8(0x83); emit8(0xC2); emit8(0x1F);  // add r10, 31
                emit8(0x45); emit8(0x31); emit8(0xC0);  // xor r8d, r8d
                emit8(0xB9); emit8(0x0A); emit8(0x00); emit8(0x00); emit8(0x00);  // mov ecx, 10

                int convLoop = newLabel();
                emitLabel(convLoop);
                emit8(0x48); emit8(0x31); emit8(0xD2);  // xor edx, edx
                emit8(0x48); emit8(0xF7); emit8(0xF1);  // div rcx
                emit8(0x80); emit8(0xC2); emit8(0x30);  // add dl, '0'
                emit8(0x49); emit8(0xFF); emit8(0xCA);  // dec r10
                emit8(0x41); emit8(0x88); emit8(0x12);  // mov [r10], dl
                emit8(0x41); emit8(0xFF); emit8(0xC0);  // inc r8d
                emit8(0x48); emit8(0x85); emit8(0xC0);  // test rax, rax
                emit8(0x0F); emit8(0x85);
                jmpFixups.push_back({(int)code.size(), convLoop});
                emit32(0);

                emitLabel(afterZero);

                // WriteFile(handle, r10, r8d, NULL, NULL)
                emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x28);
                emit8(0x48); emit8(0x8B); emit8(0x8C); emit8(0x24); emit32(72);
                emit8(0x4C); emit8(0x89); emit8(0xD2);  // mov rdx, r10
                emit8(0x45); emit8(0x31); emit8(0xC9);  // xor r9d, r9d
                emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(0);
                emit8(0xFF); emit8(0x15);
                importCallFixups.push_back({code.size(), "WriteFile", "kernel32.dll"});
                emit32(0);
                emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x28);

                // WriteFile(handle, "\r\n", 2, NULL, NULL)
                emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x28);
                emit8(0x48); emit8(0x8B); emit8(0x8C); emit8(0x24); emit32(72);
                emit8(0x48); emit8(0x8D); emit8(0x15);
                strFixups.push_back({code.size(), newlineIdx});
                emit32(0);
                emit8(0x41); emit8(0xB8); emit32(2);
                emit8(0x45); emit8(0x31); emit8(0xC9);
                emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(0);
                emit8(0xFF); emit8(0x15);
                importCallFixups.push_back({code.size(), "WriteFile", "kernel32.dll"});
                emit32(0);
                emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x28);

                emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);  // add rsp, 32 (buffer)
                emit8(0x58);  // pop rax (balance push)
            }

            regsUsed = (uint8_t)saved;
            return 0;
        }

        // ============== Shared Memory Builtins ==============
        // peek(addr) — reads a 64-bit value from the given address
        if (call->name == "peek" && call->args.size() == 1) {
            int saved = regsUsed; regsUsed = 0;
            int addrReg = emitExpr(call->args[0].get());
            if (addrReg != 0) { emitMovReg(0, addrReg); freeReg(addrReg); }
            else freeReg(0);
            emit8(0x48); emit8(0x8B); emit8(0x00); // mov rax, [rax]
            regsUsed = 1;
            return 0;
        }
        // poke(addr, val) — writes val (64-bit) to the given address
        if (call->name == "poke" && call->args.size() == 2) {
            int saved = regsUsed; regsUsed = 0;
            int valReg = emitExpr(call->args[1].get());
            if (valReg != 0) { emitMovReg(0, valReg); freeReg(valReg); }
            else freeReg(0);
            emit8(0x50);  // push rax (val)
            int addrReg = emitExpr(call->args[0].get());
            if (addrReg != 0) { emitMovReg(0, addrReg); freeReg(addrReg); }
            else freeReg(0);
            emit8(0x50);  // push rax (addr)
            emit8(0x41); emit8(0x58);  // pop r8 (addr)
            emit8(0x41); emit8(0x59);  // pop r9 (val)
            emit8(0x4D); emit8(0x89); emit8(0x08); // mov [r8], r9
            regsUsed = 1;
            return 0;
        }

        // ============== 2D Graphics Built-in Functions (GUI only) ==============

        if (prog.appType == AppType::GUI) {
        // --- drawPixel(x, y, color) ---
        if (call->name == "drawPixel" && call->args.size() == 3) {
            int saved = regsUsed; regsUsed = 0;
            int colorReg = emitExpr(call->args[2].get());
            if (colorReg != 0) { emitMovReg(0, colorReg); freeReg(colorReg); }
            else freeReg(0);
            emit8(0x50);  // push rax (color)

            int yReg = emitExpr(call->args[1].get());
            if (yReg != 0) { emitMovReg(0, yReg); freeReg(yReg); }
            else freeReg(0);
            emit8(0x50);  // push rax (y)

            int xReg = emitExpr(call->args[0].get());
            if (xReg != 0) { emitMovReg(0, xReg); freeReg(xReg); }
            else freeReg(0);
            emit8(0x50);  // push rax (x)

            // lea rbx, [rip + win32Globals]
            emit8(0x48); emit8(0x8D); emit8(0x1D);
            heapFixups.push_back({code.size(), win32GlobalsRVA});
            emit32(0);

            // Pop x, y, color (stack: color at [rsp], y at [rsp+8], x at [rsp+16])
            emit8(0x41); emit8(0x58);  // pop r8 (x)
            emit8(0x41); emit8(0x59);  // pop r9 (y)
            emit8(0x41); emit8(0x5A);  // pop r10 (color)

            // rbx = &globals
            // framebuffer = globals+16, width = globals+32
            emit8(0x4C); emit8(0x8B); emit8(0x5B); emit8(0x28);  // mov r11, [rbx+40] (framebuffer)
            emit8(0x44); emit8(0x8B); emit8(0x63); emit8(0x20);  // mov r12d, [rbx+32] (width)

            // Compute address: framebuf + (y * width + x) * 4
            emit8(0x4C); emit8(0x89); emit8(0xC8);  // mov rax, r9 (y)
            emit8(0x49); emit8(0x0F); emit8(0xAF); emit8(0xC4);  // imul rax, r12 (width)
            emit8(0x4C); emit8(0x01); emit8(0xC0);  // add rax, r8 (x)
            emit8(0x48); emit8(0xC1); emit8(0xE0); emit8(0x02);  // shl rax, 2 (*4)
            emit8(0x4C); emit8(0x01); emit8(0xD8);  // add rax, r11 (framebuffer)

            // Store color
            emit8(0x44); emit8(0x89); emit8(0x10);  // mov [rax], r10d

            regsUsed = 1;
            return 0;
        }

        // --- clear(color) ---
        if (call->name == "clear" && call->args.size() == 1) {
            int saved = regsUsed; regsUsed = 0;

            int colorReg = emitExpr(call->args[0].get());
            if (colorReg != 0) { emitMovReg(0, colorReg); freeReg(colorReg); }
            else freeReg(0);

            // lea rbx, [rip + win32Globals]
            emit8(0x48); emit8(0x8D); emit8(0x1D);
            heapFixups.push_back({code.size(), win32GlobalsRVA});
            emit32(0);

            // rbx = globals, eax = color
            // framebuf = [rbx+40], width = [rbx+32], height = [rbx+36]
            emit8(0x4C); emit8(0x8B); emit8(0x4B); emit8(0x28);  // mov r9, [rbx+40] (framebuf)
            emit8(0x44); emit8(0x8B); emit8(0x43); emit8(0x20);  // mov r8d, [rbx+32] (width)
            emit8(0x44); emit8(0x8B); emit8(0x5B); emit8(0x24);  // mov r11d, [rbx+36] (height)

            // rcx = width * height
            emit8(0x4C); emit8(0x89); emit8(0xC1);  // mov rcx, r8
            emit8(0x41); emit8(0x0F); emit8(0xAF); emit8(0xCB);  // imul rcx, r11

            // Save rdi, set up rep stosd
            emit8(0x57);  // push rdi
            emit8(0x4C); emit8(0x89); emit8(0xCF);  // mov rdi, r9 (framebuf)
            // mov ecx, ecx (zero-extend ecx to rcx, already done)
            emit8(0xF3); emit8(0xAB);  // rep stosd
            emit8(0x5F);  // pop rdi

            regsUsed = 1;
            return 0;
        }

        // --- getKey(vk) ---
        if (call->name == "getKey" && call->args.size() == 1) {
            int saved = regsUsed; regsUsed = 0;

            int vkReg = emitExpr(call->args[0].get());
            if (vkReg != 0) { emitMovReg(0, vkReg); freeReg(vkReg); }
            else freeReg(0);

            // GetAsyncKeyState takes int arg in ecx
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);
            emit8(0x89); emit8(0xC1);  // mov ecx, eax
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "GetAsyncKeyState", "user32.dll"});
            emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);

            // Result in eax, return 1 if high bit set
            emit8(0xC1); emit8(0xE8); emit8(0x0F);  // shr eax, 15
            emit8(0x83); emit8(0xE0); emit8(0x01);  // and eax, 1

            regsUsed = 1;
            return 0;
        }

        // --- closeWindow() ---
        if (call->name == "closeWindow" && call->args.empty()) {
            int saved = regsUsed; regsUsed = 0;

            // lea rax, [rip + win32Globals]
            emit8(0x48); emit8(0x8D); emit8(0x05);
            heapFixups.push_back({code.size(), win32GlobalsRVA});
            emit32(0);

            // mov rbx, rax (save globals addr)
            emit8(0x48); emit8(0x89); emit8(0xC3);

            // DestroyWindow(hwnd) - hwnd at globals+8
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x08);  // mov rcx, [rbx+8] (hwnd)
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "DestroyWindow", "user32.dll"});
            emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);

            regsUsed = (uint8_t)saved;
            int r = allocReg(); if (r != 0) { emitMovReg(r, 0); freeReg(0); }
            return r >= 0 ? r : 0;
        }

        // --- createWindow(w, h, title) ---
        if (call->name == "createWindow" && call->args.size() == 3) {
            int saved = regsUsed; regsUsed = 0;

            int titleIdx = -1;
            if (auto strExpr = dynamic_cast<StringExpr*>(call->args[2].get())) {
                for (size_t i = 0; i < stringPool.size(); i++) {
                    if (stringPool[i] == strExpr->value) { titleIdx = (int)i; break; }
                }
                if (titleIdx < 0) {
                    titleIdx = (int)stringPool.size();
                    stringPool.push_back(strExpr->value);
                }
            }

            // Load globals base into rbx
            emit8(0x48); emit8(0x8D); emit8(0x1D);
            heapFixups.push_back({code.size(), win32GlobalsRVA});
            emit32(0);

            // Check init flag, skip if already initialized
            emit8(0x48); emit8(0x83); emit8(0x3B); emit8(0x00);
            int skipAll = newLabel();
            emit8(0x0F); emit8(0x85);
            jmpFixups.push_back({(int)code.size(), skipAll});
            emit32(0);

            // Set init flag
            emit8(0x48); emit8(0xC7); emit8(0x03); emit32(1);

            // Save callee-saved registers we'll use (rdi = hInstance, rsi = hdc)
            emit8(0x57);  // push rdi
            emit8(0x56);  // push rsi

            // Evaluate w and h, store in globals
            int wReg = emitExpr(call->args[0].get());
            if (wReg != 0) { emitMovReg(0, wReg); freeReg(wReg); }
            else freeReg(0);
            emit8(0x89); emit8(0x43); emit8(0x20);

            int hReg = emitExpr(call->args[1].get());
            if (hReg != 0) { emitMovReg(0, hReg); freeReg(hReg); }
            else freeReg(0);
            emit8(0x89); emit8(0x43); emit8(0x24);

            // GetModuleHandleA(NULL) → rax = hInstance
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);
            emit8(0x33); emit8(0xC9);
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "GetModuleHandleA", "kernel32.dll"});
            emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);
            emit8(0x48); emit8(0x89); emit8(0xC7);  // rdi = hInstance

            // Build WNDCLASSEXA on stack (above shadow space)
            emit8(0x48); emit8(0x81); emit8(0xEC); emit32(0x80);
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(80);
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x24); emit32(3);
            emit8(0x48); emit8(0x8D); emit8(0x05);
            emit32((uint32_t)(int32_t)(wndProcOffset - ((int)code.size() + 4)));
            emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x28);
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x30); emit32(0);
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x34); emit32(0);
            emit8(0x48); emit8(0x89); emit8(0x7C); emit8(0x24); emit8(0x38);
            for (int i = 0; i < 4; i++) {
                emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x40 + i*8); emit32(0);
            }
            // lpszClassName
            emit8(0x48); emit8(0x8D); emit8(0x05);
            heapFixups.push_back({code.size(), classNameRVA}); emit32(0);
            emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x60);
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x68); emit32(0);

            // RegisterClassExA(rsp+0x20)
            emit8(0x48); emit8(0x8D); emit8(0x4C); emit8(0x24); emit8(0x20);
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "RegisterClassExA", "user32.dll"}); emit32(0);
            emit8(0x48); emit8(0x81); emit8(0xC4); emit32(0x80);

            // --- CreateWindowExA ---
            emit8(0x48); emit8(0x81); emit8(0xEC); emit32(0x60);
            emit8(0x33); emit8(0xC9);                              // rcx = 0 (dwExStyle)
            emit8(0x48); emit8(0x8D); emit8(0x15);                 // lea rdx, [rip + className]
            heapFixups.push_back({code.size(), classNameRVA}); emit32(0);
            emit8(0x4C); emit8(0x8D); emit8(0x05);                 // lea r8, [rip + title]
            heapFixups.push_back({code.size(), stringRVA + stringOffsets[titleIdx]}); emit32(0);
            emit8(0x41); emit8(0xB9); emit32(0x00CF0000);          // r9d = WS_OVERLAPPEDWINDOW
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(0x80000000);  // X = CW_USEDEFAULT
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x28); emit32(0x80000000);  // Y = CW_USEDEFAULT
            emit8(0x8B); emit8(0x43); emit8(0x20);                // eax = width
            emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x30);   // nWidth
            emit8(0x8B); emit8(0x43); emit8(0x24);                // eax = height
            emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x38);   // nHeight
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x40); emit32(0);  // hWndParent = NULL
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x48); emit32(0);  // hMenu = NULL
            emit8(0x48); emit8(0x89); emit8(0x7C); emit8(0x24); emit8(0x50);  // hInstance
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x58); emit32(0);  // lpParam = NULL
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "CreateWindowExA", "user32.dll"}); emit32(0);
            emit8(0x48); emit8(0x81); emit8(0xC4); emit32(0x60);

            emit8(0x48); emit8(0x89); emit8(0x43); emit8(0x08);  // [rbx+8] = hwnd
            emit8(0x48); emit8(0x89); emit8(0xC6);                // rsi = hwnd

            // ShowWindow(hwnd, SW_SHOWDEFAULT=10)
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);
            emit8(0x48); emit8(0x89); emit8(0xF1);                // rcx = hwnd
            emit8(0xBA); emit32(10);
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "ShowWindow", "user32.dll"}); emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);

            // UpdateWindow(hwnd)
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);
            emit8(0x48); emit8(0x89); emit8(0xF1);                // rcx = hwnd
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "UpdateWindow", "user32.dll"}); emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);

            // GetDC(hwnd) → eax = hdc
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);
            emit8(0x48); emit8(0x89); emit8(0xF1);                // rcx = hwnd
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "GetDC", "user32.dll"}); emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);

            // Save hdc, set up double-buffer
            emit8(0x89); emit8(0xC6);                              // esi = hdc

            // CreateCompatibleDC(hdc) → hdcMem
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);
            emit8(0x89); emit8(0xF1);                              // ecx = hdc
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "CreateCompatibleDC", "gdi32.dll"}); emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);
            emit8(0x48); emit8(0x89); emit8(0x43); emit8(0x10);   // [rbx+16] = hdcMem
            emit8(0x48); emit8(0x89); emit8(0xC2);                // rdx = hdcMem (for SelectObject)

            // Build BITMAPINFO on stack, call CreateDIBSection
            emit8(0x48); emit8(0x81); emit8(0xEC); emit32(0x60);
            // [rsp+0x20]: hSection = NULL (5th arg)
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(0);
            // [rsp+0x28]: dwOffset = 0 (6th arg)
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x28); emit32(0);
            // BITMAPINFO at [rsp+0x30] (40 bytes)
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x30); emit32(40);   // biSize
            emit8(0x8B); emit8(0x43); emit8(0x20);                // eax = width
            emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x34);   // biWidth
            emit8(0x8B); emit8(0x43); emit8(0x24);                // eax = height
            emit8(0xF7); emit8(0xD8);                              // neg eax (top-down)
            emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x38);   // biHeight
            emit8(0x66); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x3C); emit16(1);   // biPlanes
            emit8(0x66); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x3E); emit16(32);  // biBitCount
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x40); emit32(0);  // biCompression
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x44); emit32(0);  // biSizeImage
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x48); emit32(0);  // biXPelsPerMeter
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x4C); emit32(0);  // biYPelsPerMeter
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x50); emit32(0);  // biClrUsed
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x54); emit32(0);  // biClrImportant
            // pBits at [rsp+0x58]
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x58); emit32(0);
            // CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0)
            emit8(0x89); emit8(0xF1);                              // rcx = hdc
            emit8(0x48); emit8(0x8D); emit8(0x54); emit8(0x24); emit8(0x30);  // rdx = &bmi
            emit8(0x45); emit8(0x33); emit8(0xC0);                 // r8d = 0 (DIB_RGB_COLORS)
            emit8(0x4C); emit8(0x8D); emit8(0x4C); emit8(0x24); emit8(0x58);  // r9 = &pBits
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "CreateDIBSection", "gdi32.dll"}); emit32(0);
            emit8(0x48); emit8(0x89); emit8(0x43); emit8(0x18);   // [rbx+24] = hBitmap
            emit8(0x48); emit8(0x8B); emit8(0x44); emit8(0x24); emit8(0x58);  // rax = pBits
            emit8(0x48); emit8(0x89); emit8(0x43); emit8(0x28);   // [rbx+40] = framebuffer bits
            emit8(0x48); emit8(0x81); emit8(0xC4); emit32(0x60);

            // SelectObject(hdcMem, hBitmap)
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x10);   // rcx = hdcMem
            emit8(0x48); emit8(0x8B); emit8(0x53); emit8(0x18);   // rdx = hBitmap
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "SelectObject", "gdi32.dll"}); emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);

            // ReleaseDC(hwnd, hdc)
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x08);   // rcx = hwnd
            emit8(0x89); emit8(0xF2);                              // edx = hdc
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "ReleaseDC", "user32.dll"}); emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);

            // Restore callee-saved registers
            emit8(0x5E);  // pop rsi
            emit8(0x5F);  // pop rdi

            emitLabel(skipAll);

            regsUsed = 1;
            emitMovRegImm(0, 0);
            return 0;
        }

        // --- present() ---
        if (call->name == "present" && call->args.empty()) {
            int saved = regsUsed; regsUsed = 0;

            // lea rbx, [rip + win32Globals]
            emit8(0x48); emit8(0x8D); emit8(0x1D);
            heapFixups.push_back({code.size(), win32GlobalsRVA});
            emit32(0);

            // Allocate frame for GetDC + BitBlt + ReleaseDC
            emit8(0x48); emit8(0x81); emit8(0xEC); emit32(0x50);

            // GetDC(hwnd) → rax = hdc
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x08);  // rcx = hwnd
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "GetDC", "user32.dll"});
            emit32(0);

            // Save hdc at [rsp+0x00] (shadow space, no longer needed by GetDC)
            emit8(0x89); emit8(0x04); emit8(0x24);                // mov [rsp], eax

            // BitBlt(hdc, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY)
            emit8(0x8B); emit8(0x0C); emit8(0x24);                // ecx = saved hdc
            emit8(0x33); emit8(0xD2);                              // edx = 0 (x)
            emit8(0x45); emit8(0x33); emit8(0xC0);                 // r8d = 0 (y)
            emit8(0x44); emit8(0x8B); emit8(0x4B); emit8(0x20);   // r9d = width
            emit8(0x8B); emit8(0x43); emit8(0x24);                // eax = height
            emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x20);   // [rsp+0x20] = height
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x10);   // rax = hdcMem
            emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x28);  // [rsp+0x28] = hdcMem
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x30); emit32(0);  // nXSrc = 0
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x38); emit32(0);  // nYSrc = 0
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x40); emit32(0x00CC0020);  // dwRop = SRCCOPY
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "BitBlt", "gdi32.dll"});
            emit32(0);

            // ReleaseDC(hwnd, hdc)
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x08);   // rcx = hwnd
            emit8(0x8B); emit8(0x14); emit8(0x24);                // edx = saved hdc
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "ReleaseDC", "user32.dll"});
            emit32(0);

            // --- Message pump (process pending Windows messages) ---
            int pumpLoop = newLabel();
            int pumpDone = newLabel();

            // sub rsp, 0x60 for MSG struct (48 bytes) + shadow space (32 bytes)
            emit8(0x48); emit8(0x81); emit8(0xEC); emit32(0x60);

            emitLabel(pumpLoop);

            // PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)
            emit8(0x48); emit8(0x8D); emit8(0x4C); emit8(0x24); emit8(0x28);  // lea rcx, [rsp+0x28]
            emit8(0x33); emit8(0xD2);                                          // xor edx, edx
            emit8(0x45); emit8(0x33); emit8(0xC0);                             // xor r8d, r8d
            emit8(0x45); emit8(0x33); emit8(0xC9);                             // xor r9d, r9d
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(1);    // mov dword [rsp+0x20], PM_REMOVE
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "PeekMessageA", "user32.dll"});
            emit32(0);

            // if (PeekMessage returned 0) goto pumpDone
            emit8(0x85); emit8(0xC0);  // test eax, eax
            emitJcc("==", pumpDone);

            // TranslateMessage(&msg)
            emit8(0x48); emit8(0x8D); emit8(0x4C); emit8(0x24); emit8(0x28);  // lea rcx, [rsp+0x28]
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "TranslateMessage", "user32.dll"});
            emit32(0);

            // DispatchMessageA(&msg)
            emit8(0x48); emit8(0x8D); emit8(0x4C); emit8(0x24); emit8(0x28);  // lea rcx, [rsp+0x28]
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "DispatchMessageA", "user32.dll"});
            emit32(0);

            // loop back
            emitJmp(pumpLoop);
            emitLabel(pumpDone);

            // add rsp, 0x60 (cleanup MSG frame)
            emit8(0x48); emit8(0x81); emit8(0xC4); emit32(0x60);

            // --- Sleep(1) to yield CPU time ---
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);  // sub rsp, 0x20
            emit8(0xB9); emit32(1);                                // mov ecx, 1
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "Sleep", "kernel32.dll"});
            emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);  // add rsp, 0x20

            emit8(0x48); emit8(0x81); emit8(0xC4); emit32(0x50);

            regsUsed = 1;
            emitMovRegImm(0, 0);
            return 0;
        }

        // --- processMessages() ---
        if (call->name == "processMessages" && call->args.empty()) {
            int saved = regsUsed; regsUsed = 0;

            // Stack: 0x60
            // [rsp+0x00..0x1F]: shadow for API calls
            // [rsp+0x20]: 5th arg wRemoveMsg = PM_REMOVE = 1
            // [rsp+0x28..0x57]: MSG structure (48 bytes)
            emit8(0x48); emit8(0x81); emit8(0xEC); emit32(0x60);

            // PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)
            emit8(0x48); emit8(0x8D); emit8(0x4C); emit8(0x24); emit8(0x28);  // rcx = &msg
            emit8(0x33); emit8(0xD2);  // rdx = NULL
            emit8(0x45); emit8(0x33); emit8(0xC0);  // r8d = 0
            emit8(0x45); emit8(0x33); emit8(0xC9);  // r9d = 0
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(1);  // wRemoveMsg = PM_REMOVE
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "PeekMessageA", "user32.dll"});
            emit32(0);

            // If PeekMessage returned 0, skip to return 1
            emit8(0x85); emit8(0xC0);  // test eax, eax
            int pumpDone = newLabel();
            emitJcc("==", pumpDone);

            // Check if message == WM_QUIT (0x0012)
            emit8(0x83); emit8(0x7C); emit8(0x24); emit8(0x30); emit8(0x12);
            int notQuit = newLabel();
            emitJcc("!=", notQuit);

            // WM_QUIT: return 0
            emit8(0x33); emit8(0xC0);  // xor eax, eax
            int done = newLabel();
            emitJmp(done);

            emitLabel(notQuit);

            // TranslateMessage(&msg)
            emit8(0x48); emit8(0x8D); emit8(0x4C); emit8(0x24); emit8(0x28);
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "TranslateMessage", "user32.dll"});
            emit32(0);

            // DispatchMessageA(&msg)
            emit8(0x48); emit8(0x8D); emit8(0x4C); emit8(0x24); emit8(0x28);
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "DispatchMessageA", "user32.dll"});
            emit32(0);

            emitLabel(pumpDone);

            // Return 1 (continue looping)
            emit8(0xB8); emit32(1);

            emitLabel(done);

            emit8(0x48); emit8(0x81); emit8(0xC4); emit32(0x60);

            regsUsed = 1;
            return 0;
        }

        } // end GUI-only graphics builtins

        int savedRegs = regsUsed;
        int savedXmmRegs = xmmRegsUsed;
        regsUsed = 0;
        xmmRegsUsed = 0;

        // Count stack args (args beyond first 4 go on stack)
        int totalArgs = (int)call->args.size();
        int stackSlots = totalArgs > 4 ? totalArgs - 4 : 0;
        int stackAlloc = 0x20 + stackSlots * 8;
        if (stackAlloc <= 127) {
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8((uint8_t)stackAlloc);
        } else {
            emit8(0x48); emit8(0x81); emit8(0xEC); emit32((uint32_t)stackAlloc);
        }

        int intArgCount = 0;
        int floatArgCount = 0;

        for (size_t i = 0; i < call->args.size(); i++) {
            bool isFloatArg = isFloatExpr(call->args[i].get());
            if (isFloatArg) {
                if (floatArgCount < 4) {
                    int xmmIdx = floatArgCount;
                    int x = emitFloatExpr(call->args[i].get());
                    if (x != xmmIdx) {
                        emitMovssXmm(xmmIdx, x);
                        freeXmmReg(x);
                    }
                    floatArgCount++;
                } else {
                    int stackOff = 0x20 + (floatArgCount - 4) * 8;
                    int x = emitFloatExpr(call->args[i].get());
                    emitMovssXmmToMem(x, 4, 0);
                    emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x04);
                    if (stackOff < 128) {
                        emit8(0xF3); emit8(0x0F); emit8(0x10); emit8(0x44); emit8(0x24); emit8((uint8_t)(stackOff - 4));
                    } else {
                        emit8(0xF3); emit8(0x0F); emit8(0x10); emit8(0x84); emit8(0x24); emit32((uint32_t)(stackOff - 4));
                    }
                    emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x04);
                    freeXmmReg(x);
                    floatArgCount++;
                }
            } else {
                int argReg = emitExpr(call->args[i].get());
                if (intArgCount < 4) {
                    if (intArgCount == 0) {
                        if (argReg != 1) { emitMovReg(1, argReg); freeReg(argReg); }
                        regsUsed |= (1 << 1);
                    } else if (intArgCount == 1) {
                        if (argReg != 2) { emitMovReg(2, argReg); freeReg(argReg); }
                        regsUsed |= (1 << 2);
                    } else if (intArgCount == 2) {
                        if (argReg == 0) {
                            emit8(0x49); emit8(0x89); emit8(0xC0);  // MOV R8, RAX
                        } else if (argReg == 1) {
                            emit8(0x49); emit8(0x89); emit8(0xC8);  // MOV R8, RCX
                        } else if (argReg == 2) {
                            emit8(0x49); emit8(0x89); emit8(0xD0);  // MOV R8, RDX
                        } else if (argReg == 3) {
                            emit8(0x49); emit8(0x89); emit8(0xD8);  // MOV R8, RBX
                        } else {
                            emitMovReg(0, argReg);
                            emit8(0x49); emit8(0x89); emit8(0xC0);  // MOV R8, RAX
                        }
                        freeReg(argReg);
                    } else if (intArgCount == 3) {
                        if (argReg == 0) {
                            emit8(0x49); emit8(0x89); emit8(0xC1);  // MOV R9, RAX
                        } else if (argReg == 1) {
                            emit8(0x49); emit8(0x89); emit8(0xC9);  // MOV R9, RCX
                        } else if (argReg == 2) {
                            emit8(0x49); emit8(0x89); emit8(0xD1);  // MOV R9, RDX
                        } else if (argReg == 3) {
                            emit8(0x49); emit8(0x89); emit8(0xD9);  // MOV R9, RBX
                        } else {
                            emitMovReg(0, argReg);
                            emit8(0x49); emit8(0x89); emit8(0xC1);  // MOV R9, RAX
                        }
                        freeReg(argReg);
                    }
                    intArgCount++;
                } else {
                    int stackOff = 0x20 + (intArgCount - 4) * 8;
                    if (argReg != 0) { emitMovReg(0, argReg); freeReg(argReg); }
                    if (stackOff < 128) {
                        emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8((uint8_t)stackOff);
                    } else {
                        emit8(0x48); emit8(0x89); emit8(0x84); emit8(0x24); emit32((uint32_t)stackOff);
                    }
                    intArgCount++;
                }
            }
        }

        bool isImportCall = false;
        std::string importDll;
        for (auto& func : prog.functions) {
            if (func->isExtern && func->name == call->name) {
                isImportCall = true;
                importDll = func->dllName;
                break;
            }
        }

        if (isImportCall) {
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), call->name, importDll});
            emit32(0);
        } else {
            emit8(0xE8);
            size_t fixupPos = code.size();
            emit32(0);
            callFixups.push_back({fixupPos, call->name});
        }

        if (stackAlloc <= 127) {
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8((uint8_t)stackAlloc);
        } else {
            emit8(0x48); emit8(0x81); emit8(0xC4); emit32((uint32_t)stackAlloc);
        }

        // All volatile regs clobbered by call; only RAX holds the return value
        regsUsed = 1;
        xmmRegsUsed = 0;
        return 0;
    }

    int r = allocReg(); if (r < 0) r = 0;
    emitMovRegImm(r, 0);
    return r;
}

void Codegen::emitStmt(Stmt* stmt, const Type* stmtType) {
    if (auto ret = dynamic_cast<ReturnStmt*>(stmt)) {
        if (ret->value) {
            int r = emitExpr(ret->value.get());
            if (r != 0) emitMovReg(0, r);
            regsUsed = 0;
        }
        if (funcEndLabel >= 0) emitJmp(funcEndLabel);
    } else if (auto exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        int r = emitExpr(exprStmt->expr.get());
        freeReg(r);
    } else if (auto varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->init) {
            if (varDecl->type.kind == TypeKind::Float) {
                int x = emitFloatExpr(varDecl->init.get());
                emitFloatStoreToBP(x, varInfos[varDecl->name].offset);
                freeXmmReg(x);
            } else {
                int r = emitExpr(varDecl->init.get());
                if (r != 0) { emitMovReg(0, r); freeReg(r); }
                emitStoreToBP64(varInfos[varDecl->name].offset);
                freeReg(0);
            }
        }
    } else if (auto assign = dynamic_cast<AssignStmt*>(stmt)) {
        if (assign->indexExpr) {
            auto vi = getVarInfo(assign->name);
            if (vi) {
                int elementSize = 4;
                if (vi->type.kind == TypeKind::Float) elementSize = 4;
                else elementSize = 8;
                emitLeaR10FromBP(vi->offset);
                int idxReg = emitExpr(assign->indexExpr.get());
                if (idxReg != 0) { emitMovReg(0, idxReg); freeReg(idxReg); idxReg = 0; }
                emit8(0x48); emit8(0x69); emit8(0xC0); emit32(elementSize);
                emit8(0x49); emit8(0x01); emit8(0xC2);
                freeReg(0);
                if (vi->type.kind == TypeKind::Float) {
                    int x = emitFloatExpr(assign->value.get());
                    emit8(0xF3); emit8(0x41); emit8(0x0F); emit8(0x11); emit8(0x02);
                    freeXmmReg(x);
                } else {
                    int r = emitExpr(assign->value.get());
                    if (r != 0) emitMovReg(0, r);
                    emit8(0x49); emit8(0x89); emit8(0x02);
                    freeReg(r);
                }
            }
        } else if (!assign->memberPath.empty()) {
            auto vi = getVarInfo(assign->name);
            if (vi) {
                auto slIt = structLayouts.find(vi->type.structName);
                if (slIt != structLayouts.end()) {
                    auto& layout = slIt->second;
                    std::string fieldName = assign->memberPath[0];
                    auto fIt = layout.fieldOffsets.find(fieldName);
                    auto fTypeIt = layout.fieldTypes.find(fieldName);
                    if (fIt != layout.fieldOffsets.end()) {
                        int totalOffset = vi->offset + fIt->second;
                        bool isFloatField = fTypeIt != layout.fieldTypes.end() && fTypeIt->second.kind == TypeKind::Float;
                        if (isFloatField) {
                            int x = emitFloatExpr(assign->value.get());
                            emitFloatStoreToBP(x, totalOffset);
                            freeXmmReg(x);
                        } else {
                            int r = emitExpr(assign->value.get());
                            if (r != 0) { emitMovReg(0, r); freeReg(r); }
                            emitStoreToBP64(totalOffset);
                            freeReg(0);
                        }
                    }
                }
            }
        } else {
            auto vi = getVarInfo(assign->name);
            if (vi && vi->type.kind == TypeKind::Float) {
                int x = emitFloatExpr(assign->value.get());
                emitFloatStoreToBP(x, vi->offset);
                freeXmmReg(x);
            } else {
                int r = emitExpr(assign->value.get());
                if (r != 0) { emitMovReg(0, r); freeReg(r); }
                emitStoreToBP64(varInfos[assign->name].offset);
                freeReg(0);
            }
        }
    } else if (auto ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        int elseLabel = newLabel();
        int endLabel = newLabel();
        int condReg;
        if (auto bin = dynamic_cast<BinaryExpr*>(ifStmt->condition.get())) {
            if (isFloatExpr(bin)) {
                condReg = emitBinaryExpr(bin, true);
            } else {
                condReg = emitExpr(ifStmt->condition.get());
            }
        } else {
            condReg = emitExpr(ifStmt->condition.get());
        }
        if (condReg == 0) { emit8(0x85); emit8(0xC0); }
        else if (condReg == 1) { emit8(0x85); emit8(0xC9); }
        else if (condReg == 2) { emit8(0x85); emit8(0xD2); }
        else if (condReg == 3) { emit8(0x85); emit8(0xDB); }
        freeReg(condReg);
        emit8(0x0F); emit8(0x84);
        jmpFixups.push_back({(int)code.size(), elseLabel});
        emit32(0);
        for (auto& s : ifStmt->thenBlock.stmts) emitStmt(s.get());
        emitJmp(endLabel);
        emitLabel(elseLabel);
        for (auto& s : ifStmt->elseBlock.stmts) emitStmt(s.get());
        emitLabel(endLabel);
    } else if (auto whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        int loopLabel = newLabel();
        int endLabel = newLabel();
        emitLabel(loopLabel);
        int condReg;
        if (auto bin = dynamic_cast<BinaryExpr*>(whileStmt->condition.get())) {
            if (isFloatExpr(bin)) {
                condReg = emitBinaryExpr(bin, true);
            } else {
                condReg = emitExpr(whileStmt->condition.get());
            }
        } else {
            condReg = emitExpr(whileStmt->condition.get());
        }
        if (condReg == 0) { emit8(0x85); emit8(0xC0); }
        else if (condReg == 1) { emit8(0x85); emit8(0xC9); }
        else if (condReg == 2) { emit8(0x85); emit8(0xD2); }
        else if (condReg == 3) { emit8(0x85); emit8(0xDB); }
        freeReg(condReg);
        emit8(0x0F); emit8(0x84);
        jmpFixups.push_back({(int)code.size(), endLabel});
        emit32(0);
        for (auto& s : whileStmt->body.stmts) emitStmt(s.get());
        emitJmp(loopLabel);
        emitLabel(endLabel);
    }
}

void Codegen::computeStructLayouts() {
    structLayouts.clear();
    for (auto& sd : prog.structs) {
        StructLayout layout;
        layout.name = sd->name;
        int offset = 0;
        for (auto& f : sd->fields) {
            layout.fieldOffsets[f.name] = offset;
            layout.fieldTypes[f.name] = f.type;
            int fieldSize = 0;
            switch (f.type.kind) {
                case TypeKind::Int:    fieldSize = 8; break;
                case TypeKind::Float:  fieldSize = 4; break;
                case TypeKind::Bool:   fieldSize = 4; break;
                case TypeKind::Vec2:   fieldSize = 8; break;
                case TypeKind::Vec3:   fieldSize = 12; break;
                case TypeKind::Color:  fieldSize = 16; break;
                case TypeKind::Struct: fieldSize = 8; break;
                default: fieldSize = 8; break;
            }
            offset += fieldSize;
        }
        layout.totalSize = offset;
        structLayouts[sd->name] = layout;
    }
}

void Codegen::emitFunction(FunctionDecl* func) {
    funcOffsets[func->name] = code.size();
    varInfos.clear();
    int locals = 0;

    for (size_t i = 0; i < func->params.size(); i++) {
        int off = (int)(16 + i * 8);
        VarInfo vi;
        vi.offset = off;
        vi.type = func->params[i].type;
        varInfos[func->params[i].name] = vi;
    }

    for (auto& stmt : func->body.stmts) {
        if (auto varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
            int fieldSize = 8;
            if (varDecl->arraySize > 0) {
                int elemSize = 8;
                if (varDecl->type.kind == TypeKind::Float) elemSize = 4;
                fieldSize = elemSize * varDecl->arraySize;
            } else if (varDecl->type.kind == TypeKind::Struct) {
                auto it = structLayouts.find(varDecl->type.structName);
                if (it != structLayouts.end()) {
                    fieldSize = it->second.totalSize;
                    if (fieldSize % 8 != 0) fieldSize += 8 - (fieldSize % 8);
                }
            } else if (varDecl->type.kind == TypeKind::Bool) {
                fieldSize = 4;
            } else if (varDecl->type.kind == TypeKind::Float) {
                fieldSize = 4;
            } else if (varDecl->type.kind == TypeKind::Vec2) {
                fieldSize = 8;
            } else if (varDecl->type.kind == TypeKind::Vec3) {
                fieldSize = 12;
                if (fieldSize % 8 != 0) fieldSize += 8 - (fieldSize % 8);
            } else if (varDecl->type.kind == TypeKind::Color) {
                fieldSize = 16;
            }
            locals += fieldSize;
            VarInfo vi;
            vi.offset = -(locals);
            vi.type = varDecl->type;
            varInfos[varDecl->name] = vi;
        }
    }

    frameSize = (locals + 15) & ~15;
    regsUsed = 0;
    xmmRegsUsed = 0;
    funcEndLabel = newLabel();

    emit8(0x55);
    emit8(0x48); emit8(0x89); emit8(0xE5);
    if (frameSize > 0) {
        if (frameSize <= 127) {
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8((uint8_t)frameSize);
        } else {
            emit8(0x48); emit8(0x81); emit8(0xEC); emit32((uint32_t)frameSize);
        }
    }

    for (size_t i = 0; i < func->params.size() && i < 4; i++) {
        int off = (int)(16 + i * 8);
        if (i == 0) {
            emit8(0x48); emit8(0x89); emit8(0x4D); emit8((uint8_t)(int8_t)off);
        } else if (i == 1) {
            emit8(0x48); emit8(0x89); emit8(0x55); emit8((uint8_t)(int8_t)off);
        } else if (i == 2) {
            emit8(0x4C); emit8(0x89); emit8(0x45); emit8((uint8_t)(int8_t)off);
        } else if (i == 3) {
            emit8(0x4C); emit8(0x89); emit8(0x4D); emit8((uint8_t)(int8_t)off);
        }
    }

    for (auto& stmt : func->body.stmts) {
        emitStmt(stmt.get());
    }

    emitLabel(funcEndLabel);

    if (frameSize > 0) {
        if (frameSize <= 127) {
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8((uint8_t)frameSize);
        } else {
            emit8(0x48); emit8(0x81); emit8(0xC4); emit32((uint32_t)frameSize);
        }
    }
    emit8(0x5D);
    emit8(0xC3);
}

void Codegen::emitEntryPoint() {
    entryPointCodeOffset = code.size();
    if (!embeddedDLLs.empty()) {
        // Extra stack space for loader (CreateFileA has 7 params, needs stack space)
        emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x58); // sub rsp, 0x58
        emitEmbeddedLoader();
    } else {
        // sub rsp, 0x28: 8 bytes for return addr offset + 32 bytes shadow space
        emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x28);
    }

    bool hasMain = funcOffsets.count("main") > 0;
    uint8_t frameSize = embeddedDLLs.empty() ? 0x28 : 0x58;
    if (hasMain) {
        emit8(0xE8);
        size_t fixupPos = code.size();
        emit32(0);
        callFixups.push_back({fixupPos, "main"});
        emit8(0x48); emit8(0x83); emit8(0xC4); emit8(frameSize);
        emit8(0x8B); emit8(0xC8);
    } else if (!prog.functions.empty()) {
        emit8(0xE8);
        size_t fixupPos = code.size();
        emit32(0);
        callFixups.push_back({fixupPos, prog.functions[0]->name});
        emit8(0x48); emit8(0x83); emit8(0xC4); emit8(frameSize);
        emit8(0x8B); emit8(0xC8);
    } else {
        emit8(0x48); emit8(0x83); emit8(0xC4); emit8(frameSize);
        emit8(0x33); emit8(0xC9);
    }

    emit8(0xFF); emit8(0x15);
    entryExitProcessFixup = code.size();
    emit32(0);
}

void Codegen::resolveFixups() {
    for (auto& f : callFixups) {
        auto it = funcOffsets.find(f.target);
        if (it == funcOffsets.end()) {
            std::cerr << "Error: undefined function '" << f.target << "'\n";
            continue;
        }
        int64_t rel = (int64_t)it->second - (int64_t)(f.codePos + 4);
        code[f.codePos]     = (uint8_t)(rel & 0xFF);
        code[f.codePos + 1] = (uint8_t)((rel >> 8) & 0xFF);
        code[f.codePos + 2] = (uint8_t)((rel >> 16) & 0xFF);
        code[f.codePos + 3] = (uint8_t)((rel >> 24) & 0xFF);
    }
    for (auto& f : funcRefFixups) {
        auto it = funcOffsets.find(f.target);
        if (it == funcOffsets.end()) {
            std::cerr << "Error: undefined function reference '" << f.target << "'\n";
            continue;
        }
        int64_t rel = (int64_t)it->second - (int64_t)(f.codePos + 4);
        code[f.codePos]     = (uint8_t)(rel & 0xFF);
        code[f.codePos + 1] = (uint8_t)((rel >> 8) & 0xFF);
        code[f.codePos + 2] = (uint8_t)((rel >> 16) & 0xFF);
        code[f.codePos + 3] = (uint8_t)((rel >> 24) & 0xFF);
    }
}

void Codegen::collectExprStrings(Expr* expr) {
    if (auto str = dynamic_cast<StringExpr*>(expr)) {
        for (auto& s : stringPool) {
            if (s == str->value) return;
        }
        stringPool.push_back(str->value);
    } else if (auto bin = dynamic_cast<BinaryExpr*>(expr)) {
        collectExprStrings(bin->left.get());
        collectExprStrings(bin->right.get());
    } else if (auto call = dynamic_cast<CallExpr*>(expr)) {
        for (auto& arg : call->args) collectExprStrings(arg.get());
    } else if (auto memb = dynamic_cast<MemberExpr*>(expr)) {
        collectExprStrings(memb->object.get());
    } else if (auto arr = dynamic_cast<ArrayAccessExpr*>(expr)) {
        collectExprStrings(arr->array.get());
        collectExprStrings(arr->index.get());
    }
}

void Codegen::collectStmtStrings(Stmt* stmt) {
    if (auto ret = dynamic_cast<ReturnStmt*>(stmt)) {
        if (ret->value) collectExprStrings(ret->value.get());
    } else if (auto exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        collectExprStrings(exprStmt->expr.get());
    } else if (auto assign = dynamic_cast<AssignStmt*>(stmt)) {
        collectExprStrings(assign->value.get());
    } else if (auto varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->init) collectExprStrings(varDecl->init.get());
    } else if (auto ifs = dynamic_cast<IfStmt*>(stmt)) {
        collectExprStrings(ifs->condition.get());
        for (auto& s : ifs->thenBlock.stmts) collectStmtStrings(s.get());
        for (auto& s : ifs->elseBlock.stmts) collectStmtStrings(s.get());
    } else if (auto wh = dynamic_cast<WhileStmt*>(stmt)) {
        collectExprStrings(wh->condition.get());
        for (auto& s : wh->body.stmts) collectStmtStrings(s.get());
    }
}

void Codegen::collectStrings() {
    for (auto& func : prog.functions) {
        if (!func->isExtern) {
            for (auto& stmt : func->body.stmts) collectStmtStrings(stmt.get());
        }
    }
}

void Codegen::resolveJmpFixups() {
    for (auto& f : jmpFixups) {
        int target = (f.targetPos < (int)labelPositions.size()) ? labelPositions[f.targetPos] : -1;
        if (target < 0) continue;
        int64_t rel = (int64_t)target - (int64_t)(f.codePos + 4);
        code[f.codePos]     = (uint8_t)(rel & 0xFF);
        code[f.codePos + 1] = (uint8_t)((rel >> 8) & 0xFF);
        code[f.codePos + 2] = (uint8_t)((rel >> 16) & 0xFF);
        code[f.codePos + 3] = (uint8_t)((rel >> 24) & 0xFF);
    }
}

void Codegen::readEmbeddedLibs() {
    // Check if any DLL needs embedding — either @import or extern func from
    bool hasEmbeddedImports = false;
    std::set<std::string> neededDLLs;

    // From @import directives
    for (auto& imp : prog.imports) {
        if (imp.dllName == "libs.dll" && !imp.module.empty()) {
            neededDLLs.insert("libs_" + imp.module + ".dll");
            hasEmbeddedImports = true;
        } else if (imp.dllName == "libs.dll" && imp.module.empty()) {
            neededDLLs.insert("libs_thread.dll");
            neededDLLs.insert("libs_mutex.dll");
            neededDLLs.insert("libs_async.dll");
            neededDLLs.insert("libs_threadpool.dll");
            hasEmbeddedImports = true;
        }
    }

    // From extern func from "xxx.dll" — collect non-system DLLs
    for (auto& func : prog.functions) {
        if (func->isExtern && !func->dllName.empty()) {
            std::string dll = func->dllName;
            if (dll != "kernel32.dll" && dll != "user32.dll" && dll != "gdi32.dll" && dll != "ntdll.dll") {
                neededDLLs.insert(dll);
                hasEmbeddedImports = true;
            }
        }
    }

    if (!hasEmbeddedImports) return;

    std::set<std::string> foundDLLs;

    // === Phase 1: Try to read from libs.dll container ===
    std::filesystem::path libsPath;
    if (!compilerDir.empty()) {
        libsPath = compilerDir / ".." / "libs" / "bin" / "libs.dll";
    }
    if (!std::filesystem::exists(libsPath) && !compilerDir.empty()) {
        libsPath = compilerDir / "libs.dll";
    }
    if (!std::filesystem::exists(libsPath) && !outputDir.empty()) {
        libsPath = outputDir / "libs.dll";
    }

    if (std::filesystem::exists(libsPath)) {
        std::ifstream f(std::filesystem::path(libsPath), std::ios::binary);
        if (f.is_open()) {
            char magic[8] = {};
            f.read(magic, 8);
            if (memcmp(magic, "ZLIBS", 5) == 0) {
                uint32_t count = 0;
                f.read((char*)&count, 4);

                for (uint32_t i = 0; i < count; i++) {
                    uint32_t nameLen = 0;
                    f.read((char*)&nameLen, 4);
                    if (nameLen > 256) break;
                    std::string name(nameLen, '\0');
                    f.read(&name[0], nameLen);
                    uint32_t dataLen = 0;
                    f.read((char*)&dataLen, 4);
                    if (dataLen > 10 * 1024 * 1024) break;
                    std::vector<uint8_t> data(dataLen);
                    f.read((char*)data.data(), dataLen);

                    if (neededDLLs.count(name)) {
                        EmbeddedDLL emb;
                        emb.dllName = name;
                        std::string prefix = "libs_";
                        std::string suffix = ".dll";
                        if (name.find(prefix) == 0 && name.size() > prefix.size() + suffix.size()) {
                            emb.moduleName = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
                        }
                        emb.bytes = std::move(data);
                        emb.blobSize = dataLen;
                        embeddedDLLs.push_back(std::move(emb));
                        foundDLLs.insert(name);
                        std::cout << "Embedded: " << name << " (" << dataLen << " bytes)" << std::endl;
                    }
                }
            }
        }
    }

    // === Phase 2: For DLLs not found in container, read individual files ===
    for (auto& dll : neededDLLs) {
        if (foundDLLs.count(dll)) continue;

        std::filesystem::path dllPath;
        if (!outputDir.empty()) {
            dllPath = outputDir / dll;
        }
        if (dllPath.empty() || !std::filesystem::exists(dllPath)) {
            if (!compilerDir.empty()) {
                dllPath = compilerDir / ".." / "libs" / "bin" / dll;
            }
        }
        if (dllPath.empty() || !std::filesystem::exists(dllPath)) {
            if (!compilerDir.empty()) {
                dllPath = compilerDir / dll;
            }
        }
        if (dllPath.empty() || !std::filesystem::exists(dllPath)) {
            dllPath = std::filesystem::current_path() / dll;
        }
        if (dllPath.empty() || !std::filesystem::exists(dllPath)) {
            // Try current directory directly
            dllPath = dll;
        }
        if (!std::filesystem::exists(dllPath)) continue;

        std::ifstream f(std::filesystem::path(dllPath), std::ios::binary | std::ios::ate);
        if (!f.is_open()) continue;

        std::streamsize size = f.tellg();
        if (size <= 0) continue;
        f.seekg(0, std::ios::beg);
        std::vector<uint8_t> data(size);
        f.read((char*)data.data(), size);
        if (data.empty()) continue;

        EmbeddedDLL emb;
        emb.dllName = dll;
        emb.bytes = std::move(data);
        emb.blobSize = (uint32_t)size;
        embeddedDLLs.push_back(std::move(emb));
        foundDLLs.insert(dll);
        std::cout << "Embedded: " << dll << " (" << size << " bytes)" << std::endl;
    }
}

void Codegen::buildImportData() {
    rdata.clear();
    data.clear();
    importDLLs.clear();
    externFuncMap.clear();

    auto writeDW = [](std::vector<uint8_t>& buf, uint32_t val) {
        buf.push_back(val & 0xFF);
        buf.push_back((val >> 8) & 0xFF);
        buf.push_back((val >> 16) & 0xFF);
        buf.push_back((val >> 24) & 0xFF);
    };
    auto writeDQ = [](std::vector<uint8_t>& buf, uint64_t val) {
        buf.push_back(val & 0xFF);
        buf.push_back((val >> 8) & 0xFF);
        buf.push_back((val >> 16) & 0xFF);
        buf.push_back((val >> 24) & 0xFF);
        buf.push_back((val >> 32) & 0xFF);
        buf.push_back((val >> 40) & 0xFF);
        buf.push_back((val >> 48) & 0xFF);
        buf.push_back((val >> 56) & 0xFF);
    };

    // Collect extern functions grouped by DLL
    std::unordered_map<std::string, std::vector<std::string>> dllFuncMap;

    // Auto-map function names to DLLs
    auto mapFuncToDll = [](const std::string& funcName) -> std::string {
        if (funcName == "ExitProcess" || funcName == "GetStdHandle" ||
            funcName == "WriteFile" || funcName == "ReadFile" ||
            funcName == "HeapAlloc" || funcName == "HeapFree" ||
            funcName == "GetProcessHeap" || funcName == "GetModuleHandleA" ||
            funcName == "Sleep") {
            return "kernel32.dll";
        }
        if (funcName.find("CreateWindowExA") == 0 || funcName.find("DefWindowProcA") == 0 ||
            funcName.find("RegisterClassExA") == 0 || funcName.find("DestroyWindow") == 0 ||
            funcName.find("GetDC") == 0 || funcName.find("ReleaseDC") == 0 ||
            funcName.find("PeekMessageA") == 0 || funcName.find("TranslateMessage") == 0 ||
            funcName.find("DispatchMessageA") == 0 || funcName.find("GetAsyncKeyState") == 0 ||
            funcName.find("PostQuitMessage") == 0 || funcName.find("BeginPaint") == 0 ||
            funcName.find("EndPaint") == 0 ||
            funcName.find("CreateWindowEx") == 0 || funcName.find("DefWindowProc") == 0 ||
            funcName.find("RegisterClass") == 0) {
            return "user32.dll";
        }
        if (funcName.find("CreateDIBSection") == 0 || funcName.find("BitBlt") == 0 ||
            funcName.find("DeleteObject") == 0 || funcName.find("SelectObject") == 0 ||
            funcName.find("DeleteDC") == 0 || funcName.find("CreateCompatibleDC") == 0) {
            return "gdi32.dll";
        }

        // Libs DLL functions
        if (funcName.find("thread") == 0 || funcName.find("Thread") == 0) return "libs_thread.dll";
        if (funcName.find("mutex") == 0 || funcName.find("Mutex") == 0 ||
            funcName.find("condvar") == 0 || funcName.find("Condvar") == 0) return "libs_mutex.dll";
        if (funcName.find("future") == 0 || funcName.find("Future") == 0 ||
            funcName.find("promise") == 0 || funcName.find("Promise") == 0) return "libs_async.dll";
        if (funcName.find("pool") == 0 || funcName.find("Pool") == 0 ||
            funcName.find("PoolWorker") == 0) return "libs_threadpool.dll";

        return "kernel32.dll";
    };

    // Always add kernel32 functions used by built-in features
    dllFuncMap["kernel32.dll"].push_back("ExitProcess");
    dllFuncMap["kernel32.dll"].push_back("GetStdHandle");
    dllFuncMap["kernel32.dll"].push_back("WriteFile");
    dllFuncMap["kernel32.dll"].push_back("ReadFile");
    dllFuncMap["kernel32.dll"].push_back("GetModuleHandleA");
    dllFuncMap["kernel32.dll"].push_back("Sleep");

    // Pre-add user32.dll and gdi32.dll functions only for GUI applications
    if (prog.appType == AppType::GUI) {
        dllFuncMap["user32.dll"].push_back("CreateWindowExA");
        dllFuncMap["user32.dll"].push_back("DefWindowProcA");
        dllFuncMap["user32.dll"].push_back("RegisterClassExA");
        dllFuncMap["user32.dll"].push_back("DestroyWindow");
        dllFuncMap["user32.dll"].push_back("GetDC");
        dllFuncMap["user32.dll"].push_back("ReleaseDC");
        dllFuncMap["user32.dll"].push_back("PeekMessageA");
        dllFuncMap["user32.dll"].push_back("TranslateMessage");
        dllFuncMap["user32.dll"].push_back("DispatchMessageA");
        dllFuncMap["user32.dll"].push_back("GetAsyncKeyState");
        dllFuncMap["user32.dll"].push_back("PostQuitMessage");
        dllFuncMap["user32.dll"].push_back("BeginPaint");
        dllFuncMap["user32.dll"].push_back("EndPaint");
        dllFuncMap["user32.dll"].push_back("GetMessageA");
        dllFuncMap["user32.dll"].push_back("ShowWindow");
        dllFuncMap["user32.dll"].push_back("UpdateWindow");
        dllFuncMap["gdi32.dll"].push_back("CreateDIBSection");
        dllFuncMap["gdi32.dll"].push_back("BitBlt");
        dllFuncMap["gdi32.dll"].push_back("SelectObject");
        dllFuncMap["gdi32.dll"].push_back("DeleteObject");
        dllFuncMap["gdi32.dll"].push_back("DeleteDC");
        dllFuncMap["gdi32.dll"].push_back("CreateCompatibleDC");
    }

    // Collect from extern functions
    for (auto& func : prog.functions) {
        if (func->isExtern) {
            std::string dllName = func->dllName.empty() ? mapFuncToDll(func->name) : func->dllName;
            dllFuncMap[dllName].push_back(func->name);
            externFuncMap[func->name] = {dllName, 0};
        }
    }

    // Collect from @import directives
    // Module mapping: "libs.dll::thread" -> libs_thread.dll, etc.
    // "libs.dll" (no ::) -> all sub-DLLs
    for (auto& imp : prog.imports) {
        if (imp.dllName == "libs.dll" && !imp.module.empty()) {
            // Selective import: libs.dll::thread -> libs_thread.dll + deps
            std::string mod = imp.module;
            std::string resolvedDll = "libs_" + mod + ".dll";

            auto ensureDll = [&](const std::string& dll) {
                if (dllFuncMap.find(dll) == dllFuncMap.end()) {
                    dllFuncMap[dll] = {};
                }
            };

            ensureDll(resolvedDll);

            // Add dependencies
            if (mod == "async" || mod == "pool") {
                ensureDll("libs_thread.dll");
                ensureDll("libs_mutex.dll");
            }
        } else if (imp.dllName == "libs.dll" && imp.module.empty()) {
            // Full import: libs.dll -> all sub-DLLs
            auto ensureDll = [&](const std::string& dll) {
                if (dllFuncMap.find(dll) == dllFuncMap.end()) {
                    dllFuncMap[dll] = {};
                }
            };
            ensureDll("libs_thread.dll");
            ensureDll("libs_mutex.dll");
            ensureDll("libs_async.dll");
            ensureDll("libs_threadpool.dll");
        } else {
            // Direct DLL import: @import("libs_thread.dll") or any other
            if (dllFuncMap.find(imp.dllName) == dllFuncMap.end()) {
                dllFuncMap[imp.dllName] = {};
            }
        }
    }

    // Remove duplicates from each DLL's function list
    for (auto& [dll, funcs] : dllFuncMap) {
        std::sort(funcs.begin(), funcs.end());
        funcs.erase(std::unique(funcs.begin(), funcs.end()), funcs.end());
    }

    // === EMBEDDED DLL HANDLING ===
    // Read libs.dll container and extract needed sub-DLLs
    readEmbeddedLibs();

    // Map embedded DLL functions and remove from IAT list
    std::set<std::string> embeddedDLLNames;
    for (auto& emb : embeddedDLLs) {
        embeddedDLLNames.insert(emb.dllName);
        auto it = dllFuncMap.find(emb.dllName);
        if (it != dllFuncMap.end()) {
            for (auto& fn : it->second) {
                emb.funcs.push_back({fn});
            }
            dllFuncMap.erase(it);
        }
    }

    // If we have embedded DLLs, add kernel32 imports needed by the loader
    if (!embeddedDLLs.empty()) {
        auto addK32 = [&](const std::string& fn) {
            auto& kf = dllFuncMap["kernel32.dll"];
            if (std::find(kf.begin(), kf.end(), fn) == kf.end())
                kf.push_back(fn);
        };
        addK32("GetTempPathA");
        addK32("lstrcatA");
        addK32("CreateFileA");
        addK32("WriteFile");
        addK32("CloseHandle");
        addK32("LoadLibraryA");
        addK32("GetProcAddress");
        addK32("DeleteFileA");
    }

    struct DLLBuild {
        std::string dllName;
        std::vector<std::string> funcs;
        std::vector<uint32_t> hintNameRVAs;
        uint32_t iltRVA = 0;
        uint32_t nameRVA = 0;
        uint32_t iatRVA_val = 0;
        uint32_t descriptorRVA = 0;
        uint32_t iltDataRVA = 0;
    };
    std::vector<DLLBuild> dllBuilds;

    importDescCount = (uint32_t)dllFuncMap.size();
    uint32_t descRVA = rdataRVA;

    for (auto& [dll, funcs] : dllFuncMap) {
        DLLBuild db;
        db.dllName = dll;
        db.funcs = funcs;
        dllBuilds.push_back(db);
    }

    // Build .rdata into newRdata with correct layout
    std::vector<uint8_t> newRdata;

    // Compute sizes upfront
    uint32_t descSize = (uint32_t)(dllBuilds.size() + 1) * 20;
    uint32_t namesTotal = 0;
    for (auto& db : dllBuilds) namesTotal += (uint32_t)db.dllName.size() + 1;
    uint32_t namesPadded = (namesTotal + 3) & ~3;
    uint32_t hintsTotal = 0;
    std::vector<uint32_t> funcCounts;
    for (auto& db : dllBuilds) {
        funcCounts.push_back((uint32_t)db.funcs.size());
        for (auto& fn : db.funcs) hintsTotal += (uint32_t)(2 + fn.size() + 1);
    }
    uint32_t hintsPadded = (hintsTotal + 7) & ~7;

    // Compute name RVAs
    std::vector<uint32_t> nameRVAs;
    uint32_t nameOff = descSize;
    for (auto& db : dllBuilds) {
        nameRVAs.push_back(rdataRVA + nameOff);
        nameOff += (uint32_t)db.dllName.size() + 1;
    }

    // Compute hint/name RVAs
    std::vector<std::vector<uint32_t>> hintRVAs;
    uint32_t hintOff = descSize + namesPadded;
    for (size_t d = 0; d < dllBuilds.size(); d++) {
        std::vector<uint32_t> hints;
        for (auto& fn : dllBuilds[d].funcs) {
            hints.push_back(rdataRVA + hintOff);
            hintOff += (uint32_t)(2 + fn.size() + 1);
        }
        hintRVAs.push_back(hints);
    }

    // Write descriptors and .data (ILT/IAT)
    for (size_t d = 0; d < dllBuilds.size(); d++) {
        auto& db = dllBuilds[d];
        uint32_t iltDataRVA = dataRVA + (uint32_t)data.size();

        // ILT in .data (8-byte entries for PE32+)
        for (size_t i = 0; i < db.funcs.size(); i++)
            writeDQ(data, hintRVAs[d][i]);
        writeDQ(data, 0);

        // IAT in .data (8-byte entries for PE32+)
        uint32_t iatAddrRVA = dataRVA + (uint32_t)data.size();
        for (size_t i = 0; i < db.funcs.size(); i++)
            writeDQ(data, hintRVAs[d][i]);
        writeDQ(data, 0);

        uint32_t iatStartRVA = iatAddrRVA;

        writeDW(newRdata, iltDataRVA);
        writeDW(newRdata, 0);
        writeDW(newRdata, 0);
        writeDW(newRdata, nameRVAs[d]);
        writeDW(newRdata, iatAddrRVA);

        for (size_t i = 0; i < db.funcs.size(); i++)
            externFuncMap[db.funcs[i]] = {db.dllName, iatStartRVA + (uint32_t)(i * 8)};
    }

    // Zero terminator descriptor
    for (int i = 0; i < 20; i++) newRdata.push_back(0);

    // DLL names
    for (auto& db : dllBuilds) {
        for (char c : db.dllName) newRdata.push_back((uint8_t)c);
        newRdata.push_back(0);
    }
    while (newRdata.size() % 4 != 0) newRdata.push_back(0);

    // Hint/Name entries
    for (auto& db : dllBuilds) {
        for (auto& fn : db.funcs) {
            newRdata.push_back(0); newRdata.push_back(0);
            for (char c : fn) newRdata.push_back((uint8_t)c);
            newRdata.push_back(0);
        }
    }
    while (newRdata.size() % 8 != 0) newRdata.push_back(0);

    // Ensure newline string is in the pool for print()
    bool hasCRLF = false;
    for (auto& s : stringPool) if (s == "\r\n") { hasCRLF = true; break; }
    if (!hasCRLF) stringPool.push_back("\r\n");

    // Ensure pause message is in the pool for pause()
    std::string pauseMsg = "Press any key to continue . . .\r\n";
    bool hasPause = false;
    for (auto& s : stringPool) if (s == pauseMsg) { hasPause = true; break; }
    if (!hasPause) stringPool.push_back(pauseMsg);

    // String pool — pool-relative offsets
    uint32_t stringPoolStart = (uint32_t)newRdata.size();
    stringRVA = rdataRVA + stringPoolStart;
    for (auto& s : stringPool) {
        stringOffsets.push_back((uint32_t)newRdata.size() - stringPoolStart);
        for (char c : s) newRdata.push_back((uint8_t)c);
        newRdata.push_back(0);
    }
    while (newRdata.size() % 16 != 0) newRdata.push_back(0);

    // Class name string for window creation (GUI only)
    if (prog.appType == AppType::GUI) {
        classNameRVA = rdataRVA + (uint32_t)newRdata.size();
        const char* className = "ZenithWnd";
        for (const char* p = className; *p; p++) newRdata.push_back((uint8_t)*p);
        newRdata.push_back(0);
        while (newRdata.size() % 16 != 0) newRdata.push_back(0);
    }

    rdata = std::move(newRdata);

    // === EMBEDDED DLL: rdata entries (DLL path strings, function name strings) ===
    if (!embeddedDLLs.empty()) {
        for (auto& emb : embeddedDLLs) {
            // DLL path string: "\\libs_math.dll"
            emb.dllPathStrRVA = rdataRVA + (uint32_t)rdata.size();
            rdata.push_back('\\');
            for (char c : emb.dllName) rdata.push_back((uint8_t)c);
            rdata.push_back(0);
            while (rdata.size() % 4 != 0) rdata.push_back(0);

            // Function name strings
            emb.funcNameRVAs.clear();
            for (auto& fn : emb.funcs) {
                uint32_t nameRVA = rdataRVA + (uint32_t)rdata.size();
                emb.funcNameRVAs.push_back(nameRVA);
                for (char c : fn.name) rdata.push_back((uint8_t)c);
                rdata.push_back(0);
            }
            while (rdata.size() % 4 != 0) rdata.push_back(0);
        }
    }

    importDataSize = (uint32_t)data.size();

    // Heap — bump allocator state + 64KB heap area
    heapOffsetRVA = dataRVA + (uint32_t)data.size();
    for (int k = 0; k < 8; k++) data.push_back(0);
    heapAreaRVA = dataRVA + (uint32_t)data.size();
    for (int k = 0; k < 64 * 1024; k++) data.push_back(0);

    // Win32 globals for 2D graphics built-ins (GUI only)
    if (prog.appType == AppType::GUI) {
        win32GlobalsRVA = dataRVA + (uint32_t)data.size();
        for (int k = 0; k < 56; k++) data.push_back(0);
        // Layout: initFlag(8), hwnd(8), hdc(8), memDC(8), framebuf(8), width(4), height(4), pad(8)
    }

    // === EMBEDDED DLL: .data entries ===
    if (!embeddedDLLs.empty()) {
        // Loader scratch buffers
        embeddedFullPathRVA = dataRVA + (uint32_t)data.size();
        for (int k = 0; k < 520; k++) data.push_back(0); // fullPath temp buffer
        embeddedHFileRVA = dataRVA + (uint32_t)data.size();
        for (int k = 0; k < 8; k++) data.push_back(0);
        embeddedHModuleRVA = dataRVA + (uint32_t)data.size();
        for (int k = 0; k < 8; k++) data.push_back(0);
        embeddedWrittenRVA = dataRVA + (uint32_t)data.size();
        for (int k = 0; k < 8; k++) data.push_back(0);

        // For each embedded DLL: store raw bytes + pointer slots
        for (auto& emb : embeddedDLLs) {
            emb.blobRVA = dataRVA + (uint32_t)data.size();
            data.insert(data.end(), emb.bytes.begin(), emb.bytes.end());
            while (data.size() % 8 != 0) data.push_back(0);

            // Pointer slots for each function (8 bytes each, init to 0)
            emb.funcPtrRVAs.clear();
            for (auto& fn : emb.funcs) {
                uint32_t slotRVA = dataRVA + (uint32_t)data.size();
                emb.funcPtrRVAs.push_back(slotRVA);
                for (int k = 0; k < 8; k++) data.push_back(0);
            }
        }

        // Map embedded function names to their pointer slot RVAs in externFuncMap
        for (auto& emb : embeddedDLLs) {
            for (size_t f = 0; f < emb.funcs.size(); f++) {
                externFuncMap[emb.funcs[f].name] = {"EMBEDDED_" + emb.dllName, emb.funcPtrRVAs[f]};
            }
        }
    }
}

void Codegen::buildPE(const std::string& outputPath) {
    // Patch ExitProcess call (EXE only)
    if (!libOutput) {
        auto epIt = externFuncMap.find("ExitProcess");
        if (epIt != externFuncMap.end()) {
            uint32_t targetIATRVA = epIt->second.second;
            int64_t disp = (int64_t)targetIATRVA - (int64_t)(textRVA + entryExitProcessFixup + 4);
            code[entryExitProcessFixup]     = (uint8_t)(disp & 0xFF);
            code[entryExitProcessFixup + 1] = (uint8_t)((disp >> 8) & 0xFF);
            code[entryExitProcessFixup + 2] = (uint8_t)((disp >> 16) & 0xFF);
            code[entryExitProcessFixup + 3] = (uint8_t)((disp >> 24) & 0xFF);
        }
    }

    // Patch import call fixups
    for (auto& icf : importCallFixups) {
        auto it = externFuncMap.find(icf.funcName);
        if (it != externFuncMap.end()) {
            uint32_t targetIATRVA = it->second.second;
            int64_t disp = (int64_t)targetIATRVA - (int64_t)(textRVA + icf.codePos + 4);
            code[icf.codePos]     = (uint8_t)(disp & 0xFF);
            code[icf.codePos + 1] = (uint8_t)((disp >> 8) & 0xFF);
            code[icf.codePos + 2] = (uint8_t)((disp >> 16) & 0xFF);
            code[icf.codePos + 3] = (uint8_t)((disp >> 24) & 0xFF);
        }
    }

    // Patch string fixups
    for (auto& sf : strFixups) {
        uint32_t strTargetRVA = stringRVA + stringOffsets[sf.stringIndex];
        int64_t disp = (int64_t)strTargetRVA - (int64_t)(textRVA + sf.codePos + 4);
        code[sf.codePos]     = (uint8_t)(disp & 0xFF);
        code[sf.codePos + 1] = (uint8_t)((disp >> 8) & 0xFF);
        code[sf.codePos + 2] = (uint8_t)((disp >> 16) & 0xFF);
        code[sf.codePos + 3] = (uint8_t)((disp >> 24) & 0xFF);
    }

    // Patch heap fixups
    for (auto& hf : heapFixups) {
        int64_t disp = (int64_t)hf.targetRVA - (int64_t)(textRVA + hf.codePos + 4);
        code[hf.codePos]     = (uint8_t)(disp & 0xFF);
        code[hf.codePos + 1] = (uint8_t)((disp >> 8) & 0xFF);
        code[hf.codePos + 2] = (uint8_t)((disp >> 16) & 0xFF);
        code[hf.codePos + 3] = (uint8_t)((disp >> 24) & 0xFF);
    }

    uint32_t textSize = (uint32_t)code.size();
    uint32_t rdataSize = (uint32_t)rdata.size();
    uint32_t dataSize = (uint32_t)data.size();

    // Validate no section overlap
    uint32_t actualTextEnd = textRVA + ((textSize + 0xFFF) & ~0xFFF);
    if (rdataRVA < actualTextEnd) {
        std::cerr << "ERROR: .text (end=0x" << std::hex << actualTextEnd
                  << ") overlaps .rdata (0x" << rdataRVA << std::dec << ")\n";
        throw std::runtime_error("Section overlap: .text overlaps .rdata");
    }
    uint32_t actualRdataEnd = rdataRVA + ((rdataSize + 0xFFF) & ~0xFFF);
    if (dataRVA < actualRdataEnd) {
        std::cerr << "ERROR: .rdata (end=0x" << std::hex << actualRdataEnd
                  << ") overlaps .data (0x" << dataRVA << std::dec << ")\n";
        throw std::runtime_error("Section overlap: .rdata overlaps .data");
    }

    uint32_t textRawSize = (textSize + 0x1FF) & ~0x1FF;
    uint32_t rdataRawSize = (rdataSize + 0x1FF) & ~0x1FF;
    uint32_t dataRawSize = (dataSize + 0x1FF) & ~0x1FF;

    uint32_t textRawOfs = 0x200;
    uint32_t rdataRawOfs = textRawOfs + textRawSize;
    uint32_t dataRawOfs = rdataRawOfs + rdataRawSize;

    DOSHeader dos;
    IMAGE_FILE_HEADER coff;
    IMAGE_OPTIONAL_HEADER64 opt = {};

    coff.Machine = 0x8664;
    coff.NumberOfSections = 3;
    coff.SizeOfOptionalHeader = sizeof(opt);

    opt.Subsystem = (prog.appType == AppType::GUI) ? 2 : 3;
    opt.DllCharacteristics = 0x0160; // NX_COMPAT | DYNAMIC_BASE | HIGH_ENTROPY_VA
    if (libOutput) {
        coff.Characteristics = 0x2022; // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE | DLL
        opt.Subsystem = 2; // GUI subsystem for DLL (works with DllMain)
    } else {
        coff.Characteristics = 0x0022; // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE
    }

    opt.SizeOfCode = textRawSize;
    opt.SizeOfInitializedData = rdataRawSize + dataRawSize;
    opt.AddressOfEntryPoint = textRVA + (uint32_t)entryPointCodeOffset;

    // Calculate sections end for SizeOfImage
    uint32_t textEnd = textRVA + ((textSize + 0xFFF) & ~0xFFF);
    uint32_t rdataEnd = rdataRVA + ((rdataSize + 0xFFF) & ~0xFFF);
    uint32_t dataEnd = dataRVA + ((dataSize + 0xFFF) & ~0xFFF);
    uint32_t lastSectionEnd = dataEnd;
    if (rdataEnd > lastSectionEnd) lastSectionEnd = rdataEnd;
    if (textEnd > lastSectionEnd) lastSectionEnd = textEnd;
    opt.SizeOfImage = (lastSectionEnd + 0xFFF) & ~0xFFF;
    opt.SizeOfHeaders = 0x200;

    // Import directory entry — point to descriptors in .rdata
    uint32_t importDescSize = (importDescCount + 1) * 20;
    opt.DataDirectory[1].VirtualAddress = rdataRVA;
    opt.DataDirectory[1].Size = importDescSize;

    // Export directory entry for DLL mode
    if (libOutput && exportDirSize > 0) {
        opt.DataDirectory[0].VirtualAddress = exportDirRVA;
        opt.DataDirectory[0].Size = exportDirSize;
    }

    IMAGE_SECTION_HEADER textSec{}, rdataSec{}, dataSec{};

    memcpy(textSec.Name, ".text", 6);
    textSec.VirtualSize = textSize;
    textSec.VirtualAddress = textRVA;
    textSec.SizeOfRawData = textRawSize;
    textSec.PointerToRawData = textRawOfs;
    textSec.Characteristics = 0x60000020;

    memcpy(rdataSec.Name, ".rdata", 7);
    rdataSec.VirtualSize = rdataSize;
    rdataSec.VirtualAddress = rdataRVA;
    rdataSec.SizeOfRawData = rdataRawSize;
    rdataSec.PointerToRawData = rdataRawOfs;
    rdataSec.Characteristics = 0x40000040;

    memcpy(dataSec.Name, ".data", 6);
    dataSec.VirtualSize = dataSize;
    dataSec.VirtualAddress = dataRVA;
    dataSec.SizeOfRawData = dataRawSize;
    dataSec.PointerToRawData = dataRawOfs;
    dataSec.Characteristics = 0xC0000040;

    std::ofstream f{std::filesystem::path(outputPath), std::ios::binary};
    f.write((const char*)&dos, sizeof(dos));

    const char* stub = "This program cannot be run in DOS mode.\r\n";
    f.write(stub, (int)strlen(stub) + 1);
    size_t stubEnd = 0x40 + strlen(stub) + 1;
    while (stubEnd < dos.e_lfanew) { f.put(0); stubEnd++; }

    uint32_t peSig = 0x00004550;
    f.write((const char*)&peSig, 4);
    f.write((const char*)&coff, sizeof(coff));
    f.write((const char*)&opt, sizeof(opt));
    f.write((const char*)&textSec, sizeof(textSec));
    f.write((const char*)&rdataSec, sizeof(rdataSec));
    f.write((const char*)&dataSec, sizeof(dataSec));

    size_t hEnd = dos.e_lfanew + 4 + sizeof(coff) + sizeof(opt) + sizeof(textSec) + sizeof(rdataSec) + sizeof(dataSec);
    while (hEnd < textRawOfs) { f.put(0); hEnd++; }

    f.write((const char*)code.data(), code.size());
    for (uint32_t i = code.size(); i < textRawSize; i++) f.put(0);

    f.write((const char*)rdata.data(), rdata.size());
    for (uint32_t i = rdata.size(); i < rdataRawSize; i++) f.put(0);

    f.write((const char*)data.data(), data.size());
    for (uint32_t i = data.size(); i < dataRawSize; i++) f.put(0);

    f.close();
    std::cout << "Compiled: " << outputPath << " (" << textSize << " B code)\n";
}

void Codegen::emitWndProc() {
    wndProcOffset = code.size();
    // WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    // rcx = hwnd, edx = msg, r8 = wParam, r9 = lParam
    int startPos = (int)code.size();

    // cmp edx, 2 (WM_DESTROY)
    emit8(0x83); emit8(0xFA); emit8(0x02);
    emit8(0x75);  // jne .default
    int jnePos = (int)code.size();
    emit8(0x00);  // placeholder

    // WM_DESTROY: PostQuitMessage(0)
    emit8(0x33); emit8(0xC9);  // xor ecx, ecx
    emit8(0xFF); emit8(0x15);
    importCallFixups.push_back({code.size(), "PostQuitMessage", "user32.dll"});
    emit32(0);
    emit8(0x33); emit8(0xC0);  // xor eax, eax
    emit8(0xC3);  // ret

    // .default: tail call to DefWindowProcA
    int defaultPos = (int)code.size();
    code[jnePos] = (uint8_t)(defaultPos - jnePos - 1);

    emit8(0xFF); emit8(0x25);
    importCallFixups.push_back({code.size(), "DefWindowProcA", "user32.dll"});
    emit32(0);
}

uint32_t Codegen::estimateRdataSize() {
    uint32_t total = 0;

    // Import descriptors
    uint32_t dllCount = 0;
    std::unordered_map<std::string, std::vector<std::string>> dllFuncMap;
    // Always added DLLs/funcs (same as in buildImportData)
    dllFuncMap["kernel32.dll"] = {"ExitProcess", "GetStdHandle", "WriteFile", "GetModuleHandleA", "Sleep"};
    if (prog.appType == AppType::GUI) {
        dllFuncMap["user32.dll"] = {"CreateWindowExA", "DefWindowProcA", "RegisterClassExA",
            "DestroyWindow", "GetDC", "ReleaseDC", "PeekMessageA", "TranslateMessage",
            "DispatchMessageA", "GetAsyncKeyState", "PostQuitMessage", "BeginPaint",
            "EndPaint", "GetMessageA"};
        dllFuncMap["gdi32.dll"] = {"CreateDIBSection", "BitBlt", "SelectObject",
            "DeleteObject", "DeleteDC", "CreateCompatibleDC"};
    }

    for (auto& func : prog.functions) {
        if (func->isExtern) {
            auto mapFuncToDll = [](const std::string& fn) -> std::string {
                if (fn == "ExitProcess" || fn == "GetStdHandle" || fn == "WriteFile" ||
                    fn == "ReadFile" || fn == "HeapAlloc" || fn == "HeapFree" ||
                    fn == "GetProcessHeap" || fn == "GetModuleHandleA" ||
                    fn == "Sleep") return "kernel32.dll";
                if (fn.find("CreateWindowEx") == 0 || fn.find("DefWindowProc") == 0 ||
                    fn.find("RegisterClass") == 0 || fn.find("DestroyWindow") == 0 ||
                    fn.find("GetDC") == 0 || fn.find("PeekMessageA") == 0 ||
                    fn.find("TranslateMessage") == 0 || fn.find("DispatchMessageA") == 0 ||
                    fn.find("GetAsyncKeyState") == 0 || fn.find("PostQuitMessage") == 0) return "user32.dll";
                if (fn.find("CreateDIBSection") == 0 || fn.find("BitBlt") == 0 ||
                    fn.find("SelectObject") == 0 || fn.find("DeleteObject") == 0 ||
                    fn.find("CreateCompatibleDC") == 0) return "gdi32.dll";
                return "kernel32.dll";
            };
            std::string dll = func->dllName.empty() ? mapFuncToDll(func->name) : func->dllName;
            dllFuncMap[dll].push_back(func->name);
        }
    }

    // Descriptor size
    dllCount = (uint32_t)dllFuncMap.size();
    total += (dllCount + 1) * 20;

    // DLL names
    for (auto& [dll, _] : dllFuncMap)
        total += (uint32_t)dll.size() + 1;
    total = (total + 3) & ~3;

    // Hint/name entries
    for (auto& [_, funcs] : dllFuncMap) {
        for (auto& fn : funcs)
            total += (uint32_t)(2 + fn.size() + 1);
    }
    total = (total + 7) & ~7;

    // String pool
    for (auto& s : stringPool)
        total += (uint32_t)s.size() + 1;
    total = (total + 15) & ~15;

    // Class name
    if (prog.appType == AppType::GUI) {
        total += 10; // "ZenithWnd\0"
        total = (total + 15) & ~15;
    }

    // Embedded DLL strings (approximate: DLL paths + function names)
    for (auto& imp : prog.imports) {
        if (imp.dllName == "libs.dll" && !imp.module.empty()) {
            total += 64; // approximate per embedded DLL (path + func names)
        }
    }

    return total;
}

uint32_t Codegen::estimateDataSize() {
    // ILT + IAT for each DLL, heap (64KB), globals, heap offset
    uint32_t total = 0x400; // IAT/ILT entries
    total += 8;  // heap offset
    total += 64 * 1024; // heap
    if (prog.appType == AppType::GUI) {
        total += 64; // win32 globals
    }
    return total + 0x2000; // safety margin
}

void Codegen::computeSectionRVAs() {
    rdataRVA = textRVA + 0x1000;
    dataRVA = rdataRVA + 0x1000;
}

void Codegen::fixupSectionRVAs() {
    uint32_t textSize = (uint32_t)code.size();
    uint32_t rdataSize = (uint32_t)rdata.size();
    uint32_t dataSize = (uint32_t)data.size();

    uint32_t alignedText = (textSize + 0xFFF) & ~0xFFF;
    uint32_t alignedRdata = (rdataSize + 0xFFF) & ~0xFFF;

    uint32_t newRdataRVA = textRVA + alignedText;
    uint32_t newDataRVA = newRdataRVA + alignedRdata;

    int32_t dRdata = (int32_t)(newRdataRVA - rdataRVA);
    int32_t dData = (int32_t)(newDataRVA - dataRVA);

    if (dRdata == 0 && dData == 0) return;

    // Helper lambdas for reading/writing DWORDS/QWORDS in byte vectors
    auto rdDW = [](const std::vector<uint8_t>& buf, size_t off) -> uint32_t {
        return (uint32_t)buf[off] | ((uint32_t)buf[off+1] << 8) | ((uint32_t)buf[off+2] << 16) | ((uint32_t)buf[off+3] << 24);
    };
    auto wrDW = [](std::vector<uint8_t>& buf, size_t off, uint32_t val) {
        buf[off] = val & 0xFF; buf[off+1] = (val >> 8) & 0xFF;
        buf[off+2] = (val >> 16) & 0xFF; buf[off+3] = (val >> 24) & 0xFF;
    };
    auto rdDQ = [](const std::vector<uint8_t>& buf, size_t off) -> uint64_t {
        return (uint64_t)buf[off] | ((uint64_t)buf[off+1] << 8) | ((uint64_t)buf[off+2] << 16) |
               ((uint64_t)buf[off+3] << 24) | ((uint64_t)buf[off+4] << 32) |
               ((uint64_t)buf[off+5] << 40) | ((uint64_t)buf[off+6] << 48) | ((uint64_t)buf[off+7] << 56);
    };
    auto wrDQ = [](std::vector<uint8_t>& buf, size_t off, uint64_t val) {
        buf[off] = val & 0xFF; buf[off+1] = (val >> 8) & 0xFF;
        buf[off+2] = (val >> 16) & 0xFF; buf[off+3] = (val >> 24) & 0xFF;
        buf[off+4] = (val >> 32) & 0xFF; buf[off+5] = (val >> 40) & 0xFF;
        buf[off+6] = (val >> 48) & 0xFF; buf[off+7] = (val >> 56) & 0xFF;
    };

    uint32_t oldRdataRVA = newRdataRVA - dRdata;
    uint32_t oldDataRVA = newDataRVA - dData;

    // 1. Fix import descriptors in .rdata
    // Each descriptor: [ILT RVA(4)][timestamp(4)][fwd(4)][Name RVA(4)][IAT RVA(4)]
    for (uint32_t i = 0; i < importDescCount; i++) {
        size_t base = i * 20;
        uint32_t ilt = rdDW(rdata, base);
        wrDW(rdata, base, ilt + dData);
        uint32_t name = rdDW(rdata, base + 12);
        wrDW(rdata, base + 12, name + dRdata);
        uint32_t iat = rdDW(rdata, base + 16);
        wrDW(rdata, base + 16, iat + dData);
    }

    // 2. Fix ILT/IAT entries in .data (8-byte RVAs to hint/name entries in .rdata)
    for (uint32_t i = 0; i < importDataSize; i += 8) {
        uint64_t val = rdDQ(data, i);
        if (val != 0) {
            wrDQ(data, i, val + dRdata);
        }
    }

    // 3. Fix externFuncMap IAT entries
    for (auto& [name, pair] : externFuncMap) {
        if (pair.second != 0) {
            pair.second += dData;
        }
    }

    // 4. Fix heap fixups target RVAs
    for (auto& hf : heapFixups) {
        if (hf.targetRVA >= oldRdataRVA && hf.targetRVA < oldRdataRVA + rdataSize) {
            hf.targetRVA += dRdata;
        } else if (hf.targetRVA >= oldDataRVA && hf.targetRVA < oldDataRVA + dataSize) {
            hf.targetRVA += dData;
        }
    }

    // 5. Fix scalar RVAs
    stringRVA += dRdata;
    if (prog.appType == AppType::GUI) {
        classNameRVA += dRdata;
    }
    heapOffsetRVA += dData;
    heapAreaRVA += dData;
    if (prog.appType == AppType::GUI) {
        win32GlobalsRVA += dData;
    }

    // 5b. Fix embedded DLL RVAs
    embeddedFullPathRVA += dData;
    embeddedHFileRVA += dData;
    embeddedHModuleRVA += dData;
    embeddedWrittenRVA += dData;
    for (auto& emb : embeddedDLLs) {
        emb.blobRVA += dData;
        emb.dllPathStrRVA += dRdata;
        for (auto& fRVA : emb.funcNameRVAs) fRVA += dRdata;
        for (auto& pRVA : emb.funcPtrRVAs) pRVA += dData;
    }

    // 6. Update section RVAs
    rdataRVA = newRdataRVA;
    dataRVA = newDataRVA;
}

void Codegen::emitDllEntryPoint() {
    entryPointCodeOffset = code.size();
    // DllMain(hinstDLL, fdwReason, lpvReserved)
    // return TRUE (1) — x64 uses caller-managed stack, plain ret
    emit8(0x33); emit8(0xC0);  // xor eax, eax
    emit8(0xFF); emit8(0xC0);  // inc eax
    emit8(0xC3);               // ret
}

void Codegen::buildExportDir() {
    if (!libOutput || exportEntries.empty()) return;

    uint32_t N = (uint32_t)exportEntries.size();

    // DLL name string
    std::string dllBaseName = "zenithlib";
    uint32_t dllNameRVA = rdataRVA + (uint32_t)rdata.size();
    for (char c : dllBaseName) rdata.push_back((uint8_t)c);
    rdata.push_back(0);
    while (rdata.size() % 4 != 0) rdata.push_back(0);

    // Function name strings
    std::vector<uint32_t> nameStrRVAs;
    for (auto& ee : exportEntries) {
        nameStrRVAs.push_back(rdataRVA + (uint32_t)rdata.size());
        for (char c : ee.name) rdata.push_back((uint8_t)c);
        rdata.push_back(0);
    }
    while (rdata.size() % 4 != 0) rdata.push_back(0);

    exportDirRVA = rdataRVA + (uint32_t)rdata.size();

    uint32_t addrTableRVA = exportDirRVA + 40;
    uint32_t namePtrTableRVA = addrTableRVA + N * 4;
    uint32_t ordinalTableRVA = namePtrTableRVA + N * 4;

    auto writeAt = [&](uint32_t val) {
        rdata.push_back(val & 0xFF);
        rdata.push_back((val >> 8) & 0xFF);
        rdata.push_back((val >> 16) & 0xFF);
        rdata.push_back((val >> 24) & 0xFF);
    };

    writeAt(0);             // Characteristics
    writeAt(0);             // TimeDateStamp
    writeAt(0);             // MajorVersion + MinorVersion
    writeAt(dllNameRVA);    // Name
    writeAt(1);             // Base (ordinal base)
    writeAt(N);             // NumberOfFunctions
    writeAt(N);             // NumberOfNames
    writeAt(addrTableRVA);  // AddressOfFunctions
    writeAt(namePtrTableRVA); // AddressOfNames
    writeAt(ordinalTableRVA); // AddressOfNameOrdinals

    for (auto& ee : exportEntries) {
        writeAt(ee.funcRVA);
    }

    for (auto& nrva : nameStrRVAs) {
        writeAt(nrva);
    }

    for (uint32_t i = 0; i < N; i++) {
        rdata.push_back((uint8_t)(i & 0xFF));
        rdata.push_back((uint8_t)((i >> 8) & 0xFF));
    }

    exportDirSize = (uint32_t)(rdata.size() - (exportDirRVA - rdataRVA));
}

void Codegen::emitEmbeddedLoader() {
    if (embeddedDLLs.empty()) return;

    for (auto& emb : embeddedDLLs) {
        // 1. GetTempPathA(520, fullPath)
        emit8(0xB9); emit32(520);
        emit8(0x48); emit8(0x8D); emit8(0x15);
        {
            int64_t d = (int64_t)embeddedFullPathRVA - (int64_t)(textRVA + code.size() + 4);
            emit32((uint32_t)d);
        }
        emit8(0xFF); emit8(0x15);
        importCallFixups.push_back({code.size(), "GetTempPathA", "kernel32.dll"});
        emit32(0);

        // 2. lstrcatA(fullPath, "\\dllName")
        emit8(0x48); emit8(0x8D); emit8(0x0D);
        {
            int64_t d = (int64_t)embeddedFullPathRVA - (int64_t)(textRVA + code.size() + 4);
            emit32((uint32_t)d);
        }
        emit8(0x48); emit8(0x8D); emit8(0x15);
        {
            int64_t d = (int64_t)emb.dllPathStrRVA - (int64_t)(textRVA + code.size() + 4);
            emit32((uint32_t)d);
        }
        emit8(0xFF); emit8(0x15);
        importCallFixups.push_back({code.size(), "lstrcatA", "kernel32.dll"});
        emit32(0);

        // 3. CreateFileA(fullPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL)
        emit8(0x48); emit8(0x8D); emit8(0x0D);
        {
            int64_t d = (int64_t)embeddedFullPathRVA - (int64_t)(textRVA + code.size() + 4);
            emit32((uint32_t)d);
        }
        emit8(0xBA); emit32(0x40000000);        // mov edx, GENERIC_WRITE
        emit8(0x45); emit8(0x31); emit8(0xC0);  // xor r8d, r8d
        emit8(0x45); emit8(0x31); emit8(0xC9);  // xor r9d, r9d
        // [rsp+0x20] = CREATE_ALWAYS (2)
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20);
        emit32(2);
        // [rsp+0x28] = 0 (flags)
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x28);
        emit32(0);
        // [rsp+0x30] = 0 (template)
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x30);
        emit32(0);
        emit8(0xFF); emit8(0x15);
        importCallFixups.push_back({code.size(), "CreateFileA", "kernel32.dll"});
        emit32(0);

        // Save hFile: mov [rip+hFile], rax
        emit8(0x48); emit8(0x89); emit8(0x05);
        {
            int64_t d = (int64_t)embeddedHFileRVA - (int64_t)(textRVA + code.size() + 4);
            emit32((uint32_t)d);
        }

        // 4. WriteFile(hFile, dllData, dllSize, &written, NULL)
        emit8(0x48); emit8(0x89); emit8(0xC1);  // mov rcx, rax (hFile)
        emit8(0x48); emit8(0x8D); emit8(0x15);  // lea rdx, [rip+blob]
        {
            int64_t d = (int64_t)emb.blobRVA - (int64_t)(textRVA + code.size() + 4);
            emit32((uint32_t)d);
        }
        emit8(0x41); emit8(0xB8);                // mov r8d, imm32 (blobSize)
        emit32(emb.blobSize);
        emit8(0x4C); emit8(0x8D); emit8(0x0D);  // lea r9, [rip+written]
        {
            int64_t d = (int64_t)embeddedWrittenRVA - (int64_t)(textRVA + code.size() + 4);
            emit32((uint32_t)d);
        }
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20);
        emit32(0);  // [rsp+0x20] = NULL
        emit8(0xFF); emit8(0x15);
        importCallFixups.push_back({code.size(), "WriteFile", "kernel32.dll"});
        emit32(0);

        // 5. CloseHandle(hFile)
        emit8(0x48); emit8(0x8B); emit8(0x0D);  // mov rcx, [rip+hFile]
        {
            int64_t d = (int64_t)embeddedHFileRVA - (int64_t)(textRVA + code.size() + 4);
            emit32((uint32_t)d);
        }
        emit8(0xFF); emit8(0x15);
        importCallFixups.push_back({code.size(), "CloseHandle", "kernel32.dll"});
        emit32(0);

        // 6. LoadLibraryA(fullPath)
        emit8(0x48); emit8(0x8D); emit8(0x0D);  // lea rcx, [rip+fullPath]
        {
            int64_t d = (int64_t)embeddedFullPathRVA - (int64_t)(textRVA + code.size() + 4);
            emit32((uint32_t)d);
        }
        emit8(0xFF); emit8(0x15);
        importCallFixups.push_back({code.size(), "LoadLibraryA", "kernel32.dll"});
        emit32(0);
        // Save hModule
        emit8(0x48); emit8(0x89); emit8(0x05);  // mov [rip+hModule], rax
        {
            int64_t d = (int64_t)embeddedHModuleRVA - (int64_t)(textRVA + code.size() + 4);
            emit32((uint32_t)d);
        }

        // 7. GetProcAddress(hModule, "funcName") for each function
        for (size_t f = 0; f < emb.funcs.size(); f++) {
            // mov rcx, [rip+hModule]
            emit8(0x48); emit8(0x8B); emit8(0x0D);
            {
                int64_t d = (int64_t)embeddedHModuleRVA - (int64_t)(textRVA + code.size() + 4);
                emit32((uint32_t)d);
            }
            // lea rdx, [rip+funcName]
            emit8(0x48); emit8(0x8D); emit8(0x15);
            {
                int64_t d = (int64_t)emb.funcNameRVAs[f] - (int64_t)(textRVA + code.size() + 4);
                emit32((uint32_t)d);
            }
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "GetProcAddress", "kernel32.dll"});
            emit32(0);
            // Store result: mov [rip+funcSlot], rax
            emit8(0x48); emit8(0x89); emit8(0x05);
            {
                int64_t d = (int64_t)emb.funcPtrRVAs[f] - (int64_t)(textRVA + code.size() + 4);
                emit32((uint32_t)d);
            }
        }

        // 8. DeleteFileA(fullPath)
        emit8(0x48); emit8(0x8D); emit8(0x0D);  // lea rcx, [rip+fullPath]
        {
            int64_t d = (int64_t)embeddedFullPathRVA - (int64_t)(textRVA + code.size() + 4);
            emit32((uint32_t)d);
        }
        emit8(0xFF); emit8(0x15);
        importCallFixups.push_back({code.size(), "DeleteFileA", "kernel32.dll"});
        emit32(0);
    }
}

void Codegen::generate(const std::string& outputPath) {
    outputDir = std::filesystem::path(outputPath).parent_path();
    computeStructLayouts();
    collectStrings();
    computeSectionRVAs();
    buildImportData();

    if (prog.appType == AppType::GUI) {
        emitWndProc();
    }

    for (auto& func : prog.functions) {
        if (!func->isExtern) {
            emitFunction(func.get());
        }
    }

    if (libOutput) {
        emitDllEntryPoint();
        for (auto& func : prog.functions) {
            if (!func->isExtern) {
                ExportEntry ee;
                ee.name = func->name;
                ee.funcRVA = textRVA + (uint32_t)funcOffsets[func->name];
                exportEntries.push_back(ee);
            }
        }
    } else {
        emitEntryPoint();
    }

    fixupSectionRVAs();
    resolveFixups();
    resolveJmpFixups();

    // Build export directory AFTER fixupSectionRVAs so RVAs are final
    if (libOutput) {
        buildExportDir();

        // Re-check section overlap after export dir may have grown .rdata
        uint32_t newRdataEnd = rdataRVA + (((uint32_t)rdata.size() + 0xFFF) & ~0xFFF);
        if (dataRVA < newRdataEnd) {
            dataRVA = newRdataEnd;
        }
    }

    buildPE(outputPath);
}
