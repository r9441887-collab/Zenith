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
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        int portReg = emitExpr(call->args[0].get());
        if (portReg != 2) { emitMovReg(2, portReg); freeReg(portReg); }
        else freeReg(2);
        emit8(0xEC); // in al, dx
        emit8(0x48); emit8(0x0F); emit8(0xB6); emit8(0xC0); // movzx rax, al
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // ======================== inw(port) ========================
    if (call->name == "inw" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        int portReg = emitExpr(call->args[0].get());
        if (portReg != 2) { emitMovReg(2, portReg); freeReg(portReg); }
        else freeReg(2);
        emit8(0x66); emit8(0xED); // in ax, dx
        emit8(0x0F); emit8(0xB7); emit8(0xC0); // movzx eax, ax
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // ======================== ind(port) ========================
    if (call->name == "ind" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        int portReg = emitExpr(call->args[0].get());
        if (portReg != 2) { emitMovReg(2, portReg); freeReg(portReg); }
        else freeReg(2);
        emit8(0xED); // in eax, dx
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // ======================== outb(port, val) ========================
    if (call->name == "outb" && call->args.size() == 2) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        int valReg = emitExpr(call->args[1].get());
        if (valReg != 0) { emitMovReg(0, valReg); freeReg(valReg); }
        else freeReg(0);
        emit8(0x50);  // push rax (val)
        int portReg = emitExpr(call->args[0].get());
        if (portReg != 2) { emitMovReg(2, portReg); freeReg(portReg); }
        else freeReg(2);
        emit8(0x58);  // pop rax
        emit8(0xEE); // out dx, al
        emitMovRegImm(0, 0);
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // ======================== outw(port, val) ========================
    if (call->name == "outw" && call->args.size() == 2) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        int valReg = emitExpr(call->args[1].get());
        if (valReg != 0) { emitMovReg(0, valReg); freeReg(valReg); }
        else freeReg(0);
        emit8(0x50);  // push rax (val)
        int portReg = emitExpr(call->args[0].get());
        if (portReg != 2) { emitMovReg(2, portReg); freeReg(portReg); }
        else freeReg(2);
        emit8(0x58);  // pop rax
        emit8(0x66); emit8(0xEF); // out dx, ax
        emitMovRegImm(0, 0);
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // ======================== outd(port, val) ========================
    if (call->name == "outd" && call->args.size() == 2) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        int valReg = emitExpr(call->args[1].get());
        if (valReg != 0) { emitMovReg(0, valReg); freeReg(valReg); }
        else freeReg(0);
        emit8(0x50);  // push rax (val)
        int portReg = emitExpr(call->args[0].get());
        if (portReg != 2) { emitMovReg(2, portReg); freeReg(portReg); }
        else freeReg(2);
        emit8(0x58);  // pop rax
        emit8(0xEF); // out dx, eax
        emitMovRegImm(0, 0);
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // ======================== lidt(ptr) ========================
    if (call->name == "lidt" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        int ptrReg = emitExpr(call->args[0].get());
        if (ptrReg != 0) { emitMovReg(0, ptrReg); freeReg(ptrReg); }
        else freeReg(0);
        emit8(0x0F); emit8(0x01); emit8(0x18); // lidt [rax]
        emitMovRegImm(0, 0);
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // ======================== lgdt(ptr) ========================
    if (call->name == "lgdt" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        int ptrReg = emitExpr(call->args[0].get());
        if (ptrReg != 0) { emitMovReg(0, ptrReg); freeReg(ptrReg); }
        else freeReg(0);
        emit8(0x0F); emit8(0x01); emit8(0x10); // lgdt [rax]
        emitMovRegImm(0, 0);
        regsUsed = 1;
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
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        int charReg = emitExpr(call->args[0].get());
        if (charReg != 0) { emitMovReg(0, charReg); freeReg(charReg); }
        else freeReg(0);
        emit8(0x50);  // push rax (char)
        int attrReg = emitExpr(call->args[1].get());
        if (attrReg != 0) { emitMovReg(0, attrReg); freeReg(attrReg); }
        else freeReg(0);
        emit8(0x50);  // push rax (attr)
        emit8(0x5B);  // pop rbx (attr)
        emit8(0x58);  // pop rax (char)
        emit8(0x88); emit8(0xE3); // mov ah, bl -> AL=char, AH=attr
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
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // vga_print(ptr) — print null-terminated string
    if (call->name == "vga_print" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        int ptrReg = emitExpr(call->args[0].get());
        if (ptrReg != 0) { emitMovReg(0, ptrReg); freeReg(ptrReg); }
        else freeReg(0);
        emit8(0x48); emit8(0x89); emit8(0xC6); // mov rsi, rax (string pointer)
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
        regsUsed = 1;
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

    // gop_clear(color) — fill the whole screen with color (0x00RRGGBB)
    if (call->name == "gop_clear" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        int colorReg = emitExpr(call->args[0].get());
        if (colorReg != 0) { emitMovReg(0, colorReg); freeReg(colorReg); }
        else freeReg(0);
        // rax = color; rdi = framebuffer; ecx = width*height; rep stosd
        emit8(0x48); emit8(0x8B); emit8(0x3C); emit8(0x25);
        emit8(0x00); emit8(0x80); emit8(0x00); emit8(0x00);   // mov rdi, [0x8000]
        emit8(0x8B); emit8(0x0C); emit8(0x25);
        emit8(0x0C); emit8(0x80); emit8(0x00); emit8(0x00);   // mov ecx, [0x800C] (width)
        emit8(0x0F); emit8(0xAF); emit8(0x0C); emit8(0x25);
        emit8(0x10); emit8(0x80); emit8(0x00); emit8(0x00);   // imul ecx, [0x8010] (height)
        emit8(0xF3); emit8(0xAB);                             // rep stosd
        emitMovRegImm(0, 0);
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // gop_pixel(x, y, color) — draw a single pixel
    if (call->name == "gop_pixel" && call->args.size() == 3) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        int xReg = emitExpr(call->args[0].get());
        if (xReg != 0) { emitMovReg(0, xReg); freeReg(xReg); }
        else freeReg(0);
        emit8(0x50);  // push rax (x)
        int yReg = emitExpr(call->args[1].get());
        if (yReg != 0) { emitMovReg(0, yReg); freeReg(yReg); }
        else freeReg(0);
        emit8(0x50);  // push rax (y)
        int cReg = emitExpr(call->args[2].get());
        if (cReg != 0) { emitMovReg(0, cReg); freeReg(cReg); }
        else freeReg(0);
        // rax = color; stack: [rsp]=y, [rsp+8]=x
        emit8(0x41); emit8(0x58);  // pop r8 (y)
        emit8(0x41); emit8(0x59);  // pop r9 (x)
        // rdi = framebuffer + y*pitch + x*4
        emit8(0x48); emit8(0x8B); emit8(0x3C); emit8(0x25);
        emit8(0x00); emit8(0x80); emit8(0x00); emit8(0x00);   // mov rdi, [0x8000]
        emit8(0x48); emit8(0x8B); emit8(0x34); emit8(0x25);
        emit8(0x08); emit8(0x80); emit8(0x00); emit8(0x00);   // mov rsi, [0x8008] (pitch)
        emit8(0x49); emit8(0x0F); emit8(0xAF); emit8(0xF0);   // imul rsi, r8 (pitch*y)
        emit8(0x48); emit8(0x01); emit8(0xF7);               // add rdi, rsi
        emit8(0x4A); emit8(0x8D); emit8(0x3C); emit8(0x8F);   // lea rdi, [rdi + r9*4]
        emit8(0x89); emit8(0x07);                             // mov [rdi], eax
        emitMovRegImm(0, 0);
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // gop_rect(x, y, w, h, color) — draw filled rectangle
    if (call->name == "gop_rect" && call->args.size() == 5) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        for (int i = 0; i < 4; i++) {
            int a = emitExpr(call->args[i].get());
            if (a != 0) { emitMovReg(0, a); freeReg(a); }
            else freeReg(0);
            emit8(0x50);  // push x/y/w/h
        }
        int colorReg = emitExpr(call->args[4].get());
        if (colorReg != 0) { emitMovReg(0, colorReg); freeReg(colorReg); }
        else freeReg(0);
        // rax = color; stack: [rsp]=h, [rsp+8]=w, [rsp+16]=y, [rsp+24]=x
        emit8(0x5A);               // pop rdx (h)
        emit8(0x59);               // pop rcx (w)
        emit8(0x41); emit8(0x58);  // pop r8 (y)
        emit8(0x41); emit8(0x59);  // pop r9 (x)
        // rbx = framebuffer
        emit8(0x48); emit8(0x8B); emit8(0x1C); emit8(0x25);
        emit8(0x00); emit8(0x80); emit8(0x00); emit8(0x00);   // mov rbx, [0x8000]
        // rsi = pitch
        emit8(0x48); emit8(0x8B); emit8(0x34); emit8(0x25);
        emit8(0x08); emit8(0x80); emit8(0x00); emit8(0x00);   // mov rsi, [0x8008]
        // rdi = framebuffer + y*pitch + x*4
        emit8(0x49); emit8(0x0F); emit8(0xAF); emit8(0xF0);   // imul rsi, r8 (pitch*y)
        emit8(0x48); emit8(0x01); emit8(0xF7);               // add rdi, rsi
        emit8(0x48); emit8(0x01); emit8(0xDF);               // add rdi, rbx
        emit8(0x4A); emit8(0x8D); emit8(0x3C); emit8(0x8F);   // lea rdi, [rdi + r9*4]
        // r9 = row counter (0)
        emit8(0x45); emit8(0x31); emit8(0xC9);               // xor r9d, r9d
        int rowLbl = newLabel();
        int rowEndLbl = newLabel();
        emitLabel(rowLbl);
        emit8(0x4C); emit8(0x39); emit8(0xCA);               // cmp r9, rdx (h)
        emitJcc("e", rowEndLbl);
        // r8 = col counter (0)
        emit8(0x45); emit8(0x31); emit8(0xC0);               // xor r8d, r8d
        int colLbl = newLabel();
        int colEndLbl = newLabel();
        emitLabel(colLbl);
        emit8(0x4C); emit8(0x39); emit8(0xC1);               // cmp r8, rcx (w)
        emitJcc("e", colEndLbl);
        emit8(0x42); emit8(0x89); emit8(0x04); emit8(0x87);   // mov [rdi + r8*4], eax
        emit8(0x49); emit8(0xFF); emit8(0xC0);               // inc r8
        emitJmp(colLbl);
        emitLabel(colEndLbl);
        emit8(0x48); emit8(0x01); emit8(0xF7);               // add rdi, rsi (next row)
        emit8(0x49); emit8(0xFF); emit8(0xC1);               // inc r9
        emitJmp(rowLbl);
        emitLabel(rowEndLbl);
        emitMovRegImm(0, 0);
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // gop_char(x, y, ch, fg, bg) — draw 8x16 block (simplified glyph)
    if (call->name == "gop_char" && call->args.size() == 5) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        for (int i = 0; i < 4; i++) {
            int a = emitExpr(call->args[i].get());
            if (a != 0) { emitMovReg(0, a); freeReg(a); }
            else freeReg(0);
            emit8(0x50);  // push x/y/ch/fg
        }
        int bgReg = emitExpr(call->args[4].get());
        if (bgReg != 0) { emitMovReg(0, bgReg); freeReg(bgReg); }
        else freeReg(0);
        // rax = bg; stack: [rsp]=fg, [rsp+8]=ch, [rsp+16]=y, [rsp+24]=x
        emit8(0x41); emit8(0x58);  // pop r8 (fg)
        emit8(0x41); emit8(0x59);  // pop r9 (ch)
        emit8(0x41); emit8(0x5A);  // pop r10 (y)
        emit8(0x41); emit8(0x5B);  // pop r11 (x)
        // rbx = framebuffer
        emit8(0x48); emit8(0x8B); emit8(0x1C); emit8(0x25);
        emit8(0x00); emit8(0x80); emit8(0x00); emit8(0x00);   // mov rbx, [0x8000]
        // rsi = pitch
        emit8(0x48); emit8(0x8B); emit8(0x34); emit8(0x25);
        emit8(0x08); emit8(0x80); emit8(0x00); emit8(0x00);   // mov rsi, [0x8008]
        // rdi = framebuffer + y*pitch + x*4
        emit8(0x49); emit8(0x0F); emit8(0xAF); emit8(0xF2);   // imul rsi, r10 (pitch*y)
        emit8(0x48); emit8(0x01); emit8(0xDF);               // add rdi, rbx
        emit8(0x48); emit8(0x01); emit8(0xF7);               // add rdi, rsi
        emit8(0x4A); emit8(0x8D); emit8(0x3C); emit8(0x9F);   // lea rdi, [rdi + r11*4]
        // r9 = row counter (0), r10 = col counter
        emit8(0x45); emit8(0x31); emit8(0xC9);               // xor r9d, r9d
        int rowLbl = newLabel();
        int rowEndLbl = newLabel();
        emitLabel(rowLbl);
        emit8(0x49); emit8(0x83); emit8(0xF9); emit8(0x10);   // cmp r9, 16
        emitJcc("e", rowEndLbl);
        emit8(0x45); emit8(0x31); emit8(0xD2);               // xor r10d, r10d
        int colLbl = newLabel();
        int colEndLbl = newLabel();
        emitLabel(colLbl);
        emit8(0x49); emit8(0x83); emit8(0xFA); emit8(0x08);   // cmp r10, 8
        emitJcc("e", colEndLbl);
        emit8(0x46); emit8(0x89); emit8(0x04); emit8(0x97);   // mov [rdi + r10*4], r8d
        emit8(0x49); emit8(0xFF); emit8(0xC2);               // inc r10
        emitJmp(colLbl);
        emitLabel(colEndLbl);
        emit8(0x48); emit8(0x01); emit8(0xF7);               // add rdi, rsi (next row)
        emit8(0x49); emit8(0xFF); emit8(0xC1);               // inc r9
        emitJmp(rowLbl);
        emitLabel(rowEndLbl);
        emitMovRegImm(0, 0);
        regsUsed = 1;
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
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        int addrReg = emitExpr(call->args[0].get());
        if (addrReg != 0) { emitMovReg(0, addrReg); freeReg(addrReg); }
        else freeReg(0);
        emit8(0x8B); emit8(0x00); // mov eax, [rax]
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // poke32(addr, val) — write 32-bit value
    if (call->name == "poke32" && call->args.size() == 2) {
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
        emit8(0x5A);  // pop rdx (val)
        emit8(0x89); emit8(0x10); // mov [rax], edx
        emitMovRegImm(0, 0);
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // Not recognized
    return false;
}
