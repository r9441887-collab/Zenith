#include "codegen.h"
#include "ast.h"
#include "parser.h"
#include <iostream>
#include <cstring>
#include <algorithm>

using namespace std;

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
    emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x28);  // sub rsp, 0x28 (shadow + alignment)
    emit8(0x33); emit8(0xC9);  // xor ecx, ecx
    emit8(0xFF); emit8(0x15);
    importCallFixups.push_back({code.size(), "PostQuitMessage", "user32.dll"});
    emit32(0);
    emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x28);  // add rsp, 0x28
    emit8(0x33); emit8(0xC0);  // xor eax, eax
    emit8(0xC3);  // ret

    // .default: call DefWindowProcA
    int defaultPos = (int)code.size();
    code[jnePos] = (uint8_t)(defaultPos - jnePos - 1);

    emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x28);  // sub rsp, 0x28 (shadow + alignment)
    emit8(0xFF); emit8(0x15);
    importCallFixups.push_back({code.size(), "DefWindowProcA", "user32.dll"});
    emit32(0);
    emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x28);  // add rsp, 0x28
    emit8(0xC3);  // ret
}

bool Codegen::tryGUICall(CallExpr* call, int& resultReg) {
    // --- drawPixel(x, y, color) ---
    if (call->name == "drawPixel" && call->args.size() == 3) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
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

        // If framebuf is NULL, skip draw
        emit8(0x4D); emit8(0x85); emit8(0xDB);  // test r11, r11
        int skipDrawPixel = newLabel();
        emitJcc("==", skipDrawPixel);

        // Compute address: framebuf + (y * width + x) * 4
        emit8(0x4C); emit8(0x89); emit8(0xC8);  // mov rax, r9 (y)
        emit8(0x49); emit8(0x0F); emit8(0xAF); emit8(0xC4);  // imul rax, r12 (width)
        emit8(0x4C); emit8(0x01); emit8(0xC0);  // add rax, r8 (x)
        emit8(0x48); emit8(0xC1); emit8(0xE0); emit8(0x02);  // shl rax, 2 (*4)
        emit8(0x4C); emit8(0x01); emit8(0xD8);  // add rax, r11 (framebuffer)

        // Store color
        emit8(0x44); emit8(0x89); emit8(0x10);  // mov [rax], r10d

        emitLabel(skipDrawPixel);

        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // --- clear(color) ---
    if (call->name == "clear" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

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

        // If framebuf is NULL, skip clear
        emit8(0x4D); emit8(0x85); emit8(0xC9);  // test r9, r9
        int skipClear = newLabel();
        emitJcc("==", skipClear);

        // rcx = width * height
        emit8(0x4C); emit8(0x89); emit8(0xC1);  // mov rcx, r8
        emit8(0x41); emit8(0x0F); emit8(0xAF); emit8(0xCB);  // imul rcx, r11

        // Save rdi, set up rep stosd
        emit8(0x57);  // push rdi
        emit8(0x4C); emit8(0x89); emit8(0xCF);  // mov rdi, r9 (framebuf)
        // mov ecx, ecx (zero-extend ecx to rcx, already done)
        emit8(0xF3); emit8(0xAB);  // rep stosd
        emit8(0x5F);  // pop rdi
        emitLabel(skipClear);

        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // --- getKey(vk) ---
    if (call->name == "getKey" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

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
        resultReg = 0;
        return true;
    }

    // --- closeWindow() ---
    if (call->name == "closeWindow" && call->args.empty()) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        // lea rbx, [rip + win32Globals]
        emit8(0x48); emit8(0x8D); emit8(0x1D);
        heapFixups.push_back({code.size(), win32GlobalsRVA});
        emit32(0);

        if (prog.renderType == RenderType::DX11) {
            // Allocate shadow space for all Release calls (5 * 8 < 32, but each needs 32-byte aligned)
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);  // sub rsp, 0x20 (shadow space)

            // Release staging texture: stagingTexture->vtable[2](stagingTexture)
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x58);  // rax = [rbx+88]
            emit8(0x48); emit8(0x85); emit8(0xC0);               // test rax, rax
            int skipStaging = newLabel();
            emit8(0x74); emit8(0x0D);                             // jz skipStaging
            emit8(0x48); emit8(0x8B); emit8(0x00);               // rax = vtable
            emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x10); // rax = Release
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x58); // rcx = stagingTexture
            emit8(0xFF); emit8(0xD0);                            // call rax
            emitLabel(skipStaging);

            // Release RTV: rtv->vtable[2](rtv)
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x50);  // rax = [rbx+80]
            emit8(0x48); emit8(0x85); emit8(0xC0);
            int skipRTV = newLabel();
            emit8(0x74); emit8(0x0D);
            emit8(0x48); emit8(0x8B); emit8(0x00);
            emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x10);
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x50);
            emit8(0xFF); emit8(0xD0);
            emitLabel(skipRTV);

            // Release context: context->vtable[2](context)
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x40);  // rax = [rbx+64]
            emit8(0x48); emit8(0x85); emit8(0xC0);
            int skipCtx = newLabel();
            emit8(0x74); emit8(0x0D);
            emit8(0x48); emit8(0x8B); emit8(0x00);
            emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x10);
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x40);
            emit8(0xFF); emit8(0xD0);
            emitLabel(skipCtx);

            // Release swapChain: swapChain->vtable[2](swapChain)
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x48);  // rax = [rbx+72]
            emit8(0x48); emit8(0x85); emit8(0xC0);
            int skipSC = newLabel();
            emit8(0x74); emit8(0x0D);
            emit8(0x48); emit8(0x8B); emit8(0x00);
            emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x10);
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x48);
            emit8(0xFF); emit8(0xD0);
            emitLabel(skipSC);

            // Release device: device->vtable[2](device)
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x38);  // rax = [rbx+56]
            emit8(0x48); emit8(0x85); emit8(0xC0);
            int skipDev = newLabel();
            emit8(0x74); emit8(0x0D);
            emit8(0x48); emit8(0x8B); emit8(0x00);
            emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x10);
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x38);
            emit8(0xFF); emit8(0xD0);
            emitLabel(skipDev);

            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);  // add rsp, 0x20
        }

        // SR GDI cleanup: DeleteDC(hdcMem) and DeleteObject(hBitmap)
        if (prog.renderType == RenderType::Software) {
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);  // sub rsp, 0x20

            // DeleteDC([rbx+16])
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x10);  // mov rcx, [rbx+16]
            emit8(0x48); emit8(0x85); emit8(0xC9);               // test rcx, rcx
            emit8(0x74); emit8(0x06);                             // jz skipDC
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "DeleteDC", "gdi32.dll"});
            emit32(0);

            // DeleteObject([rbx+24])
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x18);  // mov rcx, [rbx+24]
            emit8(0x48); emit8(0x85); emit8(0xC9);               // test rcx, rcx
            emit8(0x74); emit8(0x06);                             // jz skipBmp
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "DeleteObject", "gdi32.dll"});
            emit32(0);

            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);  // add rsp, 0x20
        }

        // DestroyWindow(hwnd) - hwnd at globals+8
        emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);
        emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x08);  // mov rcx, [rbx+8] (hwnd)
        emit8(0x48); emit8(0x85); emit8(0xC9);               // test rcx, rcx
        emit8(0x74); emit8(0x0E);                             // jz skipDestroy (skip14: call(6) + mov(8))
        emit8(0xFF); emit8(0x15);
        importCallFixups.push_back({code.size(), "DestroyWindow", "user32.dll"});
        emit32(0);
        emit8(0x48); emit8(0xC7); emit8(0x43); emit8(0x08); emit32(0);  // mov qword [rbx+8], 0
        int skipDestroy = newLabel();
        emitLabel(skipDestroy);
        emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);

        regsUsed = (uint8_t)saved;
        reloadRegs();
        int r = allocReg(); if (r != 0) { emitMovReg(r, 0); freeReg(0); }
        resultReg = r >= 0 ? r : 0;
        return true;
    }

    // --- createWindow(w, h, title) ---
    if (call->name == "createWindow" && call->args.size() == 3) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

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
        jmpFixups.push_back({code.size(), skipAll});
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

        if (prog.renderType == RenderType::DX11) {
            // ========== DX11 Initialization ==========
            // After UpdateWindow: rbx = &globals, rsi = hwnd

            // --- D3D11CreateDeviceAndSwapChain ---
            // Stack frame: 0x120 bytes
            // [rsp+0x00..0x1F]: shadow (32)
            // [rsp+0x20..0x5F]: 8 params for D3D11CreateDeviceAndSwapChain (64)
            // [rsp+0x60..0x7F]: padding (32)
            // [rsp+0x80..0xBF]: DXGI_SWAP_CHAIN_DESC (64)
            // [rsp+0xC0]: swapChain ptr (8)
            // [rsp+0xC8]: device ptr (8)
            // [rsp+0xD0]: context ptr (8)
            // [rsp+0xD8]: featureLevel (4) + pad (4)
            // [rsp+0xE0..0x11F]: extra local space
            emit8(0x48); emit8(0x81); emit8(0xEC); emit32(0x120);

            // Fill DXGI_SWAP_CHAIN_DESC at [rsp+0x80]
            emit8(0x8B); emit8(0x43); emit8(0x20);             // eax = width
            emit8(0x89); emit8(0x84); emit8(0x24); emit32(0x80); // Width
            emit8(0x8B); emit8(0x43); emit8(0x24);             // eax = height
            emit8(0x89); emit8(0x84); emit8(0x24); emit32(0x84); // Height
            // RefreshRate = {0,0}
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0x88); emit32(0);
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0x8C); emit32(0);
            // Format = DXGI_FORMAT_R8G8B8A8_UNORM (28)
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0x90); emit32(28);
            // ScanlineOrdering = 0, Scaling = 0
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0x94); emit32(0);
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0x98); emit32(0);
            // SampleDesc = {1, 0}
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0x9C); emit32(1);
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0xA0); emit32(0);
            // BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT (0x20)
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0xA4); emit32(0x20);
            // BufferCount = 1
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0xA8); emit32(1);
            // Padding 4 bytes
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0xAC); emit32(0);
            // OutputWindow = hwnd (from globals+8)
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x08); // rax = [rbx+8] = hwnd
            emit8(0x48); emit8(0x89); emit8(0x84); emit8(0x24); emit32(0xB0);
            // Windowed = TRUE (1)
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0xB8); emit32(1);
            // SwapEffect = DXGI_SWAP_EFFECT_DISCARD (0)
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0xBC); emit32(0);
            // Flags = 0
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0xC0); emit32(0);
            // Padding
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0xC4); emit32(0);

            // Set up call parameters
            // [rsp+0x20] = pFeatureLevels = NULL
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(0);
            // [rsp+0x28] = FeatureLevels = 0
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x28); emit32(0);
            // [rsp+0x30] = SDKVersion = D3D11_SDK_VERSION (7)
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x30); emit32(7);
            // [rsp+0x38] = &SWAP_CHAIN_DESC
            emit8(0x48); emit8(0x8D); emit8(0x84); emit8(0x24); emit32(0x80);
            emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x38);
            // [rsp+0x40] = &swapChain
            emit8(0x48); emit8(0x8D); emit8(0x84); emit8(0x24); emit32(0xC0);
            emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x40);
            // [rsp+0x48] = &device
            emit8(0x48); emit8(0x8D); emit8(0x84); emit8(0x24); emit32(0xC8);
            emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x48);
            // [rsp+0x50] = &featureLevel
            emit8(0x48); emit8(0x8D); emit8(0x84); emit8(0x24); emit32(0xD8);
            emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x50);
            // [rsp+0x58] = &context
            emit8(0x48); emit8(0x8D); emit8(0x84); emit8(0x24); emit32(0xD0);
            emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x58);

            // rcx = NULL (pAdapter)
            emit8(0x33); emit8(0xC9);                            // xor ecx, ecx
            // edx = D3D_DRIVER_TYPE_HARDWARE (1)
            emit8(0xBA); emit32(1);
            // r8 = NULL (Software)
            emit8(0x45); emit8(0x33); emit8(0xC0);               // xor r8d, r8d
            // r9 = 0 (Flags)
            emit8(0x45); emit8(0x33); emit8(0xC9);               // xor r9d, r9d

            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "D3D11CreateDeviceAndSwapChain", "d3d11.dll"});
            emit32(0);

            // Check HRESULT, skip DX11 init if failed
            int dx11Failed = newLabel();
            emit8(0x85); emit8(0xC0);  // test eax, eax
            emitJcc("<", dx11Failed);  // jl = jump if sign (FAILED)

            // Store device, context, swapChain in globals
            // [globals+56] = device
            emit8(0x48); emit8(0x8B); emit8(0x84); emit8(0x24); emit32(0xC8); // rax = device
            emit8(0x48); emit8(0x89); emit8(0x43); emit8(0x38);               // [rbx+56] = device
            // [globals+64] = context
            emit8(0x48); emit8(0x8B); emit8(0x84); emit8(0x24); emit32(0xD0); // rax = context
            emit8(0x48); emit8(0x89); emit8(0x43); emit8(0x40);               // [rbx+64] = context
            // [globals+72] = swapChain
            emit8(0x48); emit8(0x8B); emit8(0x84); emit8(0x24); emit32(0xC0); // rax = swapChain
            emit8(0x48); emit8(0x89); emit8(0x43); emit8(0x48);               // [rbx+72] = swapChain

            // --- IDXGISwapChain::GetBuffer(swapChain, 0, &IID_ID3D11Texture2D, &backBuffer) ---
            // IID_ID3D11Texture2D = {6F15AAF2-D208-4E89-9AB4-489535D34F9C}
            int iidIdx = -1;
            {
                std::string iidBytes(16, '\0');
                iidBytes[0] = '\xF2'; iidBytes[1] = '\xAA'; iidBytes[2] = '\x15'; iidBytes[3] = '\x6F';
                iidBytes[4] = '\x08'; iidBytes[5] = '\xD2'; iidBytes[6] = '\x89'; iidBytes[7] = '\x4E';
                iidBytes[8] = '\x9A'; iidBytes[9] = '\xB4'; iidBytes[10] = '\x48'; iidBytes[11] = '\x95';
                iidBytes[12] = '\x35'; iidBytes[13] = '\xD3'; iidBytes[14] = '\x4F'; iidBytes[15] = '\x9C';
                for (size_t i = 0; i < stringPool.size(); i++) {
                    if (stringPool[i] == iidBytes) { iidIdx = (int)i; break; }
                }
                if (iidIdx < 0) {
                    iidIdx = (int)stringPool.size();
                    stringPool.push_back(iidBytes);
                }
            }

            // COM call: swapChain->vtable[9](swapChain, 0, &IID, &backBuffer)
            // [rsp+0x80] will store backBuffer
            // Load swapChain from globals+72
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x48);  // rax = [rbx+72] = swapChain
            emit8(0x48); emit8(0x8B); emit8(0x00);                // rax = [rax] = vtable
            emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x48);  // rax = [rax+72] = GetBuffer fn
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x48);  // rcx = [rbx+72] = swapChain (this)
            emit8(0x33); emit8(0xD2);                             // rdx = 0 (buffer index)
            emit8(0x45); emit8(0x33); emit8(0xC0);               // r8d = 0
            // lea r8, [rip+IID] or use heap fixup
            emit8(0x4C); emit8(0x8D); emit8(0x05);
            heapFixups.push_back({code.size(), stringRVA + stringOffsets[iidIdx]});
            emit32(0);
            // r9 = &backBuffer at [rsp+0x80]
            emit8(0x4C); emit8(0x8D); emit8(0x8C); emit8(0x24); emit32(0x80);
            emit8(0xFF); emit8(0xD0);                             // call rax
            // backBuffer is now at [rsp+0x80]

            // --- ID3D11Device::CreateRenderTargetView(device, backBuffer, NULL, &rtv) ---
            // [rsp+0x88] will store rtv
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x38);  // rax = [rbx+56] = device
            emit8(0x48); emit8(0x8B); emit8(0x00);                // rax = vtable
            emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x48);  // rax = [rax+72] = CreateRenderTargetView fn
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x38);  // rcx = device (this)
            emit8(0x48); emit8(0x8B); emit8(0x94); emit8(0x24); emit32(0x80); // rdx = backBuffer
            emit8(0x45); emit8(0x33); emit8(0xC0);               // r8d = 0 (pDesc = NULL)
            emit8(0x4C); emit8(0x8D); emit8(0x8C); emit8(0x24); emit32(0x88); // r9 = &rtv
            emit8(0xFF); emit8(0xD0);                             // call rax
            // rtv is now at [rsp+0x88]
            // Store rtv in globals+80 for cleanup
            emit8(0x48); emit8(0x8B); emit8(0x84); emit8(0x24); emit32(0x88); // rax = [rsp+0x88]
            emit8(0x48); emit8(0x89); emit8(0x43); emit8(0x50);               // [rbx+80] = rtv

            // --- Release backBuffer ---
            // backBuffer->vtable[2](backBuffer) = Release
            emit8(0x48); emit8(0x8B); emit8(0x84); emit8(0x24); emit32(0x80); // rax = backBuffer
            emit8(0x48); emit8(0x85); emit8(0xC0);                          // test rax, rax
            int skipReleaseBB = newLabel();
            emit8(0x74); emit8(0x0E);                                       // jz skipReleaseBB
            emit8(0x48); emit8(0x8B); emit8(0x00);                          // rax = vtable
            emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x10);            // rax = Release
            emit8(0x48); emit8(0x8B); emit8(0x8C); emit8(0x24); emit32(0x80); // rcx = backBuffer
            emit8(0xFF); emit8(0xD0);                                       // call rax
            emitLabel(skipReleaseBB);

            // --- ID3D11DeviceContext::OMSetRenderTargets(context, 1, &rtv, NULL) ---
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x40);  // rax = [rbx+64] = context
            emit8(0x48); emit8(0x8B); emit8(0x00);                // rax = vtable
            emit8(0x48); emit8(0x8B); emit8(0x80); emit32(0x108); // rax = [rax+264] = OMSetRenderTargets
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x40);  // rcx = context (this)
            emit8(0xBA); emit32(1);                               // rdx = 1 (NumViews)
            emit8(0x4C); emit8(0x8D); emit8(0x84); emit8(0x24); emit32(0x88); // r8 = &rtv
            emit8(0x45); emit8(0x33); emit8(0xC9);               // r9d = 0 (pDepthStencilView = NULL)
            emit8(0xFF); emit8(0xD0);                             // call rax

            // --- Create staging texture ---
            // D3D11_TEXTURE2D_DESC at [rsp+0x90] (48 bytes)
            emit8(0x8B); emit8(0x43); emit8(0x20);             // eax = width
            emit8(0x89); emit8(0x84); emit8(0x24); emit32(0x90); // Width
            emit8(0x8B); emit8(0x43); emit8(0x24);             // eax = height
            emit8(0x89); emit8(0x84); emit8(0x24); emit32(0x94); // Height
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0x98); emit32(1);  // MipLevels = 1
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0x9C); emit32(1);  // ArraySize = 1
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0xA0); emit32(28); // Format = R8G8B8A8_UNORM
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0xA4); emit32(1);  // SampleDesc.Count = 1
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0xA8); emit32(0);  // SampleDesc.Quality = 0
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0xAC); emit32(3);  // Usage = D3D11_USAGE_STAGING
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0xB0); emit32(0);  // BindFlags = 0
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0xB4); emit32(0x10000); // CPUAccessFlags = D3D11_CPU_ACCESS_WRITE
            emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0xB8); emit32(0);  // MiscFlags = 0

            // ID3D11Device::CreateTexture2D(device, &desc, NULL, &stagingTexture)
            // stagingTexture at [rsp+0xC0]
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x38);  // rax = device
            emit8(0x48); emit8(0x8B); emit8(0x00);                // rax = vtable
            emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x28);  // rax = [rax+40] = CreateTexture2D
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x38);  // rcx = device (this)
            emit8(0x48); emit8(0x8D); emit8(0x94); emit8(0x24); emit32(0x90); // rdx = &desc
            emit8(0x45); emit8(0x33); emit8(0xC0);               // r8d = 0 (pInitialData)
            emit8(0x4C); emit8(0x8D); emit8(0x8C); emit8(0x24); emit32(0xC0); // r9 = &stagingTexture
            emit8(0xFF); emit8(0xD0);                             // call rax
            // stagingTexture at [rsp+0xC0]

            // Store stagingTexture in globals+88
            emit8(0x48); emit8(0x8B); emit8(0x84); emit8(0x24); emit32(0xC0); // rax = [rsp+0xC0]
            emit8(0x48); emit8(0x89); emit8(0x43); emit8(0x58);  // [rbx+88] = stagingTexture

            // --- Map staging texture ---
            // ID3D11DeviceContext::Map(context, stagingTexture, 0, D3D11_MAP_WRITE, 0, &mapped)
            // D3D11_MAPPED_SUBRESOURCE at [rsp+0xD0] (24 bytes: pData(8), RowPitch(4), DepthPitch(4), pad(8))
            // Compute &mapped and store as 6th param BEFORE loading Map fn into rax
            emit8(0x48); emit8(0x8D); emit8(0x84); emit8(0x24); emit32(0xD0); // rax = &mapped
            emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x28); // [rsp+0x28] = &mapped
            // Load Map fn via vtable
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x40);  // rax = context
            emit8(0x48); emit8(0x8B); emit8(0x00);                // rax = vtable
            emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x70);  // rax = [rax+112] = Map fn
            // Set up parameters
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x40);  // rcx = context (this)
            emit8(0x48); emit8(0x8B); emit8(0x53); emit8(0x58);  // rdx = [rbx+88] stagingTexture
            emit8(0x45); emit8(0x33); emit8(0xC0);               // r8d = 0 (Subresource)
            emit8(0x41); emit8(0xB9); emit32(2);                  // r9d = D3D11_MAP_WRITE (2)
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(0); // [rsp+0x20] = MapFlags = 0
            emit8(0xFF); emit8(0xD0);                             // call rax

            // Store mapped.pData in globals+40 (framebuffer pointer)
            emit8(0x48); emit8(0x8B); emit8(0x84); emit8(0x24); emit32(0xD0); // rax = mapped.pData
            emit8(0x48); emit8(0x89); emit8(0x43); emit8(0x28);  // [rbx+40] = framebuf

            // Jump over error handler
            int dx11Done = newLabel();
            emitJmp(dx11Done);
            emitLabel(dx11Failed);

            // D3D11CreateDeviceAndSwapChain failed - zero out COM pointers and framebuf
            emit8(0x48); emit8(0xC7); emit8(0x43); emit8(0x38); emit32(0);  // [rbx+56] = device = 0
            emit8(0x48); emit8(0xC7); emit8(0x43); emit8(0x40); emit32(0);  // [rbx+64] = context = 0
            emit8(0x48); emit8(0xC7); emit8(0x43); emit8(0x48); emit32(0);  // [rbx+72] = swapChain = 0
            emit8(0x48); emit8(0xC7); emit8(0x43); emit8(0x50); emit32(0);  // [rbx+80] = rtv = 0
            emit8(0x48); emit8(0xC7); emit8(0x43); emit8(0x58); emit32(0);  // [rbx+88] = stagingTexture = 0
            emit8(0x48); emit8(0xC7); emit8(0x43); emit8(0x28); emit32(0);  // [rbx+40] = framebuf = 0

            emitLabel(dx11Done);

            // Restore stack
            emit8(0x48); emit8(0x81); emit8(0xC4); emit32(0x120);

        } else {
            // ========== Software Rendering (GDI) ==========
            // GetDC(hwnd) -> eax = hdc
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);
            emit8(0x48); emit8(0x89); emit8(0xF1);                // rcx = hwnd
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "GetDC", "user32.dll"}); emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);

            // Save hdc, set up double-buffer
            emit8(0x89); emit8(0xC6);                              // esi = hdc

            // CreateCompatibleDC(hdc) -> hdcMem
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);
            emit8(0x89); emit8(0xF1);                              // ecx = hdc
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "CreateCompatibleDC", "gdi32.dll"}); emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);
            emit8(0x48); emit8(0x89); emit8(0x43); emit8(0x10);   // [rbx+16] = hdcMem
            emit8(0x48); emit8(0x89); emit8(0xC2);                // rdx = hdcMem (for SelectObject)

            // Build BITMAPINFO on stack, call CreateDIBSection
            emit8(0x48); emit8(0x81); emit8(0xEC); emit32(0x60);
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(0);
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x28); emit32(0);
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x30); emit32(40);
            emit8(0x8B); emit8(0x43); emit8(0x20);                // eax = width
            emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x34);
            emit8(0x8B); emit8(0x43); emit8(0x24);                // eax = height
            emit8(0xF7); emit8(0xD8);                              // neg eax (top-down)
            emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x38);
            emit8(0x66); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x3C); emit16(1);
            emit8(0x66); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x3E); emit16(32);
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x40); emit32(0);
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x44); emit32(0);
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x48); emit32(0);
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x4C); emit32(0);
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x50); emit32(0);
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x54); emit32(0);
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x58); emit32(0);
            emit8(0x89); emit8(0xF1);                              // rcx = hdc
            emit8(0x48); emit8(0x8D); emit8(0x54); emit8(0x24); emit8(0x30);
            emit8(0x45); emit8(0x33); emit8(0xC0);
            emit8(0x4C); emit8(0x8D); emit8(0x4C); emit8(0x24); emit8(0x58);
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "CreateDIBSection", "gdi32.dll"}); emit32(0);
            emit8(0x48); emit8(0x89); emit8(0x43); emit8(0x18);   // [rbx+24] = hBitmap
            emit8(0x48); emit8(0x8B); emit8(0x44); emit8(0x24); emit8(0x58);
            emit8(0x48); emit8(0x89); emit8(0x43); emit8(0x28);   // [rbx+40] = framebuffer
            emit8(0x48); emit8(0x81); emit8(0xC4); emit32(0x60);

            // SelectObject(hdcMem, hBitmap)
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x10);
            emit8(0x48); emit8(0x8B); emit8(0x53); emit8(0x18);
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "SelectObject", "gdi32.dll"}); emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);

            // ReleaseDC(hwnd, hdc)
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x08);
            emit8(0x89); emit8(0xF2);
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "ReleaseDC", "user32.dll"}); emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);
        }

        // Restore callee-saved registers
        emit8(0x5E);  // pop rsi
        emit8(0x5F);  // pop rdi

        emitLabel(skipAll);

        regsUsed = 1;
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // --- present() ---
    if (call->name == "present" && call->args.empty()) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        // lea rbx, [rip + win32Globals]
        emit8(0x48); emit8(0x8D); emit8(0x1D);
        heapFixups.push_back({code.size(), win32GlobalsRVA});
        emit32(0);

        if (prog.renderType == RenderType::DX11) {
            // If swapChain is NULL, skip DX11 rendering
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x48);  // rax = [rbx+72] = swapChain
            emit8(0x48); emit8(0x85); emit8(0xC0);               // test rax, rax
            int skipDx11Present = newLabel();
            emitJcc("==", skipDx11Present);

            // ========== DX11 Present ==========
            // Stack: 0x80 bytes
            // [rsp+0x00..0x1F]: shadow
            // [rsp+0x38]: backBuffer ptr (local)
            // [rsp+0x40]: rtv ptr (local)
            // [rsp+0x50..0x67]: D3D11_MAPPED_SUBRESOURCE
            emit8(0x48); emit8(0x81); emit8(0xEC); emit32(0x80);

            // --- Unmap staging texture ---
            // context->vtable[13](context, stagingTexture, 0)
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x40);  // rax = context
            emit8(0x48); emit8(0x8B); emit8(0x00);                // rax = vtable
            emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x78);  // rax = [rax+120] = Unmap
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x40);  // rcx = context (this)
            emit8(0x48); emit8(0x8B); emit8(0x53); emit8(0x58);  // rdx = [rbx+88] = stagingTexture
            emit8(0x45); emit8(0x33); emit8(0xC0);               // r8d = 0 (Subresource)
            emit8(0xFF); emit8(0xD0);                             // call rax

            // --- GetBuffer to get current back buffer ---
            // swapChain->vtable[9](swapChain, 0, &IID, &backBuffer)
            // backBuffer at [rsp+0x38]
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x48);  // rax = swapChain
            emit8(0x48); emit8(0x8B); emit8(0x00);                // rax = vtable
            emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x48);  // rax = [rax+72] = GetBuffer
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x48);  // rcx = swapChain (this)
            emit8(0x33); emit8(0xD2);                             // rdx = 0 (buffer index)
            emit8(0x45); emit8(0x33); emit8(0xC0);               // r8d = 0
            // lea r8, [IID_ID3D11Texture2D]
            emit8(0x4C); emit8(0x8D); emit8(0x05);
            {
                // Find the IID string we added in createWindow
                std::string iidBytes(16, '\0');
                iidBytes[0] = '\xF2'; iidBytes[1] = '\xAA'; iidBytes[2] = '\x15'; iidBytes[3] = '\x6F';
                iidBytes[4] = '\x08'; iidBytes[5] = '\xD2'; iidBytes[6] = '\x89'; iidBytes[7] = '\x4E';
                iidBytes[8] = '\x9A'; iidBytes[9] = '\xB4'; iidBytes[10] = '\x48'; iidBytes[11] = '\x95';
                iidBytes[12] = '\x35'; iidBytes[13] = '\xD3'; iidBytes[14] = '\x4F'; iidBytes[15] = '\x9C';
                int iidIdx = -1;
                for (size_t i = 0; i < stringPool.size(); i++) {
                    if (stringPool[i] == iidBytes) { iidIdx = (int)i; break; }
                }
                if (iidIdx < 0) {
                    iidIdx = (int)stringPool.size();
                    stringPool.push_back(iidBytes);
                }
                heapFixups.push_back({code.size(), stringRVA + stringOffsets[iidIdx]});
            }
            emit32(0);
            // r9 = &backBuffer
            emit8(0x4C); emit8(0x8D); emit8(0x4C); emit8(0x24); emit8(0x38);
            emit8(0xFF); emit8(0xD0);                             // call rax

            // --- CopyResource(context, backBuffer, stagingTexture) ---
            // context->vtable[47](context, backBuffer, stagingTexture)
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x40);  // rax = context
            emit8(0x48); emit8(0x8B); emit8(0x00);                // rax = vtable
            emit8(0x48); emit8(0x8B); emit8(0x80); emit32(0x178); // rax = [rax+376] = CopyResource
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x40);  // rcx = context (this)
            emit8(0x48); emit8(0x8B); emit8(0x54); emit8(0x24); emit8(0x38); // rdx = backBuffer
            emit8(0x4C); emit8(0x8B); emit8(0x43); emit8(0x58);  // r8 = [rbx+88] = stagingTexture
            emit8(0xFF); emit8(0xD0);                             // call rax

            // --- Release backBuffer ---
            // backBuffer->vtable[2](backBuffer)
            emit8(0x48); emit8(0x8B); emit8(0x44); emit8(0x24); emit8(0x38); // rax = backBuffer
            emit8(0x48); emit8(0x8B); emit8(0x00);                // rax = vtable
            emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x10);  // rax = [rax+16] = Release
            emit8(0x48); emit8(0x8B); emit8(0x4C); emit8(0x24); emit8(0x38); // rcx = backBuffer (this)
            emit8(0xFF); emit8(0xD0);                             // call rax

            // --- Present(1, 0) ---
            // swapChain->vtable[8](swapChain, 1, 0)
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x48);  // rax = swapChain
            emit8(0x48); emit8(0x8B); emit8(0x00);                // rax = vtable
            emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x40);  // rax = [rax+64] = Present
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x48);  // rcx = swapChain (this)
            emit8(0xBA); emit32(1);                               // rdx = SyncInterval = 1
            emit8(0x45); emit8(0x33); emit8(0xC0);               // r8d = Flags = 0
            emit8(0xFF); emit8(0xD0);                             // call rax

            // --- Map staging texture for next frame ---
            // context->vtable[11](context, stagingTexture, 0, D3D11_MAP_WRITE, 0, &mapped)
            // D3D11_MAPPED_SUBRESOURCE at [rsp+0x50]
            // Compute &mapped and store as 6th param BEFORE loading Map fn into rax
            emit8(0x48); emit8(0x8D); emit8(0x44); emit8(0x24); emit8(0x50); // rax = &mapped
            emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x28); // [rsp+0x28] = &mapped
            // Load Map fn via vtable
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x40);  // rax = context
            emit8(0x48); emit8(0x8B); emit8(0x00);                // rax = vtable
            emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x70);  // rax = [rax+112] = Map
            // Set up parameters
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x40);  // rcx = context (this)
            emit8(0x48); emit8(0x8B); emit8(0x53); emit8(0x58);  // rdx = stagingTexture
            emit8(0x45); emit8(0x33); emit8(0xC0);               // r8d = 0 (Subresource)
            emit8(0x41); emit8(0xB9); emit32(2);                  // r9d = D3D11_MAP_WRITE (2)
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(0); // MapFlags = 0
            emit8(0xFF); emit8(0xD0);                             // call rax

            // Store mapped.pData in globals+40 (framebuffer pointer)
            emit8(0x48); emit8(0x8B); emit8(0x44); emit8(0x24); emit8(0x50); // rax = mapped.pData
            emit8(0x48); emit8(0x89); emit8(0x43); emit8(0x28);  // [rbx+40] = framebuf

            // Restore stack
            emit8(0x48); emit8(0x81); emit8(0xC4); emit32(0x80);

            emitLabel(skipDx11Present);

        } else {
            // ========== Software Rendering (GDI) Present ==========
            // Allocate frame for GetDC + BitBlt + ReleaseDC
            emit8(0x48); emit8(0x81); emit8(0xEC); emit32(0x50);

            // GetDC(hwnd)
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x08);  // rcx = hwnd
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "GetDC", "user32.dll"});
            emit32(0);

            // Save hdc at [rsp+0x00]
            emit8(0x89); emit8(0x04); emit8(0x24);

            // BitBlt(hdc, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY)
            emit8(0x8B); emit8(0x0C); emit8(0x24);
            emit8(0x33); emit8(0xD2);
            emit8(0x45); emit8(0x33); emit8(0xC0);
            emit8(0x44); emit8(0x8B); emit8(0x4B); emit8(0x20);
            emit8(0x8B); emit8(0x43); emit8(0x24);
            emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x20);
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x10);
            emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x28);
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x30); emit32(0);
            emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x38); emit32(0);
            emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x40); emit32(0x00CC0020);
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "BitBlt", "gdi32.dll"});
            emit32(0);

            // ReleaseDC(hwnd, hdc)
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x08);
            emit8(0x8B); emit8(0x14); emit8(0x24);
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "ReleaseDC", "user32.dll"});
            emit32(0);
        }

        // --- Message pump (shared for both modes) ---
        int pumpLoop = newLabel();
        int pumpDone = newLabel();

        emit8(0x48); emit8(0x81); emit8(0xEC); emit32(0x60);

        emitLabel(pumpLoop);

        // PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)
        emit8(0x48); emit8(0x8D); emit8(0x4C); emit8(0x24); emit8(0x28);
        emit8(0x33); emit8(0xD2);
        emit8(0x45); emit8(0x33); emit8(0xC0);
        emit8(0x45); emit8(0x33); emit8(0xC9);
        emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(1);
        emit8(0xFF); emit8(0x15);
        importCallFixups.push_back({code.size(), "PeekMessageA", "user32.dll"});
        emit32(0);

        emit8(0x85); emit8(0xC0);
        emitJcc("==", pumpDone);

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

        emitJmp(pumpLoop);
        emitLabel(pumpDone);

        emit8(0x48); emit8(0x81); emit8(0xC4); emit32(0x60);

        // --- Sleep(1) ---
        emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);
        emit8(0xB9); emit32(1);
        emit8(0xFF); emit8(0x15);
        importCallFixups.push_back({code.size(), "Sleep", "kernel32.dll"});
        emit32(0);
        emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);

        // Restore frame based on mode
        if (prog.renderType == RenderType::DX11) {
            // No extra frame to restore (DX11 already cleaned up)
        } else {
            emit8(0x48); emit8(0x81); emit8(0xC4); emit32(0x50);
        }

        regsUsed = 1;
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // --- processMessages() ---
    if (call->name == "processMessages" && call->args.empty()) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

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
        resultReg = 0;
        return true;
    }

    return false;
}
