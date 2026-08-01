#include "codegen.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <filesystem>

// Forward-declare Windows API to avoid windows.h conflicts with custom PE structs
extern "C" {
    __declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned int cp, unsigned long flags, const char* str, int len, wchar_t* wstr, int wlen);
    __declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned int cp, unsigned long flags, const wchar_t* wstr, int wlen, char* str, int len, const char* def, int* used);
    __declspec(dllimport) int __stdcall GetModuleFileNameW(void* hMod, wchar_t* path, unsigned long size);
    __declspec(dllimport) void* __stdcall GetModuleHandleW(const wchar_t* name);
    __declspec(dllimport) unsigned int __stdcall GetSystemDirectoryW(wchar_t* path, unsigned int size);
}

static std::filesystem::path safeNarrowToPath(const std::string& s) {
    int wlen = MultiByteToWideChar(0 /*CP_ACP*/, 0, s.c_str(), -1, nullptr, 0);
    if (wlen > 0) {
        std::wstring ws(wlen - 1, L'\0');
        MultiByteToWideChar(0, 0, s.c_str(), -1, &ws[0], wlen);
        return std::filesystem::path(ws);
    }
    return std::filesystem::path(s);
}

void Codegen::setCompilerDir(const std::string& dir) {
    compilerDir = safeNarrowToPath(dir);
}

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
    else { emit8(0x48); emit8(0x2B); emit8(0xC0 + dst + src * 8); }
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
    else { emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xC0 + dst + src * 8); }
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
    jmpFixups.push_back({code.size(), label});
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
    jmpFixups.push_back({code.size(), label});
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

void Codegen::spillRegs() {
    if (regsUsed == 0) return;
    for (int i = 0; i < 4; i++) {
        if (regsUsed & (1 << i)) {
            emitStoreRegToBP(i, -(spillBase + i * 8));
        }
    }
}

