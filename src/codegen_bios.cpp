// codegen_bios.cpp — BIOS/Bare-metal built-in functions
// Supports:
//   kernel_mode: independent — VGA directly (0xB8000), no BIOS calls after boot
//   kernel_mode: dependent — can use BIOS interrupts

#include "codegen.h"
#include "ast.h"
#include "parser.h"
#include <iostream>
#include <vector>

using namespace std;

bool Codegen::tryBIOSCall(CallExpr* call, int& resultReg) {
    // ======================== cli() ========================
    if (call->name == "cli" && call->args.size() == 0) {
        emit8(0xFA); // cli
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== sti() ========================
    if (call->name == "sti" && call->args.size() == 0) {
        emit8(0xFB); // sti
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== hlt() ========================
    if (call->name == "hlt" && call->args.size() == 0) {
        emit8(0xF4); // hlt
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== halt() ========================
    if (call->name == "halt" && call->args.size() == 0) {
        int loopTop = newLabel();
        emitLabel(loopTop);
        emit8(0xF4); // hlt
        emitJmp(loopTop);
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== int(n) ========================
    if (call->name == "int" && call->args.size() == 1) {
        if (auto num = dynamic_cast<NumberExpr*>(call->args[0].get())) {
            emit8(0xCD); // INT imm8
            emit8((uint8_t)(num->value & 0xFF));
        }
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== inb(port) ========================
    if (call->name == "inb" && call->args.size() == 1) {
        emit8(0xEC); // in al, dx
        emit8(0x48); emit8(0x0F); emit8(0xB6); emit8(0xC0);
        resultReg = 0;
        return true;
    }

    // ======================== inw(port) ========================
    if (call->name == "inw" && call->args.size() == 1) {
        emit8(0x66); emit8(0xED);
        emit8(0x0F); emit8(0xB7); emit8(0xC0);
        resultReg = 0;
        return true;
    }

    // ======================== ind(port) ========================
    if (call->name == "ind" && call->args.size() == 1) {
        emit8(0xED);
        resultReg = 0;
        return true;
    }

    // ======================== outb(port, val) ========================
    if (call->name == "outb" && call->args.size() == 2) {
        emit8(0xEE);
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== outw(port, val) ========================
    if (call->name == "outw" && call->args.size() == 2) {
        emit8(0x66); emit8(0xEF);
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== outd(port, val) ========================
    if (call->name == "outd" && call->args.size() == 2) {
        emit8(0xEF);
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== lidt(ptr) ========================
    if (call->name == "lidt" && call->args.size() == 1) {
        emit8(0x0F); emit8(0x01); emit8(0x18);
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== lgdt(ptr) ========================
    if (call->name == "lgdt" && call->args.size() == 1) {
        emit8(0x0F); emit8(0x01); emit8(0x10);
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // =====================================================================
    // VGA Functions (Direct VGA text mode 0xB8000)
    // Cursor position stored at 0x7E00
    // =====================================================================

    // vga_clear() — clear VGA text buffer
    if (call->name == "vga_clear" && call->args.size() == 0) {
        emit8(0x48); emit8(0xBF);
        emit8(0x00); emit8(0x80); emit8(0x0B); 
        emit8(0x00); emit8(0x00); emit8(0x00); emit8(0x00);
        emit8(0x66); emit8(0xB8);
        emit8(0x20); emit8(0x07);
        emit8(0x48); emit8(0xC7); emit8(0xC1);
        emit8(0xD0); emit8(0x07); emit8(0x00); emit8(0x00);
        emit8(0x66); emit8(0xF3); emit8(0xAB);
        emit8(0x48); emit8(0xC7); emit8(0x04); emit8(0x25);
        emit8(0x00); emit8(0x7E); emit8(0x00); emit8(0x00);
        emit8(0x00); emit8(0x00); emit8(0x00); emit8(0x00);
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // vga_putc(char, attr) — write char at cursor
    if (call->name == "vga_putc" && call->args.size() == 2) {
        // rdi=char, rsi=attr
        emit8(0x89); emit8(0xF0); // mov eax, edi
        emit8(0xC1); emit8(0xE0); emit8(0x08); // shl eax, 8
        emit8(0x40); emit8(0x8A); emit8(0xF2); // mov sil, dl
        emit8(0x88); emit8(0xE0); // mov al, ah
        emit8(0x48); emit8(0xBF);
        emit8(0x00); emit8(0x80); emit8(0x0B); 
        emit8(0x00); emit8(0x00); emit8(0x00); emit8(0x00);
        emit8(0x48); emit8(0x8B); emit8(0x0C); emit8(0x25);
        emit8(0x00); emit8(0x7E); emit8(0x00); emit8(0x00);
        emit8(0x48); emit8(0x01); emit8(0xC9);
        emit8(0x48); emit8(0x01); emit8(0xCF);
        emit8(0x66); emit8(0x89); emit8(0x07);
        emit8(0x48); emit8(0xFF); emit8(0x04); emit8(0x25);
        emit8(0x00); emit8(0x7E); emit8(0x00); emit8(0x00);
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // vga_print(ptr) — print string at cursor
    if (call->name == "vga_print" && call->args.size() == 1) {
        emit8(0x48); emit8(0xBF);
        emit8(0x00); emit8(0x80); emit8(0x0B); 
        emit8(0x00); emit8(0x00); emit8(0x00); emit8(0x00);
        emit8(0x48); emit8(0x8B); emit8(0x0C); emit8(0x25);
        emit8(0x00); emit8(0x7E); emit8(0x00); emit8(0x00);
        emit8(0x48); emit8(0x01); emit8(0xCF);
        emit8(0x48); emit8(0x01); emit8(0xCF);
        
        int loopStart = newLabel();
        int loopEnd = newLabel();
        
        emitLabel(loopStart);
        emit8(0x0F); emit8(0xB6); emit8(0x06);
        emit8(0x85); emit8(0xC0);
        emitJcc("e", loopEnd);
        emit8(0xB4); emit8(0x07);
        emit8(0x66); emit8(0x89); emit8(0x07);
        emit8(0x48); emit8(0xFF); emit8(0xC6);
        emit8(0x48); emit8(0x83); emit8(0xC7); emit8(0x02);
        emitJmp(loopStart);
        
        emitLabel(loopEnd);
        emit8(0x48); emit8(0x89); emit8(0xF8);
        emit8(0x48); emit8(0x2D);
        emit8(0x00); emit8(0x80); emit8(0x0B); 
        emit8(0x00); emit8(0x00); emit8(0x00); emit8(0x00);
        emit8(0x48); emit8(0xD1); emit8(0xE8);
        emit8(0x48); emit8(0xA3);
        emit8(0x00); emit8(0x7E); emit8(0x00); emit8(0x00);
        emit8(0x00); emit8(0x00); emit8(0x00); emit8(0x00);
        
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // =====================================================================
    // BIOS Interrupt Functions (kernel_mode: dependent)
    // =====================================================================

    // bios_get_char() — get char from keyboard (INT 0x16 AH=0x00)
    if (call->name == "bios_get_char" && call->args.size() == 0) {
        emit8(0xB4); emit8(0x00); // mov ah, 0
        emit8(0xCD); emit8(0x16); // int 0x16
        // AX = scan code in AH, ASCII in AL
        resultReg = 0;
        return true;
    }

    // bios_check_key() — check if key available (INT 0x16 AH=0x01)
    if (call->name == "bios_check_key" && call->args.size() == 0) {
        emit8(0xB4); emit8(0x01); // mov ah, 1
        emit8(0xCD); emit8(0x16); // int 0x16
        // ZF = 1 if no key, 0 if key available
        emit8(0x98); // cbw - sign extend
        emit8(0xB4); emit8(0x00); // mov ah, 0
        resultReg = 0;
        return true;
    }

    // bios_set_cursor(x, y) — set cursor (INT 0x10 AH=0x02)
    if (call->name == "bios_set_cursor" && call->args.size() == 2) {
        // rdi=x, rsi=y
        emit8(0xB4); emit8(0x02); // mov ah, 2
        emit8(0xB7); emit8(0x00); // mov bh, 0 (page 0)
        emit8(0x89); emit8(0xF2); // mov dl, sil
        emit8(0x89); emit8(0xD6); // mov dh, sil (y)
        emit8(0xCD); emit8(0x10); // int 0x10
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // bios_scroll(lines) — scroll screen (INT 0x10 AH=0x06)
    if (call->name == "bios_scroll" && call->args.size() == 1) {
        emit8(0xB4); emit8(0x06); // mov ah, 6
        emit8(0x89); emit8(0xC4); // mov al, ah (0 = clear)
        emit8(0x31); emit8(0xFF); // xor bh, bh (attribute 0)
        emit8(0x31); emit8(0xDB); // xor bl, bl
        emit8(0xB7); emit8(0x00); // mov bh, 0
        emit8(0xB6); emit8(0x00); // mov dh, 0 (top row)
        emit8(0xB2); emit8(0x00); // mov dl, 0 (left col)
        emit8(0xB7); emit8(0x18); // mov bh, 24 (bottom row)
        emit8(0xB3); emit8(0x4F); // mov bl, 79 (right col)
        emit8(0xCD); emit8(0x10); // int 0x10
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // bios_video_mode(mode) — set video mode (INT 0x10 AH=0x00)
    if (call->name == "bios_video_mode" && call->args.size() == 1) {
        emit8(0xB4); emit8(0x00);
        emit8(0xCD); emit8(0x10);
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // bios_video_int() — call INT 0x10 with AH in AL
    if (call->name == "bios_video_int" && call->args.size() == 1) {
        emit8(0x89); emit8(0xC4); // mov ah, al
        emit8(0xCD); emit8(0x10);
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // bios_disk_read(drive, sector, count, buffer) — INT 0x13 AH=0x42
    if (call->name == "bios_disk_read" && call->args.size() == 4) {
        // Complex: DAP-based disk read
        // For now, just return 0
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // bios_memory_map() — get memory map (INT 0x15 EAX=0xE820)
    if (call->name == "bios_memory_map" && call->args.size() == 0) {
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // =====================================================================
    // Framebuffer Functions (for VESA/LFB mode)
    // Info stored at same addresses as GOP
    // =====================================================================

    // fb_clear(color) — clear framebuffer
    if (call->name == "fb_clear" && call->args.size() == 1) {
        emit8(0x89); emit8(0xC7); // mov edi, eax
        emit8(0x48); emit8(0x8B); emit8(0x04); emit8(0x25);
        emit8(0x00); emit8(0x80); emit8(0x00); emit8(0x00);
        emit8(0x48); emit8(0x8B); emit8(0x0C); emit8(0x25);
        emit8(0x0C); emit8(0x80); emit8(0x00); emit8(0x00);
        emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0x0C); emit8(0x25);
        emit8(0x10); emit8(0x80); emit8(0x00); emit8(0x00);
        emit8(0xF3); emit8(0xAB);
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // fb_pixel(x, y, color)
    if (call->name == "fb_pixel" && call->args.size() == 3) {
        emit8(0x50); // push rax
        emit8(0x48); emit8(0x8B); emit8(0x04); emit8(0x25);
        emit8(0x00); emit8(0x80); emit8(0x00); emit8(0x00);
        emit8(0x48); emit8(0x89); emit8(0xC3);
        emit8(0x48); emit8(0x8B); emit8(0x0C); emit8(0x25);
        emit8(0x08); emit8(0x80); emit8(0x00); emit8(0x00);
        emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xDE);
        emit8(0x48); emit8(0x01); emit8(0xC8);
        emit8(0x48); emit8(0x8D); emit8(0x04); emit8(0x87);
        emit8(0x58);
        emit8(0x89); emit8(0x00);
        emitMovRegImm(1, 0);
        resultReg = 0;
        return true;
    }

    // fb_width() / fb_height() / fb_pitch() / fb_addr()
    if (call->name == "fb_width" && call->args.size() == 0) {
        emit8(0x48); emit8(0x8B); emit8(0x04); emit8(0x25);
        emit8(0x0C); emit8(0x80); emit8(0x00); emit8(0x00);
        resultReg = 0;
        return true;
    }
    if (call->name == "fb_height" && call->args.size() == 0) {
        emit8(0x48); emit8(0x8B); emit8(0x04); emit8(0x25);
        emit8(0x10); emit8(0x80); emit8(0x00); emit8(0x00);
        resultReg = 0;
        return true;
    }
    if (call->name == "fb_pitch" && call->args.size() == 0) {
        emit8(0x48); emit8(0x8B); emit8(0x04); emit8(0x25);
        emit8(0x08); emit8(0x80); emit8(0x00); emit8(0x00);
        resultReg = 0;
        return true;
    }
    if (call->name == "fb_addr" && call->args.size() == 0) {
        emit8(0x48); emit8(0x8B); emit8(0x04); emit8(0x25);
        emit8(0x00); emit8(0x80); emit8(0x00); emit8(0x00);
        resultReg = 0;
        return true;
    }
    if (call->name == "fb_bpp" && call->args.size() == 0) {
        emit8(0x48); emit8(0x8B); emit8(0x04); emit8(0x25);
        emit8(0x14); emit8(0x80); emit8(0x00); emit8(0x00);
        resultReg = 0;
        return true;
    }

    // =====================================================================
    // Memory Access Functions
    // =====================================================================

    // peek(addr)
    if (call->name == "peek" && call->args.size() == 1) {
        emit8(0x48); emit8(0x8B); emit8(0x00);
        resultReg = 0;
        return true;
    }

    // poke(addr, val)
    if (call->name == "poke" && call->args.size() == 2) {
        emit8(0x49); emit8(0x89); emit8(0x08);
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // peek32(addr)
    if (call->name == "peek32" && call->args.size() == 1) {
        emit8(0x8B); emit8(0x00);
        resultReg = 0;
        return true;
    }

    // poke32(addr, val)
    if (call->name == "poke32" && call->args.size() == 2) {
        emit8(0x67); emit8(0x89); emit8(0x08);
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // Not recognized
    return false;
}
