// codegen_efi.cpp — EFI and Bare-metal built-in functions
// Handles: cli, sti, hlt, int(n), inb, inw, ind, outb, outw, outd
//          halt, lidt, lgdt (bare/efi)
//          vga_clear, vga_print, vga_putc (bare only)
//
// Without this file these names are parsed as CallExpr but the compiler
// tries to emit a regular function call → "undefined function" error.
// This file generates raw x86-64 instructions directly.

#include "codegen.h"
#include "ast.h"
#include "parser.h"
#include <iostream>
#include <vector>

using namespace std;

bool Codegen::tryEFICall(CallExpr* call, int& resultReg) {

    // ======================== cli() ========================
    if (call->name == "cli" && call->args.size() == 0) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        emit8(0xFA); // cli
        regsUsed = (uint8_t)saved;
        reloadRegs();
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== sti() ========================
    if (call->name == "sti" && call->args.size() == 0) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        emit8(0xFB); // sti
        regsUsed = (uint8_t)saved;
        reloadRegs();
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== hlt() ========================
    if (call->name == "hlt" && call->args.size() == 0) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        emit8(0xF4); // hlt
        regsUsed = (uint8_t)saved;
        reloadRegs();
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== halt() ========================
    // Bare-metal: infinite hlt loop (CPU sleeps until reset)
    if (call->name == "halt" && call->args.size() == 0) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        int loopTop = newLabel();
        emitLabel(loopTop);
        emit8(0xF4);       // hlt
        emitJmp(loopTop);  // jmp back (infinite loop)
        regsUsed = (uint8_t)saved;
        reloadRegs();
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== int(n) ========================
    // Software interrupt: INT imm8
    if (call->name == "int" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        if (auto num = dynamic_cast<NumberExpr*>(call->args[0].get())) {
            // Constant: emit INT imm8 directly (0xCD <imm8>)
            emit8(0xCD);
            emit8((uint8_t)(num->value & 0xFF));
        } else {
            // Dynamic: evaluate expr, then use the byte in al
            // INT only takes an immediate; for dynamic we build
            // a self-modifying code sequence
            int nReg = emitExpr(call->args[0].get());
            if (nReg != 0) { emitMovReg(0, nReg); freeReg(nReg); }
            // mov byte [rip+patch], al
            emit8(0x88); emit8(0x05); emit32(2); // mov [rip+2], al
            emit8(0xCD); // INT imm8 (patched)
            emit8(0x00); // placeholder imm8 (will be patched at runtime)
            freeReg(0);
        }

        regsUsed = (uint8_t)saved;
        reloadRegs();
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== inb(port) -> int ========================
    // Read byte from I/O port. Port in DX, result in AL → zero-extended to RAX.
    if (call->name == "inb" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        int pReg = emitExpr(call->args[0].get());
        if (pReg != 2) { emitMovReg(2, pReg); freeReg(pReg); }
        // Port is in rdx → DX has port number
        // in al, dx → 0xEC
        emit8(0xEC);
        // Zero-extend AL to RAX: movzx rax, al
        emit8(0x48); emit8(0x0F); emit8(0xB6); emit8(0xC0);
        freeReg(2);

        regsUsed = 1; // rax has result
        int r = allocReg();
        if (r != 0) { emitMovReg(r, 0); freeReg(0); }
        resultReg = r >= 0 ? r : 0;
        return true;
    }

    // ======================== inw(port) -> int ========================
    // Read word (16-bit) from I/O port
    if (call->name == "inw" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        int pReg = emitExpr(call->args[0].get());
        if (pReg != 2) { emitMovReg(2, pReg); freeReg(pReg); }
        // in ax, dx → 0x66 0xED
        emit8(0x66); emit8(0xED);
        // Zero-extend AX to EAX: movzx eax, ax
        emit8(0x0F); emit8(0xB7); emit8(0xC0);
        freeReg(2);

        regsUsed = 1;
        int r = allocReg();
        if (r != 0) { emitMovReg(r, 0); freeReg(0); }
        resultReg = r >= 0 ? r : 0;
        return true;
    }

    // ======================== ind(port) -> int ========================
    // Read dword (32-bit) from I/O port
    if (call->name == "ind" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        int pReg = emitExpr(call->args[0].get());
        if (pReg != 2) { emitMovReg(2, pReg); freeReg(pReg); }
        // in eax, dx → 0xED
        emit8(0xED);
        // EAX auto-zero-extends to RAX
        freeReg(2);

        regsUsed = 1;
        int r = allocReg();
        if (r != 0) { emitMovReg(r, 0); freeReg(0); }
        resultReg = r >= 0 ? r : 0;
        return true;
    }

    // ======================== outb(port, val) ========================
    // Write byte to I/O port. Port in DX, value in AL.
    if (call->name == "outb" && call->args.size() == 2) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        // Evaluate port → rdx
        int pReg = emitExpr(call->args[0].get());
        if (pReg != 2) { emitMovReg(2, pReg); freeReg(pReg); }
        // Save port on stack
        emit8(0x52); // push rdx

        // Evaluate val → rax
        int vReg = emitExpr(call->args[1].get());
        if (vReg != 0) { emitMovReg(0, vReg); freeReg(vReg); }

        // Restore port to rdx
        emit8(0x5A); // pop rdx
        // out dx, al → 0xEE (port in DX, byte in AL)
        emit8(0xEE);

        freeReg(0); freeReg(2);
        regsUsed = (uint8_t)saved;
        reloadRegs();
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== outw(port, val) ========================
    // Write word (16-bit) to I/O port. Port in DX, value in AX.
    if (call->name == "outw" && call->args.size() == 2) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        int pReg = emitExpr(call->args[0].get());
        if (pReg != 2) { emitMovReg(2, pReg); freeReg(pReg); }
        emit8(0x52); // push rdx

        int vReg = emitExpr(call->args[1].get());
        if (vReg != 0) { emitMovReg(0, vReg); freeReg(vReg); }

        emit8(0x5A); // pop rdx
        // out dx, ax → 0x66 0xEF
        emit8(0x66); emit8(0xEF);

        freeReg(0); freeReg(2);
        regsUsed = (uint8_t)saved;
        reloadRegs();
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== outd(port, val) ========================
    // Write dword (32-bit) to I/O port. Port in DX, value in EAX.
    if (call->name == "outd" && call->args.size() == 2) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        int pReg = emitExpr(call->args[0].get());
        if (pReg != 2) { emitMovReg(2, pReg); freeReg(pReg); }
        emit8(0x52); // push rdx

        int vReg = emitExpr(call->args[1].get());
        if (vReg != 0) { emitMovReg(0, vReg); freeReg(vReg); }

        emit8(0x5A); // pop rdx
        // out dx, eax → 0xEF
        emit8(0xEF);

        freeReg(0); freeReg(2);
        regsUsed = (uint8_t)saved;
        reloadRegs();
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== lidt(ptr) ========================
    if (call->name == "lidt" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        int pReg = emitExpr(call->args[0].get());
        if (pReg != 0) { emitMovReg(0, pReg); freeReg(pReg); }
        // lidt [rax]: 0F 01 /3  → ModRM byte = 0x18
        emit8(0x0F); emit8(0x01); emit8(0x18);

        freeReg(0);
        regsUsed = (uint8_t)saved;
        reloadRegs();
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== lgdt(ptr) ========================
    if (call->name == "lgdt" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        int pReg = emitExpr(call->args[0].get());
        if (pReg != 0) { emitMovReg(0, pReg); freeReg(pReg); }
        // lgdt [rax]: 0F 01 /2  → ModRM byte = 0x10
        emit8(0x0F); emit8(0x01); emit8(0x10);

        freeReg(0);
        regsUsed = (uint8_t)saved;
        reloadRegs();
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ======================== VGA functions (Bare mode) ========================

    // vga_clear() — fill VGA text buffer (0xB8000) with spaces
    if (call->name == "vga_clear" && call->args.size() == 0) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        // rdi = 0xB8000
        emit8(0x48); emit8(0xBF);
        emit8(0x00); emit8(0x80); emit8(0x0B); emit8(0x00);
        emit8(0x00); emit8(0x00); emit8(0x00); emit8(0x00); // movabs rdi, 0xB8000

        // ax = 0x0720 (space 0x20 + attr 0x07 = light gray on black)
        emit8(0x66); emit8(0xB8);
        emit8(0x20); emit8(0x07); // mov ax, 0x0720

        // rcx = 2000 (80*25 characters)
        emit8(0x48); emit8(0xC7); emit8(0xC1);
        emit8(0xD0); emit8(0x07); emit8(0x00); emit8(0x00); // mov rcx, 2000

        // rep stosw: 0x66 0xF3 0xAB
        emit8(0x66); emit8(0xF3); emit8(0xAB);

        // Also reset cursor position stored at 0x7E00
        emit8(0x48); emit8(0xC7); emit8(0x04); emit8(0x25);
        emit8(0x00); emit8(0x7E); emit8(0x00); emit8(0x00);
        emit8(0x00); emit8(0x00); emit8(0x00); emit8(0x00); // mov qword [0x7E00], 0

        freeReg(0); freeReg(1); freeReg(2); freeReg(3);
        regsUsed = (uint8_t)saved;
        reloadRegs();
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // vga_putc(char, attr) — write one char with attribute at cursor position
    // Cursor position tracked at memory address 0x7E00 (just past boot sector)
    if (call->name == "vga_putc" && call->args.size() == 2) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        // Get char
        int cReg = emitExpr(call->args[0].get());
        if (cReg != 0) { emitMovReg(0, cReg); freeReg(cReg); }
        emit8(0x50); // push rax (save char)

        // Get attr
        int aReg = emitExpr(call->args[1].get());
        if (aReg != 0) { emitMovReg(0, aReg); freeReg(aReg); }
        // Move attr to AH: shl rax, 8
        emit8(0x48); emit8(0xC1); emit8(0xE0); emit8(0x08);
        // rax = attr << 8

        // Get char from stack into AL
        emit8(0x59); // pop rcx (char)
        emit8(0x08); emit8(0xC8); // or al, cl → rax = (attr << 8) | char

        // rdi = 0xB8000
        emit8(0x48); emit8(0xBF);
        emit8(0x00); emit8(0x80); emit8(0x0B); emit8(0x00);
        emit8(0x00); emit8(0x00); emit8(0x00); emit8(0x00); // movabs rdi, 0xB8000

        // rcx = [0x7E00] (cursor position)
        emit8(0x48); emit8(0x8B); emit8(0x0C); emit8(0x25);
        emit8(0x00); emit8(0x7E); emit8(0x00); emit8(0x00); // mov rcx, [0x7E00]

        // rdi += rcx * 2
        emit8(0x48); emit8(0x01); emit8(0xC9); // add rcx, rcx
        emit8(0x48); emit8(0x01); emit8(0xCF); // add rdi, rcx

        // Store ax at [rdi]
        emit8(0x66); emit8(0x89); emit8(0x07); // mov [rdi], ax

        // Increment cursor: [0x7E00] += 1
        emit8(0x48); emit8(0xFF); emit8(0x04); emit8(0x25);
        emit8(0x00); emit8(0x7E); emit8(0x00); emit8(0x00); // inc qword [0x7E00]

        freeReg(0); freeReg(1); freeReg(2); freeReg(3);
        regsUsed = (uint8_t)saved;
        reloadRegs();
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // vga_print(text) — print null-terminated string to VGA at cursor
    if (call->name == "vga_print" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        // Get string pointer
        int sReg = emitExpr(call->args[0].get());
        if (sReg != 1) { emitMovReg(1, sReg); freeReg(sReg); }
        // rcx = string pointer
        emit8(0x51); // push rcx

        // rdi = 0xB8000
        emit8(0x48); emit8(0xBF);
        emit8(0x00); emit8(0x80); emit8(0x0B); emit8(0x00);
        emit8(0x00); emit8(0x00); emit8(0x00); emit8(0x00); // movabs rdi, 0xB8000

        // r8 = [0x7E00] (cursor position)
        emit8(0x4C); emit8(0x8B); emit8(0x04); emit8(0x25);
        emit8(0x00); emit8(0x7E); emit8(0x00); emit8(0x00); // mov r8, [0x7E00]

        // rdi += r8 * 2
        emit8(0x4C); emit8(0x01); emit8(0xC7); // add rdi, r8
        emit8(0x48); emit8(0x01); emit8(0xC7); // add rdi, r8

        // Pop string pointer into rsi
        emit8(0x5E); // pop rsi

        // Loop: read byte from [rsi], if 0 → end, else write to [rdi] and advance
        int loopLabel = newLabel();
        int endLabel = newLabel();

        emitLabel(loopLabel);

        // movzx eax, byte [rsi]
        emit8(0x0F); emit8(0xB6); emit8(0x06);

        // test eax, eax
        emit8(0x85); emit8(0xC0);

        // je endLabel
        emit8(0x0F); emit8(0x84);
        jmpFixups.push_back({code.size(), endLabel}); emit32(0);

        // mov ah, 0x07 (attr: light gray on black)
        emit8(0xB4); emit8(0x07);

        // mov [rdi], ax
        emit8(0x66); emit8(0x89); emit8(0x07);

        // rsi++
        emit8(0x48); emit8(0xFF); emit8(0xC6); // inc rsi

        // rdi += 2
        emit8(0x48); emit8(0x83); emit8(0xC7); emit8(0x02); // add rdi, 2

        // jmp loopLabel
        emitJmp(loopLabel);

        emitLabel(endLabel);

        // Update cursor: [0x7E00] = (rdi - 0xB8000) / 2
        emit8(0x48); emit8(0x89); emit8(0xF8); // mov rax, rdi
        // sub rax, 0xB8000
        emit8(0x48); emit8(0x2D);
        emit8(0x00); emit8(0x80); emit8(0x0B); emit8(0x00);
        emit8(0x00); emit8(0x00); emit8(0x00); emit8(0x00); // sub rax, 0xB8000
        // shr rax, 1
        emit8(0x48); emit8(0xD1); emit8(0xE8);
        // mov [0x7E00], rax
        emit8(0x48); emit8(0xA3);
        emit8(0x00); emit8(0x7E); emit8(0x00); emit8(0x00);
        emit8(0x00); emit8(0x00); emit8(0x00); emit8(0x00); // mov [0x7E00], rax

        freeReg(0); freeReg(1); freeReg(2); freeReg(3);
        regsUsed = (uint8_t)saved;
        reloadRegs();
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // Not a recognized EFI/bare builtin
    return false;
}