void Codegen::reloadRegs() {
    if (regsUsed == 0) return;
    for (int i = 0; i < 4; i++) {
        if (regsUsed & (1 << i)) {
            emitLoadRegFromBP(i, -(spillBase + i * 8));
        }
    }
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
    // Short-circuit && and ||
    if (bin->op == "&&") {
        int leftReg = emitExpr(bin->left.get());
        emit8(0x48); emit8(0x85); emit8((uint8_t)(0xC0 | (leftReg & 7)));
        int falseLabel = newLabel();
        emit8(0x0F); emit8(0x84);
        jmpFixups.push_back({code.size(), falseLabel}); emit32(0);
        freeReg(leftReg);
        int rightReg = emitExpr(bin->right.get());
        emit8(0x48); emit8(0x85); emit8((uint8_t)(0xC0 | (rightReg & 7)));
        freeReg(rightReg);
        int trueLabel = newLabel();
        emit8(0x0F); emit8(0x85);
        jmpFixups.push_back({code.size(), trueLabel}); emit32(0);
        int resultReg = allocReg(); if (resultReg < 0) resultReg = 0;
        int endLabel = newLabel();
        emitLabel(falseLabel);
        emitMovRegImm(resultReg, 0);
        emitJmp(endLabel);
        emitLabel(trueLabel);
        emitMovRegImm(resultReg, 1);
        emitLabel(endLabel);
        return resultReg;
    }
    if (bin->op == "||") {
        int leftReg = emitExpr(bin->left.get());
        emit8(0x48); emit8(0x85); emit8((uint8_t)(0xC0 | (leftReg & 7)));
        int trueLabel = newLabel();
        emit8(0x0F); emit8(0x85);
        jmpFixups.push_back({code.size(), trueLabel}); emit32(0);
        freeReg(leftReg);
        int rightReg = emitExpr(bin->right.get());
        emit8(0x48); emit8(0x85); emit8((uint8_t)(0xC0 | (rightReg & 7)));
        freeReg(rightReg);
        int falseLabel = newLabel();
        emit8(0x0F); emit8(0x84);
        jmpFixups.push_back({code.size(), falseLabel}); emit32(0);
        int resultReg = allocReg(); if (resultReg < 0) resultReg = 0;
        int endLabel = newLabel();
        emitLabel(trueLabel);
        emitMovRegImm(resultReg, 1);
        emitJmp(endLabel);
        emitLabel(falseLabel);
        emitMovRegImm(resultReg, 0);
        emitLabel(endLabel);
        return resultReg;
    }

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
                if (rightReg == 0 && popReg == 1) {
                    // left in rcx, right in rax → swap via xchg
                    emit8(0x48); emit8(0x87); emit8(0xC1); // xchg rax, rcx
                } else {
                    if (rightReg == 0) { emit8(0x50); emit8(0x59); }
                    else if (rightReg == 1) { emit8(0x51); emit8(0x59); }
                    else if (rightReg == 2) { emit8(0x52); emit8(0x59); }
                    else if (rightReg == 3) { emit8(0x53); emit8(0x59); }
                    else { emit8(0x50); emit8(0x59); }
                    emitMovReg(0, popReg);
                }
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
                jmpFixups.push_back({code.size(), endLabel});
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
            // idiv needs divisor in rcx, but pop rcx can clobber a live rcx from outer expr
            bool saveRcx = (regsUsed & 2) != 0 && rightReg != 1;
            if (saveRcx) emit8(0x51);  // push rcx (save outer value)
            if (rightReg == 0) emit8(0x50);
            else if (rightReg == 1) emit8(0x51);
            else if (rightReg == 2) emit8(0x52);
            else if (rightReg == 3) emit8(0x53);
            emitMovReg(0, tempReg);
            emit8(0x48); emit8(0x99);  // cqo (sign-extend RAX to RDX:RAX, 64-bit)
            emit8(0x59);  // pop rcx
            emit8(0x48); emit8(0xF7); emit8(0xF9);  // idiv rcx (64-bit signed divide)
            if (saveRcx) emit8(0x59);  // pop rcx (restore outer value)
            freeReg(rightReg);
            freeReg(tempReg);
            regsUsed |= 1;  // RAX holds the division result
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
            jmpFixups.push_back({code.size(), endLabel});
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
        emit8(0x48); emit8(0x8D);
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
            // test reg, reg (sets ZF if zero)
            emit8(0x48); emit8(0x85); emit8((uint8_t)(0xC0 | (r & 7)));
            // setz reg (set to 1 if zero flag)
            emit8(0x0F); emit8(0x94); emit8((uint8_t)(0xC0 | (r & 7)));
            // movzx r32, r8 (zero-extend to 64-bit)
            emit8(0x48); emit8(0x0F); emit8(0xB6); emit8((uint8_t)(0xC0 | (r & 7)));
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
            spillRegs();
            regsUsed = 0;
            int sizeReg = emitExpr(call->args[0].get());
            if (sizeReg != 1) { emitMovReg(1, sizeReg); freeReg(sizeReg); sizeReg = 1; }
            emit8(0x48); emit8(0x83); emit8(0xC1); emit8(15);  // add rcx, 15
            emit8(0x48); emit8(0x83); emit8(0xE1); emit8(0xF0); // and rcx, -16
            emit8(0x48); emit8(0x83); emit8(0xC1); emit8(16);  // add rcx, 16
            emit8(0x48); emit8(0x89); emit8(0xCB);  // mov rbx, rcx (save totalSize)
            emit8(0x48); emit8(0x8D); emit8(0x05);
            heapFixups.push_back({code.size(), heapAreaRVA}); emit32(0);
            int bumpLabel = newLabel();
            int failLabel = newLabel();
            int doneLabel = newLabel();
            emit8(0x48); emit8(0x8B); emit8(0x15);
            heapFixups.push_back({code.size(), heapFreeHeadRVA}); emit32(0);
            emit8(0x48); emit8(0x85); emit8(0xD2);  // test rdx, rdx
            emit8(0x0F); emit8(0x84);
            jmpFixups.push_back({code.size(), bumpLabel}); emit32(0);
            emit8(0x48); emit8(0x8B); emit8(0x4A); emit8(8);  // mov rcx, [rdx+8]
            emit8(0x48); emit8(0x39); emit8(0xD9);  // cmp rcx, rbx
            emit8(0x0F); emit8(0x82);
            jmpFixups.push_back({code.size(), bumpLabel}); emit32(0);
            emit8(0x48); emit8(0x8B); emit8(0x0A);  // mov rcx, [rdx]
            emit8(0x48); emit8(0x89); emit8(0x0D);
            heapFixups.push_back({code.size(), heapFreeHeadRVA}); emit32(0);
            emit8(0x31); emit8(0xC9);  // xor ecx, ecx
            emit8(0x48); emit8(0x89); emit8(0x0A);  // mov [rdx], rcx
            emit8(0x48); emit8(0x8D); emit8(0x42); emit8(0x10);  // lea rax, [rdx+16]
            emitJmp(doneLabel);
            emitLabel(bumpLabel);
            emit8(0x48); emit8(0x8B); emit8(0x15);
            heapFixups.push_back({code.size(), heapOffsetRVA}); emit32(0);
            emit8(0x48); emit8(0x89); emit8(0xD1);  // mov rcx, rdx
            emit8(0x48); emit8(0x01); emit8(0xD9);  // add rcx, rbx
            emit8(0x48); emit8(0x81); emit8(0xF9); emit32(64 * 1024);
            emit8(0x0F); emit8(0x87);
            jmpFixups.push_back({code.size(), failLabel}); emit32(0);
            emit8(0x48); emit8(0x89); emit8(0x0D);
            heapFixups.push_back({code.size(), heapOffsetRVA}); emit32(0);
            emit8(0x48); emit8(0x89); emit8(0x5C); emit8(0x10); emit8(8);  // mov [rax+rdx+8], rbx
            emit8(0x48); emit8(0x8D); emit8(0x44); emit8(0x10); emit8(16); // lea rax, [rax+rdx+16]
            emitJmp(doneLabel);
            emitLabel(failLabel);
            emit8(0x48); emit8(0x31); emit8(0xC0);  // xor eax, eax
            emitLabel(doneLabel);
            freeReg(1); freeReg(2); freeReg(3);
            regsUsed = (uint8_t)saved;
            reloadRegs();
            int r = allocReg(); if (r != 0) { emitMovReg(r, 0); freeReg(0); }
            return r >= 0 ? r : 0;
        }
        if (call->name == "free" && call->args.size() == 1) {
            int r = emitExpr(call->args[0].get());
            if (r != 1) { emitMovReg(1, r); freeReg(r); }
            regsUsed = 0;
            emit8(0x48); emit8(0x8D); emit8(0x41); emit8(0xF0);  // lea rax, [rcx-16]
            emit8(0x48); emit8(0x8B); emit8(0x15);
            heapFixups.push_back({code.size(), heapFreeHeadRVA}); emit32(0);
            emit8(0x48); emit8(0x89); emit8(0x10);  // mov [rax], rdx
            emit8(0x48); emit8(0x89); emit8(0x05);
            heapFixups.push_back({code.size(), heapFreeHeadRVA}); emit32(0);
            emitMovRegImm(0, 0);
            return 0;
        }

        // ============== Arena Allocator Builtins ==============
        // Arena header: [capacity:8][used:8], data at handle+16

        auto emitHeapAlloc = [this](int sizeReg) {
            if (sizeReg != 1) { emitMovReg(1, sizeReg); freeReg(sizeReg); }
            // rcx = size
            emit8(0x48); emit8(0x83); emit8(0xC1); emit8(15);  // add rcx, 15
            emit8(0x48); emit8(0x83); emit8(0xE1); emit8(0xF0); // and rcx, -16
            emit8(0x48); emit8(0x83); emit8(0xC1); emit8(16);  // add rcx, 16 (header)
            emit8(0x48); emit8(0x8D); emit8(0x05);
            heapFixups.push_back({code.size(), heapAreaRVA}); emit32(0);  // rax = heapArea
            emit8(0x48); emit8(0x8B); emit8(0x15);
            heapFixups.push_back({code.size(), heapOffsetRVA}); emit32(0);  // rdx = offset
            emit8(0x49); emit8(0x89); emit8(0xC0); // r8 = rax (save heapArea)
            emit8(0x48); emit8(0x03); emit8(0xCA); // rcx = totalSize + offset = newOffset
            emit8(0x48); emit8(0x81); emit8(0xF9); emit32(64 * 1024);
            int fl = newLabel();
            emit8(0x0F); emit8(0x87); jmpFixups.push_back({code.size(), fl}); emit32(0);
            emit8(0x48); emit8(0x89); emit8(0x0D);
            heapFixups.push_back({code.size(), heapOffsetRVA}); emit32(0);
            emit8(0x48); emit8(0x29); emit8(0xD1); // sub rcx, rdx (totalSize = newOffset - oldOffset)
            emit8(0x49); emit8(0x89); emit8(0x4C); emit8(0x10); emit8(8); // mov [r8+rdx+8], rcx
            emit8(0x49); emit8(0x8D); emit8(0x44); emit8(0x10); emit8(16); // lea rax, [r8+rdx+16]
            int dl = newLabel(); emitJmp(dl);
            emitLabel(fl); emit8(0x48); emit8(0x31); emit8(0xC0); // xor rax, rax (fail=0)
            emitLabel(dl);
            freeReg(1); freeReg(2);
        };

        // arenaCreate(capacity) -> handle
        // Header: [capacity:8][used:8], data at handle+16
        if (call->name == "arenaCreate" && call->args.size() == 1) {
            int saved = regsUsed;
            spillRegs();
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
            reloadRegs();
            int r = allocReg();
            if (r != 0) { emitMovReg(r, 0); freeReg(0); }
            return r >= 0 ? r : 0;
        }

        // arenaAlloc(arena, size) -> ptr
        if (call->name == "arenaAlloc" && call->args.size() == 2) {
            int saved = regsUsed;
            spillRegs();
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
            reloadRegs();
            int r = allocReg();
            if (r != pReg) { emitMovReg(r, pReg); freeReg(pReg); }
            return r;
        }

        // arenaReset(arena) -> void
        if (call->name == "arenaReset" && call->args.size() == 1) {
            int saved = regsUsed;
            spillRegs();
            regsUsed = 0;
            int a = emitExpr(call->args[0].get());
            emitMovQwordDisp8Imm32(a, 8, 0);
            freeReg(a);
            regsUsed = 1;
            emitMovRegImm(0, 0);
            return 0;
        }

        // arenaDestroy(arena) -> void (free the arena block back to heap)
        if (call->name == "arenaDestroy" && call->args.size() == 1) {
            int r = emitExpr(call->args[0].get());
            if (r != 1) { emitMovReg(1, r); freeReg(r); }
            regsUsed = 0;
            emit8(0x48); emit8(0x8D); emit8(0x41); emit8(0xF0); // lea rax, [rcx-16]
            emit8(0x48); emit8(0x8B); emit8(0x15);
            heapFixups.push_back({code.size(), heapFreeHeadRVA}); emit32(0);
            emit8(0x48); emit8(0x89); emit8(0x10); // mov [rax], rdx
            emit8(0x48); emit8(0x89); emit8(0x05);
            heapFixups.push_back({code.size(), heapFreeHeadRVA}); emit32(0);
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
            spillRegs();
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
            reloadRegs();
            int r = allocReg();
            if (r != 0) { emitMovReg(r, 0); freeReg(0); }
            return r >= 0 ? r : 0;
        }

        // poolAlloc(pool) -> ptr
        if (call->name == "poolAlloc" && call->args.size() == 1) {
            int saved = regsUsed;
            spillRegs();
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
            jmpFixups.push_back({code.size(), overflow}); emit32(0);
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
            reloadRegs();
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
            spillRegs();
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
            spillRegs();
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
            // +32 slot header, align to 16, +16 block header
            emit8(0x48); emit8(0x83); emit8(0xC1); emit8(32 + 15); // add rcx, 47
            emit8(0x48); emit8(0x83); emit8(0xE1); emit8(0xF0);    // and rcx, -16
            emit8(0x48); emit8(0x83); emit8(0xC1); emit8(16);      // add rcx, 16 (block header)
            // rcx = totalSize
            emit8(0x48); emit8(0x8D); emit8(0x05);
            heapFixups.push_back({code.size(), heapAreaRVA}); emit32(0);
            emit8(0x48); emit8(0x8B); emit8(0x15);
            heapFixups.push_back({code.size(), heapOffsetRVA}); emit32(0);
            emit8(0x49); emit8(0x89); emit8(0xC0); // mov r8, rax (save heapArea)
            emit8(0x48); emit8(0x03); emit8(0xCA); // rcx = totalSize + offset = newOffset
            emit8(0x48); emit8(0x81); emit8(0xF9); emit32(64 * 1024);
            int scFail = newLabel();
            emit8(0x0F); emit8(0x87);
            jmpFixups.push_back({code.size(), scFail}); emit32(0);
            emit8(0x48); emit8(0x89); emit8(0x0D);
            heapFixups.push_back({code.size(), heapOffsetRVA}); emit32(0);
            emit8(0x48); emit8(0x29); emit8(0xD1); // sub rcx, rdx (totalSize)
            emit8(0x49); emit8(0x89); emit8(0x4C); emit8(0x10); emit8(8); // mov [r8+rdx+8], totalSize
            emit8(0x49); emit8(0x8D); emit8(0x44); emit8(0x10); emit8(16); // lea rax, [r8+rdx+16]
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
            reloadRegs();
            int r = allocReg();
            if (r != 0) { emitMovReg(r, 0); freeReg(0); }
            return r >= 0 ? r : 0;
        }

        // slotSpawn(slot) -> handle
        if (call->name == "slotSpawn" && call->args.size() == 1) {
            int saved = regsUsed;
            spillRegs();
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
            reloadRegs();
            int r = allocReg();
            if (r != 0) { emitMovReg(r, 0); freeReg(0); }
            return r >= 0 ? r : 0;
        }

        // slotKill(slot, handle) -> void
        if (call->name == "slotKill" && call->args.size() == 2) {
            int saved = regsUsed;
            spillRegs();
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
            reloadRegs();
            emitMovRegImm(0, 0);
            return 0;
        }

        // slotGetI64(slot, handle, byteOffset) -> int
        if (call->name == "slotGetI64" && call->args.size() == 3) {
            int saved = regsUsed;
            spillRegs();
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
            reloadRegs();
            int r = allocReg();
            if (r != 1) { emitMovReg(r, 1); freeReg(1); }
            return r >= 0 ? r : 0;
        }

        // slotSetI64(slot, handle, byteOffset, value) -> void
        if (call->name == "slotSetI64" && call->args.size() == 4) {
            int saved = regsUsed;
            spillRegs();
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
            reloadRegs();
            emitMovRegImm(0, 0);
            return 0;
        }

        // slotGetF32(slot, handle, byteOffset) -> i64 (float bits)
        if (call->name == "slotGetF32" && call->args.size() == 3) {
            int saved = regsUsed;
            spillRegs();
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
            reloadRegs();
            int r = allocReg();
            if (r != 1) { emitMovReg(r, 1); freeReg(1); }
            return r >= 0 ? r : 0;
        }

        // slotSetF32(slot, handle, byteOffset, value) -> void
        if (call->name == "slotSetF32" && call->args.size() == 4) {
            int saved = regsUsed;
            spillRegs();
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
            reloadRegs();
            emitMovRegImm(0, 0);
            return 0;
        }

        // slotCount(slot) -> int
        if (call->name == "slotCount" && call->args.size() == 1) {
            int saved = regsUsed;
            spillRegs();
            regsUsed = 0;
            int sReg = emitExpr(call->args[0].get());
            if (sReg != 3) { emitMovReg(3, sReg); freeReg(sReg); }
            emitLoadQwordDisp8(0, 3, 24); // rax = aliveCount
            freeReg(3);
            regsUsed = (uint8_t)saved;
            reloadRegs();
            int r = allocReg();
            if (r != 0) { emitMovReg(r, 0); freeReg(0); }
            return r >= 0 ? r : 0;
        }

        // slotReset(slot) -> void
        if (call->name == "slotReset" && call->args.size() == 1) {
            int saved = regsUsed;
            spillRegs();
            regsUsed = 0;
            int sReg = emitExpr(call->args[0].get());
            if (sReg != 3) { emitMovReg(3, sReg); freeReg(sReg); }
            emitMovQwordDisp8Imm32(3, 16, -1); // freeHead = -1
            emitMovQwordDisp8Imm32(3, 24, 0);  // aliveCount = 0
            freeReg(3);
            regsUsed = (uint8_t)saved;
            reloadRegs();
            emitMovRegImm(0, 0);
            return 0;
        }

        // slotDestroy(slot) -> void (free the slot block back to heap)
        if (call->name == "slotDestroy" && call->args.size() == 1) {
            int r = emitExpr(call->args[0].get());
            if (r != 1) { emitMovReg(1, r); freeReg(r); }
            regsUsed = 0;
            emit8(0x48); emit8(0x8D); emit8(0x41); emit8(0xF0); // lea rax, [rcx-16]
            emit8(0x48); emit8(0x8B); emit8(0x15);
            heapFixups.push_back({code.size(), heapFreeHeadRVA}); emit32(0);
            emit8(0x48); emit8(0x89); emit8(0x10); // mov [rax], rdx
            emit8(0x48); emit8(0x89); emit8(0x05);
            heapFixups.push_back({code.size(), heapFreeHeadRVA}); emit32(0);
            emitMovRegImm(0, 0);
            return 0;
        }

        // sleep(seconds) -> void
        if (call->name == "sleep" && call->args.size() == 1) {
            int saved = regsUsed;
            spillRegs();
            regsUsed = 0;
            if (isFloatExpr(call->args[0].get())) {
                // Float argument: convert to int via SSE, then multiply
                int x = emitFloatExpr(call->args[0].get());
                int r = allocReg(); if (r < 0) r = 0;
                emitCvtss2si(r, x);
                freeXmmReg(x);
                freeReg(0);
                // r = seconds as int; move to rax and multiply
                if (r != 0) { emitMovReg(0, r); freeReg(r); }
                emit8(0x48); emit8(0x69); emit8(0xC0); emit32(1000); // imul rax, rax, 1000
            } else {
                int sReg = emitExpr(call->args[0].get());
                if (sReg != 0) { emitMovReg(0, sReg); freeReg(sReg); }
                // rax = seconds; ms = rax * 1000
                emit8(0x48); emit8(0x69); emit8(0xC0); emit32(1000); // imul rax, rax, 1000
            }
            // sub rsp, 0x20; mov ecx, eax; call Sleep; add rsp, 0x20
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20); // sub rsp, 32
            emit8(0x89); emit8(0xC1); // mov ecx, eax
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "Sleep", "kernel32.dll"});
            emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20); // add rsp, 32
            freeReg(3);
            regsUsed = (uint8_t)saved;
            reloadRegs();
            emitMovRegImm(0, 0);
            return 0;
        }

        // pause() -> void: print message and wait for Enter
        if (call->name == "pause" && call->args.size() == 0) {
            int saved = regsUsed;
            spillRegs();
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
            reloadRegs();
            emitMovRegImm(0, 0);
            return 0;
        }

        if (call->name == "print" && call->args.size() == 1) {
            int saved = regsUsed;
            spillRegs();
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

                // Check for negative number
                int isNegLabel = newLabel();
                int notNegLabel = newLabel();
                emit8(0x48); emit8(0x85); emit8(0xC0);  // test rax, rax
                emit8(0x0F); emit8(0x88);  // js isNegLabel (jump if sign flag set)
                jmpFixups.push_back({code.size(), isNegLabel});
                emit32(0);
                emit8(0xC6); emit8(0x44); emit8(0x24); emit8(0x0A); emit8(0x00);  // mov byte [rsp+10], 0 (positive flag)
                emitJmp(notNegLabel);
                emitLabel(isNegLabel);
                emit8(0x48); emit8(0xF7); emit8(0xD8);  // neg rax
                emit8(0xC6); emit8(0x44); emit8(0x24); emit8(0x0A); emit8(0x01);  // mov byte [rsp+10], 1 (negative flag)
                emitLabel(notNegLabel);

                int notZero = newLabel();
                emit8(0x48); emit8(0x85); emit8(0xC0);  // test rax, rax
                emit8(0x0F); emit8(0x85);
                jmpFixups.push_back({code.size(), notZero});
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
                jmpFixups.push_back({code.size(), convLoop});
                emit32(0);

                // If was negative, prepend '-' before the digits
                int notNegPrint = newLabel();
                emit8(0x80); emit8(0x7C); emit8(0x24); emit8(0x0A); emit8(0x01);  // cmp byte [rsp+10], 1
                emit8(0x75);  // jne notNegPrint
                int jnePos2 = (int)code.size();
                emit8(0x00);  // placeholder
                emit8(0x49); emit8(0xFF); emit8(0xCA);  // dec r10
                emit8(0x41); emit8(0xC6); emit8(0x02); emit8(0x2D);  // mov byte [r10], '-'
                emit8(0x41); emit8(0xFF); emit8(0xC0);  // inc r8d
                emitLabel(notNegPrint);
                code[jnePos2] = (uint8_t)((int)code.size() - jnePos2 - 1);

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
            reloadRegs();
            return 0;
        }

        // ============== Shared Memory Builtins ==============
        // peek(addr) — reads a 64-bit value from the given address
        if (call->name == "peek" && call->args.size() == 1) {
            int saved = regsUsed;
            spillRegs();
            regsUsed = 0;
            int addrReg = emitExpr(call->args[0].get());
            if (addrReg != 0) { emitMovReg(0, addrReg); freeReg(addrReg); }
            else freeReg(0);
            emit8(0x48); emit8(0x8B); emit8(0x00); // mov rax, [rax]
            regsUsed = 1;
            return 0;
        }
        // poke(addr, val) — writes val (64-bit) to the given address
        if (call->name == "poke" && call->args.size() == 2) {
            int saved = regsUsed;
            spillRegs();
            regsUsed = 0;
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

        // ============== GUI Built-in Functions ==============
        if (prog.appType == AppType::GUI) {
            int guiResult;
            if (tryGUICall(call, guiResult)) return guiResult;
        }

        // ============== DX11 Shader Built-in Functions ==============
        if (prog.renderType == RenderType::DX11) {
            int dxResult;
            if (tryDX11Call(call, dxResult)) return dxResult;
        }

        // ============== EFI / Bare-metal Built-in Functions ==============
        if (prog.appType == AppType::EFI || prog.appType == AppType::Bare) {
            int efiResult;
            if (tryEFICall(call, efiResult)) return efiResult;
        }

        int savedRegs = regsUsed;
        int savedXmmRegs = xmmRegsUsed;
        regsUsed = 0;
        xmmRegsUsed = 0;

        // Count stack args (args beyond first 4 go on stack)
        int totalArgs = (int)call->args.size();
        int stackSlots = totalArgs > 4 ? totalArgs - 4 : 0;
        int stackAlloc = 0x20 + stackSlots * 8;
        stackAlloc = (stackAlloc + 15) & ~15;
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
        // Check if the function is extern (from DLL import or auto-imported from embedded DLL)
        for (auto& func : prog.functions) {
            if (func->isExtern && func->name == call->name) {
                isImportCall = true;
                importDll = func->dllName;
                break;
            }
        }
        // Also check externFuncMap for auto-imported functions not in prog.functions
        if (!isImportCall) {
            auto eit = externFuncMap.find(call->name);
            if (eit != externFuncMap.end()) {
                isImportCall = true;
                importDll = eit->second.first;
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
            } else if (vi) {
                int r = emitExpr(assign->value.get());
                if (r != 0) { emitMovReg(0, r); freeReg(r); }
                emitStoreToBP64(vi->offset);
                freeReg(0);
            } else {
                fprintf(stderr, "Error: undefined variable '%s'\n", assign->name.c_str());
                exit(1);
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
        jmpFixups.push_back({code.size(), elseLabel});
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
        jmpFixups.push_back({code.size(), endLabel});
        emit32(0);
        for (auto& s : whileStmt->body.stmts) emitStmt(s.get());
        emitJmp(loopLabel);
        emitLabel(endLabel);
    } else if (auto forStmt = dynamic_cast<ForStmt*>(stmt)) {
        int loopLabel = newLabel();
        int endLabel = newLabel();

        int startReg = emitExpr(forStmt->start.get());
        if (startReg != 0) { emitMovReg(0, startReg); freeReg(startReg); }
        emitStoreToBP64(varInfos[forStmt->varName].offset);
        freeReg(0);

        // Determine if step is negative at compile time for correct comparison
        bool countdown = false;
        if (forStmt->step) {
            if (auto numExpr = dynamic_cast<NumberExpr*>(forStmt->step.get())) {
                if (numExpr->value < 0) countdown = true;
            }
        }

        emitLabel(loopLabel);

        int varReg = allocReg(); if (varReg < 0) varReg = 0;
        emitLoadRegFromBP64(varReg, varInfos[forStmt->varName].offset);
        int endReg = emitExpr(forStmt->end.get());
        emit8(0x48); emit8(0x39); emit8((uint8_t)(0xC0 + endReg * 8 + varReg));
        freeReg(endReg);
        if (countdown) {
            emit8(0x0F); emit8(0x8E); // jle endLabel (countdown: exit when var <= end)
        } else {
            emit8(0x0F); emit8(0x8D); // jge endLabel (countup: exit when var >= end)
        }
        jmpFixups.push_back({code.size(), endLabel});
        emit32(0);
        freeReg(varReg);

        for (auto& s : forStmt->body.stmts) emitStmt(s.get());

        int curReg = allocReg(); if (curReg < 0) curReg = 0;
        emitLoadRegFromBP64(curReg, varInfos[forStmt->varName].offset);
        if (forStmt->step) {
            int stepReg = emitExpr(forStmt->step.get());
            emitAdd(curReg, stepReg);
            freeReg(stepReg);
        } else {
            emit8(0x48); emit8(0xFF); emit8(0xC0 + curReg); // inc reg
        }
        if (curReg != 0) { emitMovReg(0, curReg); freeReg(curReg); }
        emitStoreToBP64(varInfos[forStmt->varName].offset);
        freeReg(0);

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
            if (offset % fieldSize != 0) offset += fieldSize - (offset % fieldSize);
            layout.fieldOffsets[f.name] = offset;
            layout.fieldTypes[f.name] = f.type;
            offset += fieldSize;
        }
        layout.totalSize = offset;
        structLayouts[sd->name] = layout;
    }
}

