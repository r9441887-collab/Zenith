// codegen_efi.cpp — EFI and Bare-metal built-in functions
// Handles: cli, sti, hlt, int(n), inb, inw, ind, outb, outw, outd
// halt, lidt, lgdt (bare/efi)
// vga_clear, vga_putc, vga_print (bare/BIOS only)
// gop_clear, gop_pixel, gop_rect, gop_char, gop_init (UEFI GOP)
// fb_width, fb_height, fb_pitch, fb_addr (framebuffer info)
// peek, poke (memory access)

#include "codegen.h"
#include "ast.h"
#include "parser.h"
#include <iostream>
#include <vector>

using namespace std;

bool Codegen::tryEFICall(CallExpr* call, int& resultReg) {
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
        emitJmp(loopTop); // jmp $
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
        emit8(0x48); emit8(0x0F); emit8(0xB6); emit8(0xC0); // movzx rax, al
        resultReg = 0;
        return true;
    }

    // ======================== inw(port) ========================
    if (call->name == "inw" && call->args.size() == 1) {
        emit8(0x66); emit8(0xED); // in ax, dx
        emit8(0x0F); emit8(0xB7); emit8(0xC0); // movzx eax, ax
        resultReg = 0;
        return true;
    }

    // ======================== ind(port) ========================
    if (call->name == "ind" && call->args.size() == 1) {
        emit8(0xED); // in eax, dx
        resultReg = 0;
        return true;
    }

    // ======================== outb(port, val) ========================
    if (call->name == "outb" && call->args.size() == 2) {
        emit8(0xEE); // out dx, al
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== outw(port, val) ========================
    if (call->name == "outw" && call->args.size() == 2) {
        emit8(0x66); emit8(0xEF); // out dx, ax
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== outd(port, val) ========================
    if (call->name == "outd" && call->args.size() == 2) {
        emit8(0xEF); // out dx, eax
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== lidt(ptr) ========================
    if (call->name == "lidt" && call->args.size() == 1) {
        emit8(0x0F); emit8(0x01); emit8(0x18); // lidt [rax]
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== lgdt(ptr) ========================
    if (call->name == "lgdt" && call->args.size() == 1) {
        emit8(0x0F); emit8(0x01); emit8(0x10); // lgdt [rax]
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // =====================================================================
    // VGA Functions (Bare/BIOS mode - direct VGA text mode 0xB8000)
    // =====================================================================

    // vga_clear() — fill VGA text buffer with spaces
    if (call->name == "vga_clear" && call->args.size() == 0) {
        // rdi = 0xB8000
        emit8(0x48); emit8(0xBF);
        emit8(0x00); emit8(0x80); emit8(0x0B); 
        emit8(0x00); emit8(0x00); emit8(0x00); emit8(0x00);
        // ax = 0x0720 (space + light gray attr)
        emit8(0x66); emit8(0xB8);
        emit8(0x20); emit8(0x07);
        // rcx = 2000 (80*25)
        emit8(0x48); emit8(0xC7); emit8(0xC1);
        emit8(0xD0); emit8(0x07); emit8(0x00); emit8(0x00);
        // rep stosw
        emit8(0x66); emit8(0xF3); emit8(0xAB);
        // Reset cursor at 0x7E00
        emit8(0x48); emit8(0xC7); emit8(0x04); emit8(0x25);
        emit8(0x00); emit8(0x7E); emit8(0x00); emit8(0x00);
        emit8(0x00); emit8(0x00); emit8(0x00); emit8(0x00);
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // vga_putc(char, attr) — write char at cursor
    if (call->name == "vga_putc" && call->args.size() == 2) {
        // AH = attr, AL = char (or use RDI, RSI)
        // rdi = char, rsi = attr
        emit8(0x89); emit8(0xF0); // mov eax, edi (char -> AL)
        emit8(0x40); emit8(0x8A); emit8(0xF2); // mov sil, dl (attr -> AH high)
        emit8(0x88); emit8(0xC4); // mov ah, al (AH = char, we'll fix this)
        // Actually: AL=char, AH=attr
        emit8(0x89); emit8(0xF0); // mov eax, edi
        emit8(0xC1); emit8(0xE0); emit8(0x08); // shl eax, 8
        emit8(0x40); emit8(0x8A); emit8(0xF2); // mov sil, dl
        emit8(0x88); emit8(0xE0); // mov al, ah
        // Now: AL=char, AH=attr
        // rdi = 0xB8000
        emit8(0x48); emit8(0xBF);
        emit8(0x00); emit8(0x80); emit8(0x0B); 
        emit8(0x00); emit8(0x00); emit8(0x00); emit8(0x00);
        // rcx = cursor position at 0x7E00
        emit8(0x48); emit8(0x8B); emit8(0x0C); emit8(0x25);
        emit8(0x00); emit8(0x7E); emit8(0x00); emit8(0x00);
        // rdi += rcx * 2
        emit8(0x48); emit8(0x01); emit8(0xC9);
        emit8(0x48); emit8(0x01); emit8(0xCF);
        // mov [rdi], ax
        emit8(0x66); emit8(0x89); emit8(0x07);
        // Increment cursor
        emit8(0x48); emit8(0xFF); emit8(0x04); emit8(0x25);
        emit8(0x00); emit8(0x7E); emit8(0x00); emit8(0x00);
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // vga_print(ptr) — print null-terminated string
    if (call->name == "vga_print" && call->args.size() == 1) {
        // rsi = string pointer
        // rdi = 0xB8000 + cursor*2
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
        // movzx eax, byte [rsi]
        emit8(0x0F); emit8(0xB6); emit8(0x06);
        // test eax, eax
        emit8(0x85); emit8(0xC0);
        emitJcc("e", loopEnd);
        // mov ah, 0x07 (attr)
        emit8(0xB4); emit8(0x07);
        // mov [rdi], ax
        emit8(0x66); emit8(0x89); emit8(0x07);
        // rsi++
        emit8(0x48); emit8(0xFF); emit8(0xC6);
        // rdi += 2
        emit8(0x48); emit8(0x83); emit8(0xC7); emit8(0x02);
        emitJmp(loopStart);
        
        emitLabel(loopEnd);
        // Update cursor
        emit8(0x48); emit8(0x89); emit8(0xF8); // mov rax, rdi
        emit8(0x48); emit8(0x2D);
        emit8(0x00); emit8(0x80); emit8(0x0B); 
        emit8(0x00); emit8(0x00); emit8(0x00); emit8(0x00);
        emit8(0x48); emit8(0xD1); emit8(0xE8); // shr rax, 1
        emit8(0x48); emit8(0xA3);
        emit8(0x00); emit8(0x7E); emit8(0x00); emit8(0x00);
        emit8(0x00); emit8(0x00); emit8(0x00); emit8(0x00);
        
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // =====================================================================
    // GOP Functions (UEFI Graphics Output Protocol mode)
    // Framebuffer info at fixed addresses:
    //   0x8000 = Framebuffer Address (64-bit)
    //   0x8008 = Pitch (bytes per scanline)
    //   0x800C = Width
    //   0x8010 = Height
    //   0x8014 = BPP
    //   0x8018 = Pixel Format
    // =====================================================================

    // gop_init() — check if GOP is available, returns 1 if available
    if (call->name == "gop_init" && call->args.size() == 0) {
        emit8(0x48); emit8(0x8B); emit8(0x04); emit8(0x25);
        emit8(0x00); emit8(0x80); emit8(0x00); emit8(0x00);
        emit8(0x48); emit8(0x85); emit8(0xC0); // test rax, rax
        emit8(0x0F); emit8(0x95); emit8(0xC0); // setnz al
        emit8(0x48); emit8(0x0F); emit8(0xB6); emit8(0xC0); // movzx rax, al
        resultReg = 0;
        return true;
    }

    // gop_clear(color) — clear screen with color (0x00RRGGBB)
    if (call->name == "gop_clear" && call->args.size() == 1) {
        // eax = color (will be 0x00RRGGBB)
        emit8(0x89); emit8(0xC7); // mov edi, eax (color)
        // rax = framebuffer address
        emit8(0x48); emit8(0x8B); emit8(0x04); emit8(0x25);
        emit8(0x00); emit8(0x80); emit8(0x00); emit8(0x00);
        // rcx = width
        emit8(0x48); emit8(0x8B); emit8(0x0C); emit8(0x25);
        emit8(0x0C); emit8(0x80); emit8(0x00); emit8(0x00);
        // rcx *= height
        emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0x0C); emit8(0x25);
        emit8(0x10); emit8(0x80); emit8(0x00); emit8(0x00);
        // rep stosd
        emit8(0xF3); emit8(0xAB);
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // gop_pixel(x, y, color) — draw pixel
    if (call->name == "gop_pixel" && call->args.size() == 3) {
        // rdi=x, rsi=y, edx=color
        // Calculate: addr = framebuffer + y * pitch + x * 4
        // Save color to stack
        emit8(0x50); // push rax (color in eax/edx)
        // rax = framebuffer
        emit8(0x48); emit8(0x8B); emit8(0x04); emit8(0x25);
        emit8(0x00); emit8(0x80); emit8(0x00); emit8(0x00);
        // rbx = framebuffer (save)
        emit8(0x48); emit8(0x89); emit8(0xC3);
        // rcx = pitch
        emit8(0x48); emit8(0x8B); emit8(0x0C); emit8(0x25);
        emit8(0x08); emit8(0x80); emit8(0x00); emit8(0x00);
        // rcx *= y
        emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xDE); // imul rcx, rsi
        // rax += rcx
        emit8(0x48); emit8(0x01); emit8(0xC8);
        // rax += x * 4 (use lea)
        emit8(0x48); emit8(0x8D); emit8(0x04); emit8(0x87); // lea rax, [rdi*4 + rax]
        // Restore color and write
        emit8(0x58); // pop rax
        emit8(0x89); emit8(0x00); // mov [rax], eax
        emitMovRegImm(1, 0);
        resultReg = 0;
        return true;
    }

    // gop_rect(x, y, w, h, color) — draw filled rectangle
    if (call->name == "gop_rect" && call->args.size() == 5) {
        // rdi=x, rsi=y, rdx=w, rcx=h, r8=color
        // Stack: save w, color
        emit8(0x51); // push rcx (h)
        emit8(0x52); // push rdx (w)
        emit8(0x41); emit8(0x50); // push r8 (color)
        
        // rax = framebuffer + y * pitch
        emit8(0x48); emit8(0x8B); emit8(0x04); emit8(0x25);
        emit8(0x00); emit8(0x80); emit8(0x00); emit8(0x00);
        emit8(0x48); emit8(0x8B); emit8(0x0C); emit8(0x25);
        emit8(0x08); emit8(0x80); emit8(0x00); emit8(0x00);
        emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xDE); // imul rcx, rsi
        emit8(0x48); emit8(0x01); emit8(0xC8);
        // rax += x * 4
        emit8(0x48); emit8(0x8D); emit8(0x04); emit8(0x87);
        // rbx = row pointer
        emit8(0x48); emit8(0x89); emit8(0xC3);
        // rcx = pitch (for next row calculation)
        emit8(0x48); emit8(0x8B); emit8(0x0C); emit8(0x25);
        emit8(0x08); emit8(0x80); emit8(0x00); emit8(0x00);
        // r9 = w (loop counter)
        emit8(0x41); emit8(0x58); // pop r9 (color)
        emit8(0x5A); // pop r8 (w)
        emit8(0x59); // pop r10 (h)
        
        // Row loop
        int rowStart = newLabel();
        int rowEnd = newLabel();
        // rsi = row counter = 0
        emit8(0x31); emit8(0xF6); // xor esi, esi
        
        emitLabel(rowStart);
        emit8(0x49); emit8(0x39); emit8(0xF2); // cmp r10, rsi
        emitJcc("e", rowEnd);
        
        // Col loop
        int colStart = newLabel();
        int colEnd = newLabel();
        emit8(0x31); emit8(0xFF); // xor edi, edi (x counter)
        
        emitLabel(colStart);
        emit8(0x4C); emit8(0x39); emit8(0xC7); // cmp rdi, r8
        emitJcc("e", colEnd);
        
        // Draw pixel: [rbx + rdi*4] = r9
        emit8(0x4C); emit8(0x89); emit8(0x7C); emit8(0xFB); emit8(0x04); // mov [r11+rbx*4], rdi (debug)
        emit8(0x4D); emit8(0x89); emit8(0x04); emit8(0x83); // mov [rbx + rax*4], r9
        emit8(0x48); emit8(0xFF); emit8(0xC7); // inc rdi
        emitJmp(colStart);
        
        emitLabel(colEnd);
        // Next row: rbx += pitch
        emit8(0x48); emit8(0x01); emit8(0xD9);
        emit8(0x48); emit8(0xFF); emit8(0xC6); // inc rsi
        emitJmp(rowStart);
        
        emitLabel(rowEnd);
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // gop_char(x, y, char, fg, bg) — draw 8x16 character
    if (call->name == "gop_char" && call->args.size() == 5) {
        // rdi=x, rsi=y, rdx=char, rcx=fg, r8=bg
        // Simplified: draw 8x16 rectangle with fg color
        emit8(0x41); emit8(0x89); emit8(0xC8); // mov r8, rcx (fg color)
        emit8(0x49); emit8(0x51); // push r9 (bg)
        
        // rax = framebuffer + y * pitch + x * 4
        emit8(0x48); emit8(0x8B); emit8(0x04); emit8(0x25);
        emit8(0x00); emit8(0x80); emit8(0x00); emit8(0x00);
        emit8(0x48); emit8(0x8B); emit8(0x0C); emit8(0x25);
        emit8(0x08); emit8(0x80); emit8(0x00); emit8(0x00);
        emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xDE);
        emit8(0x48); emit8(0x01); emit8(0xC8);
        emit8(0x4C); emit8(0x8D); emit8(0x04); emit8(0x87);
        
        // rbx = start of char
        emit8(0x48); emit8(0x89); emit8(0xC3);
        // rsi = row counter
        emit8(0x31); emit8(0xF6);
        
        // Draw 16 rows of 8 pixels
        int rowLoop = newLabel();
        int rowEnd = newLabel();
        
        emitLabel(rowLoop);
        emit8(0x48); emit8(0x83); emit8(0xFE); emit8(0x10); // cmp rsi, 16
        emitJcc("e", rowEnd);
        
        // Draw row: 8 pixels
        emit8(0x41); emit8(0x8B); emit8(0xC0); // mov eax, r8 (fg color)
        for (int i = 0; i < 8; i++) {
            emit8(0x89); emit8(0x44); emit8(0x83); emit8(i * 4);
        }
        
        // Next row: add pitch
        emit8(0x48); emit8(0x81); emit8(0xC3);
        emit8(0x00); emit8(0x04); emit8(0x00); emit8(0x00); // add rbx, pitch
        emit8(0x48); emit8(0xFF); emit8(0xC6); // inc rsi
        emitJmp(rowLoop);
        
        emitLabel(rowEnd);
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // =====================================================================
    // Framebuffer Info Functions
    // =====================================================================

    // fb_addr() — get framebuffer address
    if (call->name == "fb_addr" && call->args.size() == 0) {
        emit8(0x48); emit8(0x8B); emit8(0x04); emit8(0x25);
        emit8(0x00); emit8(0x80); emit8(0x00); emit8(0x00);
        resultReg = 0;
        return true;
    }

    // fb_width() — get framebuffer width
    if (call->name == "fb_width" && call->args.size() == 0) {
        emit8(0x48); emit8(0x8B); emit8(0x04); emit8(0x25);
        emit8(0x0C); emit8(0x80); emit8(0x00); emit8(0x00);
        resultReg = 0;
        return true;
    }

    // fb_height() — get framebuffer height
    if (call->name == "fb_height" && call->args.size() == 0) {
        emit8(0x48); emit8(0x8B); emit8(0x04); emit8(0x25);
        emit8(0x10); emit8(0x80); emit8(0x00); emit8(0x00);
        resultReg = 0;
        return true;
    }

    // fb_pitch() — get framebuffer pitch
    if (call->name == "fb_pitch" && call->args.size() == 0) {
        emit8(0x48); emit8(0x8B); emit8(0x04); emit8(0x25);
        emit8(0x08); emit8(0x80); emit8(0x00); emit8(0x00);
        resultReg = 0;
        return true;
    }

    // fb_bpp() — get bits per pixel
    if (call->name == "fb_bpp" && call->args.size() == 0) {
        emit8(0x48); emit8(0x8B); emit8(0x04); emit8(0x25);
        emit8(0x14); emit8(0x80); emit8(0x00); emit8(0x00);
        resultReg = 0;
        return true;
    }

    // =====================================================================
    // Memory Access Functions
    // =====================================================================

    // peek(addr) — read 64-bit value from address
    if (call->name == "peek" && call->args.size() == 1) {
        emit8(0x48); emit8(0x8B); emit8(0x00); // mov rax, [rax]
        resultReg = 0;
        return true;
    }

    // poke(addr, val) — write 64-bit value to address
    if (call->name == "poke" && call->args.size() == 2) {
        emit8(0x49); emit8(0x89); emit8(0x08); // mov [r8], rcx
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // peek32(addr) — read 32-bit value
    if (call->name == "peek32" && call->args.size() == 1) {
        emit8(0x8B); emit8(0x00); // mov eax, [rax]
        resultReg = 0;
        return true;
    }

    // poke32(addr, val) — write 32-bit value
    if (call->name == "poke32" && call->args.size() == 2) {
        emit8(0x67); emit8(0x89); emit8(0x08); // mov [eax], ecx (using si addressing)
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // Not recognized
    return false;
}