void Codegen::allocateBlockVars(const Block& block) {
    for (auto& stmt : block.stmts) {
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
        } else if (auto forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
            if (varInfos.find(forStmt->varName) == varInfos.end()) {
                locals += 8;
                VarInfo vi;
                vi.offset = -(locals);
                vi.type = {TypeKind::Int};
                varInfos[forStmt->varName] = vi;
            }
            allocateBlockVars(forStmt->body);
        } else if (auto ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
            allocateBlockVars(ifStmt->thenBlock);
            allocateBlockVars(ifStmt->elseBlock);
        } else if (auto whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
            allocateBlockVars(whileStmt->body);
        }
    }
}

void Codegen::emitFunction(FunctionDecl* func) {
    funcOffsets[func->name] = code.size();
    varInfos.clear();
    locals = 0;

    for (size_t i = 0; i < func->params.size(); i++) {
        int off = (int)(24 + i * 8);
        VarInfo vi;
        vi.offset = off;
        vi.type = func->params[i].type;
        varInfos[func->params[i].name] = vi;
    }

    allocateBlockVars(func->body);

    spillBase = locals + 8; // spill area starts after local vars (32 bytes for 4 regs)
    locals += 32;           // reserve spill area

    frameSize = ((locals + 15) & ~15) + 8;
    regsUsed = 0;
    xmmRegsUsed = 0;
    funcEndLabel = newLabel();

    emit8(0x55);  // push rbp
    emit8(0x53);  // push rbx (callee-saved)
    emit8(0x48); emit8(0x89); emit8(0xE5);  // mov rbp, rsp
    if (frameSize > 0) {
        if (frameSize <= 127) {
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8((uint8_t)frameSize);
        } else {
            emit8(0x48); emit8(0x81); emit8(0xEC); emit32((uint32_t)frameSize);
        }
    }

    for (size_t i = 0; i < func->params.size() && i < 4; i++) {
        int off = (int)(24 + i * 8);
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
    emit8(0x5B);  // pop rbx (callee-saved)
    emit8(0x5D);  // pop rbp
    emit8(0xC3);
}

void Codegen::emitEntryPoint() {
    entryPointCodeOffset = code.size();

    if (prog.appType == AppType::Bare) {
        entryPointCodeOffset = code.size();
        bool hm = funcOffsets.count("main") > 0;
        if (hm) { emit8(0xE8); size_t fp=code.size(); emit32(0); callFixups.push_back({fp,"main"}); }
        else if (!prog.functions.empty()) { emit8(0xE8); size_t fp=code.size(); emit32(0); callFixups.push_back({fp,prog.functions[0]->name}); }
        int l = newLabel(); emitLabel(l); emit8(0xF4); emit8(0xEB); emit8(0xFC);
        return;
    }

    if (prog.appType == AppType::EFI) {
        // EFI entry point: EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
        // rcx = ImageHandle, rdx = SystemTable
        // Just call main() and return 0 (EFI_SUCCESS)
        emit8(0x55);  // push rbp
        emit8(0x48); emit8(0x89); emit8(0xE5);  // mov rbp, rsp
        emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);  // sub rsp, 0x20 (shadow space)

        bool hasMain = funcOffsets.count("main") > 0;
        if (hasMain) {
            emit8(0xE8);
            size_t fixupPos = code.size();
            emit32(0);
            callFixups.push_back({fixupPos, "main"});
        } else if (!prog.functions.empty()) {
            emit8(0xE8);
            size_t fixupPos = code.size();
            emit32(0);
            callFixups.push_back({fixupPos, prog.functions[0]->name});
        }

        // Return 0 (EFI_SUCCESS)
        emit8(0x33); emit8(0xC0);  // xor eax, eax
        emit8(0xC9);  // leave
        emit8(0xC3);  // ret
        return;
    }

    if (!embeddedDLLs.empty()) {
        // Extra stack space for loader (CreateFileA has 7 params, needs stack space)
        emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x58); // sub rsp, 0x58
        emitEmbeddedLoader();
    } else {
        // sub rsp, 0x28: 32 bytes shadow space + 8 alignment padding
        // (entry rsp has 8 mod 16 from OS call; sub 0x28 → rsp 0 mod 16 for calling main)
        emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x28);
    }

    bool hasMain = funcOffsets.count("main") > 0;

    if (prog.appType == AppType::GUI) {
        // Call main first
        if (hasMain) {
            emit8(0xE8);
            size_t fixupPos = code.size();
            emit32(0);
            callFixups.push_back({fixupPos, "main"});
        } else if (!prog.functions.empty()) {
            emit8(0xE8);
            size_t fixupPos = code.size();
            emit32(0);
            callFixups.push_back({fixupPos, prog.functions[0]->name});
        }

        // Save exit code, register VEH handler just before ExitProcess
        // VEH catches D3D11/DXGI cleanup exceptions during DLL_PROCESS_DETACH
        emit8(0x89); emit8(0xC3);  // mov ebx, eax (save exit code, callee-saved)
        emit8(0xB9); emit32(1);    // mov ecx, 1 (First = add as first handler)
        emit8(0x48); emit8(0x8D); emit8(0x15);  // lea rdx, [rip + handler]
        size_t leaDispPos = code.size();
        emit32(0);  // placeholder
        emit8(0xFF); emit8(0x15);
        importCallFixups.push_back({code.size(), "AddVectoredExceptionHandler", "kernel32.dll"});
        emit32(0);

        // ExitProcess(exitCode)
        emit8(0x89); emit8(0xD9);  // mov ecx, ebx (restore exit code)
        emit8(0xFF); emit8(0x15);
        entryExitProcessFixup = code.size();
        emit32(0);

        // --- VEH Handler ---
        size_t handlerStart = code.size();
        int32_t handlerDisp = (int32_t)(handlerStart - (leaDispPos + 4));
        code[leaDispPos]     = (uint8_t)(handlerDisp & 0xFF);
        code[leaDispPos + 1] = (uint8_t)((handlerDisp >> 8) & 0xFF);
        code[leaDispPos + 2] = (uint8_t)((handlerDisp >> 16) & 0xFF);
        code[leaDispPos + 3] = (uint8_t)((handlerDisp >> 24) & 0xFF);

        // Suppress ALL exceptions during DLL cleanup (process is exiting anyway)
        emit8(0xB8); emit32(0xFFFFFFFF);  // mov eax, -1 (EXCEPTION_CONTINUE_EXECUTION)
        emit8(0xC3);                       // ret

    } else {
        if (hasMain) {
            emit8(0xE8);
            size_t fixupPos = code.size();
            emit32(0);
            callFixups.push_back({fixupPos, "main"});
        } else if (!prog.functions.empty()) {
            emit8(0xE8);
            size_t fixupPos = code.size();
            emit32(0);
            callFixups.push_back({fixupPos, prog.functions[0]->name});
        } else {
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x28);  // add rsp, 0x28
            emit8(0x33); emit8(0xC9);  // xor ecx, ecx
        }

        emit8(0x8B); emit8(0xC8);  // mov ecx, eax (exit code)
        emit8(0xFF); emit8(0x15);
        entryExitProcessFixup = code.size();
        emit32(0);
    }
}

void Codegen::generate(const std::string& outputPath) {
    generateWide(safeNarrowToPath(outputPath).wstring());
}


void Codegen::generateWide(const std::wstring& outputPath) {
    outputDir = std::filesystem::path(outputPath).parent_path();
    computeStructLayouts();
    collectStrings();
    computeSectionRVAs();

    if (prog.appType != AppType::EFI && prog.appType != AppType::Bare) {
        buildImportData();
    }

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

    // Convert wide output path back to narrow for buildPE
    {
        int ulen = WideCharToMultiByte(0 /*CP_ACP*/, 0, outputPath.c_str(), -1, NULL, 0, NULL, NULL);
        std::string narrowOut(ulen > 0 ? ulen - 1 : 0, '\0');
        if (ulen > 1) WideCharToMultiByte(0 /*CP_ACP*/, 0, outputPath.c_str(), -1, &narrowOut[0], ulen, NULL, NULL);
        if (prog.appType == AppType::Bare) {
            std::ofstream rf(narrowOut, std::ios::binary);
            rf.write((const char*)code.data(), code.size());
            rf.close();
            std::cout << "Compiled raw: " << narrowOut << " (" << code.size() << " B)\n";
        } else {
            buildPE(narrowOut);
        }
    }
}
