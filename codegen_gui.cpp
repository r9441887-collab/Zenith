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
            emitDX11Cleanup();
        }

        // SR GDI cleanup: DeleteDC(hdcMem) and DeleteObject(hBitmap)
        if (prog.renderType == RenderType::Software) {
            emitSWCleanup();
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
        // hIcon = 0
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x40); emit32(0);

        // hCursor = LoadCursorA(NULL, IDC_ARROW=32512)
        emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);
        emit8(0x33); emit8(0xC9);
        emit8(0xBA); emit32(32512);
        emit8(0xFF); emit8(0x15);
        importCallFixups.push_back({code.size(), "LoadCursorA", "user32.dll"}); emit32(0);
        emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);
        emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x48);

        // hbrBackground = 5 (NULL_BRUSH to prevent white flash)
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x50); emit32(5);

        // lpszMenuName = 0
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x58); emit32(0);

        // lpszClassName
        emit8(0x48); emit8(0x8D); emit8(0x05);
        heapFixups.push_back({code.size(), classNameRVA}); emit32(0);
        emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x60);
        // hIconSm = 0
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
            emitDX11Init();
        } else {
            // ========== Software Rendering (GDI) ==========
            emitSWInit();
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

            // If a GPU frame was drawn (dxDraw/dxDrawIndexed), skip the CPU
            // staging copy — CopyResource(backBuffer, stagingTexture) would
            // overwrite the GPU-rendered back buffer with the staging content.
            emit8(0x8B); emit8(0x43); emit8(0x60);   // eax = [rbx+96] = gpuFrame
            emit8(0x85); emit8(0xC0);                // test eax, eax
            int skipCpuPresent = newLabel();
            emitJcc("!=", skipCpuPresent);

            // ========== DX11 Present ==========
            emitDX11Present();

            emitJmp(skipDx11Present);

            emitLabel(skipCpuPresent);

            // GPU-only path: back buffer already contains the GPU render, just Present
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);  // sub rsp, 0x20
            emit8(0x48); emit8(0x8B); emit8(0x43); emit8(0x48);  // rax = swapChain
            emit8(0x48); emit8(0x8B); emit8(0x00);               // rax = vtable
            emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x40); // rax = [rax+64] = Present
            emit8(0x48); emit8(0x8B); emit8(0x4B); emit8(0x48); // rcx = swapChain
            emit8(0xBA); emit32(1);                              // rdx = SyncInterval = 1
            emit8(0x45); emit8(0x33); emit8(0xC0);              // r8d = Flags = 0
            emit8(0xFF); emit8(0xD0);                            // call rax
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);  // add rsp, 0x20
            // Reset gpuFrame flag for next frame
            emit8(0xC7); emit8(0x43); emit8(0x60); emit32(0);    // [rbx+96] = 0

            emitLabel(skipDx11Present);

        } else {
            // ========== Software Rendering (GDI) Present ==========
            emitSWPresent();
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

    // ============== TOOL functions (app gui tool) ==============
    if (prog.appCategory == AppCategory::Tool) {
        // --- textWidth(text) -> int ---
        if (call->name == "textWidth" && call->args.size() == 1) {
            spillRegs();
            regsUsed = 0;
            int r = emitExpr(call->args[0].get());
            if (r != 0) { emitMovReg(0, r); freeReg(r); }
            else freeReg(0);
            // rax = text ptr; rdx = length
            emit8(0x31); emit8(0xD2);               // xor edx, edx
            int lenLoop = newLabel();
            int lenDone = newLabel();
            emitLabel(lenLoop);
            emit8(0x8A); emit8(0x0C); emit8(0x10);  // mov cl, [rax+rdx]
            emit8(0x84); emit8(0xC9);               // test cl, cl
            emitJcc("==", lenDone);
            emit8(0x48); emit8(0xFF); emit8(0xC2);  // inc rdx
            emitJmp(lenLoop);
            emitLabel(lenDone);
            emit8(0x48); emit8(0x89); emit8(0xD0);  // mov rax, rdx
            emit8(0x48); emit8(0x6B); emit8(0xC0); emit8(0x06); // imul rax, rax, 6
            regsUsed = 1;
            resultReg = 0;
            return true;
        }

        // --- drawRect(x, y, w, h, color) ---
        if (call->name == "drawRect" && call->args.size() == 5) {
            spillRegs();
            regsUsed = 0;
            // Push color, h, w, y, x
            int cReg = emitExpr(call->args[4].get());
            if (cReg != 0) { emitMovReg(0, cReg); freeReg(cReg); } else freeReg(0);
            emit8(0x50);
            int hReg = emitExpr(call->args[3].get());
            if (hReg != 0) { emitMovReg(0, hReg); freeReg(hReg); } else freeReg(0);
            emit8(0x50);
            int wReg = emitExpr(call->args[2].get());
            if (wReg != 0) { emitMovReg(0, wReg); freeReg(wReg); } else freeReg(0);
            emit8(0x50);
            int yReg = emitExpr(call->args[1].get());
            if (yReg != 0) { emitMovReg(0, yReg); freeReg(yReg); } else freeReg(0);
            emit8(0x50);
            int xReg = emitExpr(call->args[0].get());
            if (xReg != 0) { emitMovReg(0, xReg); freeReg(xReg); } else freeReg(0);
            emit8(0x50);
            emit8(0x41); emit8(0x58);  // pop r8  (x)
            emit8(0x41); emit8(0x59);  // pop r9  (y)
            emit8(0x41); emit8(0x5A);  // pop r10 (w)
            emit8(0x41); emit8(0x5B);  // pop r11 (h)
            emit8(0x41); emit8(0x5C);  // pop r12 (color)

            emit8(0x48); emit8(0x8D); emit8(0x1D);
            heapFixups.push_back({code.size(), win32GlobalsRVA});
            emit32(0);
            emit8(0x4C); emit8(0x8B); emit8(0x6B); emit8(0x28);  // mov r13, [rbx+40] (fb)
            emit8(0x44); emit8(0x8B); emit8(0x73); emit8(0x20);  // mov r14d, [rbx+32] (width)
            emit8(0x4D); emit8(0x85); emit8(0xED);               // test r13, r13
            int skipRect = newLabel();
            emitJcc("==", skipRect);
            emit8(0x4D); emit8(0x85); emit8(0xD2);               // test r10, r10 (w)
            emitJcc("<=", skipRect);
            emit8(0x4D); emit8(0x85); emit8(0xDB);               // test r11, r11 (h)
            emitJcc("<=", skipRect);

            emit8(0x45); emit8(0x31); emit8(0xFF);               // xor r15d, r15d (row)
            int outerL = newLabel();
            int innerL = newLabel();
            int nextRow = newLabel();
            int rectDone = newLabel();
            emitLabel(outerL);
            emit8(0x4D); emit8(0x39); emit8(0xDF);               // cmp r15, r11
            emitJcc(">=", rectDone);
            emit8(0x49); emit8(0x8B); emit8(0xC7);               // mov rax, r15
            emit8(0x4C); emit8(0x01); emit8(0xC8);               // add rax, r9  (y+row)
            emit8(0x49); emit8(0x0F); emit8(0xAF); emit8(0xC6);  // imul rax, r14 (width)
            emit8(0x48); emit8(0xC1); emit8(0xE0); emit8(0x02);  // shl rax, 2
            emit8(0x4C); emit8(0x01); emit8(0xE8);               // add rax, r13 (+fb)
            emit8(0x31); emit8(0xC9);                            // xor ecx, ecx (col)
            emitLabel(innerL);
            emit8(0x4C); emit8(0x39); emit8(0xD1);               // cmp rcx, r10
            emitJcc(">=", nextRow);
            emit8(0x49); emit8(0x8B); emit8(0xD0);               // mov rdx, r8  (x)
            emit8(0x48); emit8(0x01); emit8(0xCA);               // add rdx, rcx (x+col)
            emit8(0x48); emit8(0xC1); emit8(0xE2); emit8(0x02);  // shl rdx, 2
            emit8(0x48); emit8(0x01); emit8(0xC2);               // add rdx, rax
            emit8(0x44); emit8(0x89); emit8(0x22);               // mov [rdx], r12d
            emit8(0x48); emit8(0xFF); emit8(0xC1);               // inc rcx
            emitJmp(innerL);
            emitLabel(nextRow);
            emit8(0x49); emit8(0xFF); emit8(0xC7);               // inc r15
            emitJmp(outerL);
            emitLabel(rectDone);
            emitLabel(skipRect);

            regsUsed = 1;
            emitMovRegImm(0, 0);
            resultReg = 0;
            return true;
        }

        // --- drawLine(x1, y1, x2, y2, color) --- (Bresenham)
        if (call->name == "drawLine" && call->args.size() == 5) {
            spillRegs();
            regsUsed = 0;
            int cReg = emitExpr(call->args[4].get());
            if (cReg != 0) { emitMovReg(0, cReg); freeReg(cReg); } else freeReg(0);
            emit8(0x50);
            int y2Reg = emitExpr(call->args[3].get());
            if (y2Reg != 0) { emitMovReg(0, y2Reg); freeReg(y2Reg); } else freeReg(0);
            emit8(0x50);
            int x2Reg = emitExpr(call->args[2].get());
            if (x2Reg != 0) { emitMovReg(0, x2Reg); freeReg(x2Reg); } else freeReg(0);
            emit8(0x50);
            int y1Reg = emitExpr(call->args[1].get());
            if (y1Reg != 0) { emitMovReg(0, y1Reg); freeReg(y1Reg); } else freeReg(0);
            emit8(0x50);
            int x1Reg = emitExpr(call->args[0].get());
            if (x1Reg != 0) { emitMovReg(0, x1Reg); freeReg(x1Reg); } else freeReg(0);
            emit8(0x50);
            emit8(0x41); emit8(0x58);  // pop r8  (x1)
            emit8(0x41); emit8(0x59);  // pop r9  (y1)
            emit8(0x41); emit8(0x5A);  // pop r10 (x2)
            emit8(0x41); emit8(0x5B);  // pop r11 (y2)
            emit8(0x41); emit8(0x5C);  // pop r12 (color)

            emit8(0x48); emit8(0x8D); emit8(0x1D);
            heapFixups.push_back({code.size(), win32GlobalsRVA});
            emit32(0);
            emit8(0x4C); emit8(0x8B); emit8(0x6B); emit8(0x28);  // mov r13, [rbx+40] (fb, null check)
            emit8(0x44); emit8(0x8B); emit8(0x73); emit8(0x20);  // mov r14d, [rbx+32] (width)
            emit8(0x4D); emit8(0x85); emit8(0xED);               // test r13, r13
            int skipLine = newLabel();
            emitJcc("==", skipLine);

            // dx = x2 - x1
            emit8(0x49); emit8(0x8B); emit8(0xC2);               // mov rax, r10
            emit8(0x4C); emit8(0x29); emit8(0xC0);               // sub rax, r8
            emitMovRegImm(6, 1);                                 // rsi = 1 (sx)
            emit8(0x48); emit8(0x85); emit8(0xC0);               // test rax, rax
            int skipNegSx = newLabel();
            emitJcc(">=", skipNegSx);
            emitMovRegImm(6, (uint32_t)-1);                      // rsi = -1
            emit8(0x48); emit8(0xF7); emit8(0xD8);               // neg rax
            emitLabel(skipNegSx);
            emit8(0x4C); emit8(0x8B); emit8(0xE8);               // mov r13, rax (dx)

            // dy = y2 - y1
            emit8(0x49); emit8(0x8B); emit8(0xC3);               // mov rax, r11
            emit8(0x4C); emit8(0x29); emit8(0xC8);               // sub rax, r9
            emitMovRegImm(7, 1);                                 // rdi = 1 (sy)
            emit8(0x48); emit8(0x85); emit8(0xC0);               // test rax, rax
            int skipNegSy = newLabel();
            emitJcc(">=", skipNegSy);
            emitMovRegImm(7, (uint32_t)-1);                      // rdi = -1
            emit8(0x48); emit8(0xF7); emit8(0xD8);               // neg rax
            emitLabel(skipNegSy);
            emit8(0x48); emit8(0x8B); emit8(0xC8);               // mov rcx, rax (dy)

            // err = dx - dy
            emit8(0x49); emit8(0x8B); emit8(0xC5);               // mov rax, r13 (dx)
            emit8(0x48); emit8(0x29); emit8(0xC8);               // sub rax, rcx
            emit8(0x4C); emit8(0x8B); emit8(0xF8);               // mov r15, rax (err)

            int lineLoop = newLabel();
            int contLine = newLabel();
            int skipErr1 = newLabel();
            int skipErr2 = newLabel();
            int doneLine = newLabel();
            emitLabel(lineLoop);
            // plot(x1, y1)
            emit8(0x49); emit8(0x8B); emit8(0xC1);               // mov rax, r9 (y1)
            emit8(0x49); emit8(0x0F); emit8(0xAF); emit8(0xC6);  // imul rax, r14 (width)
            emit8(0x4C); emit8(0x01); emit8(0xC0);               // add rax, r8 (x1)
            emit8(0x48); emit8(0xC1); emit8(0xE0); emit8(0x02);  // shl rax, 2
            emit8(0x48); emit8(0x8B); emit8(0x53); emit8(0x28);  // mov rdx, [rbx+40] (fb)
            emit8(0x48); emit8(0x01); emit8(0xC2);               // add rdx, rax
            emit8(0x44); emit8(0x89); emit8(0x22);               // mov [rdx], r12d
            // if x1==x2 && y1==y2 done
            emit8(0x4D); emit8(0x39); emit8(0xD0);               // cmp r8, r10
            emitJcc("!=", contLine);
            emit8(0x4D); emit8(0x39); emit8(0xD9);               // cmp r9, r11
            emitJcc("==", doneLine);
            emitLabel(contLine);
            // e2 = 2*err
            emit8(0x49); emit8(0x8B); emit8(0xC7);               // mov rax, r15
            emit8(0x48); emit8(0x01); emit8(0xC0);               // add rax, rax
            // if e2 > -dy: err -= dy; x1 += sx
            emit8(0x48); emit8(0x8B); emit8(0xD1);               // mov rdx, rcx (dy)
            emit8(0x48); emit8(0xF7); emit8(0xDA);               // neg rdx
            emit8(0x48); emit8(0x39); emit8(0xD0);               // cmp rax, rdx
            emitJcc("<=", skipErr1);
            emit8(0x49); emit8(0x29); emit8(0xCF);               // sub r15, rcx
            emit8(0x49); emit8(0x01); emit8(0xF0);               // add r8, rsi
            emitLabel(skipErr1);
            // if e2 < dx: err += dx; y1 += sy
            emit8(0x4C); emit8(0x39); emit8(0xE8);               // cmp rax, r13
            emitJcc(">=", skipErr2);
            emit8(0x4D); emit8(0x01); emit8(0xEF);               // add r15, r13
            emit8(0x49); emit8(0x01); emit8(0xF9);               // add r9, rdi
            emitLabel(skipErr2);
            emitJmp(lineLoop);
            emitLabel(doneLine);
            emitLabel(skipLine);

            regsUsed = 1;
            emitMovRegImm(0, 0);
            resultReg = 0;
            return true;
        }

        // --- drawText(x, y, text, color) ---
        if (call->name == "drawText" && call->args.size() == 4) {
            spillRegs();
            regsUsed = 0;
            int cReg = emitExpr(call->args[3].get());
            if (cReg != 0) { emitMovReg(0, cReg); freeReg(cReg); } else freeReg(0);
            emit8(0x50);
            int tReg = emitExpr(call->args[2].get());
            if (tReg != 0) { emitMovReg(0, tReg); freeReg(tReg); } else freeReg(0);
            emit8(0x50);
            int yReg = emitExpr(call->args[1].get());
            if (yReg != 0) { emitMovReg(0, yReg); freeReg(yReg); } else freeReg(0);
            emit8(0x50);
            int xReg = emitExpr(call->args[0].get());
            if (xReg != 0) { emitMovReg(0, xReg); freeReg(xReg); } else freeReg(0);
            emit8(0x50);
            emit8(0x41); emit8(0x58);  // pop r8  (x)
            emit8(0x41); emit8(0x59);  // pop r9  (y)
            emit8(0x41); emit8(0x5A);  // pop r10 (text)
            emit8(0x41); emit8(0x5B);  // pop r11 (color)

            emit8(0x48); emit8(0x8D); emit8(0x1D);
            heapFixups.push_back({code.size(), win32GlobalsRVA});
            emit32(0);
            emit8(0x4C); emit8(0x8B); emit8(0x6B); emit8(0x28);  // mov r13, [rbx+40] (fb)
            emit8(0x44); emit8(0x8B); emit8(0x73); emit8(0x20);  // mov r14d, [rbx+32] (width)
            emit8(0x4D); emit8(0x85); emit8(0xED);               // test r13, r13
            int skipText = newLabel();
            emitJcc("==", skipText);
            emit8(0x4C); emit8(0x8D); emit8(0x25);               // lea r12, [rip+font]
            heapFixups.push_back({code.size(), fontRVA});
            emit32(0);

            emit8(0x45); emit8(0x31); emit8(0xFF);               // xor r15d, r15d (charIdx)
            int charLoopL = newLabel();
            int doneText = newLabel();
            int noLo = newLabel();
            int noHi = newLabel();
            int rowLoopL = newLabel();
            int bitLoopL = newLabel();
            int bitOff = newLabel();
            emitLabel(charLoopL);
            emit8(0x4B); emit8(0x0F); emit8(0xB6); emit8(0x04); emit8(0x3A);  // movzx rax, byte [r10+r15]
            emit8(0x84); emit8(0xC0);                            // test al, al
            emitJcc("==", doneText);
            emit8(0x83); emit8(0xF8); emit8(0x20);               // cmp eax, 32
            emitJcc(">=", noLo);
            emit8(0xB8); emit32(32);                             // mov eax, 32
            emitLabel(noLo);
            emit8(0x83); emit8(0xF8); emit8(0x7E);               // cmp eax, 126
            emitJcc("<=", noHi);
            emit8(0xB8); emit32(126);                            // mov eax, 126
            emitLabel(noHi);
            emit8(0x83); emit8(0xE8); emit8(0x20);               // sub eax, 32
            emit8(0x48); emit8(0x6B); emit8(0xC0); emit8(0x07);  // imul rax, rax, 7
            emit8(0x4C); emit8(0x01); emit8(0xE0);               // add rax, r12 (font)
            emit8(0x48); emit8(0x8B); emit8(0xF0);               // mov rsi, rax (glyph)
            emit8(0x49); emit8(0x8B); emit8(0xDF);               // mov rbx, r15
            emit8(0x48); emit8(0x6B); emit8(0xDB); emit8(0x06);  // imul rbx, rbx, 6
            emit8(0x4C); emit8(0x01); emit8(0xC3);               // add rbx, r8 (px)

            emit8(0x31); emit8(0xC0);                            // xor eax, eax (row)
            emitLabel(rowLoopL);
            emit8(0x48); emit8(0x0F); emit8(0xB6); emit8(0x0C); emit8(0x06);  // movzx rcx, byte [rsi+rax]
            emit8(0x50);                                         // push rax (row)
            emit8(0x51);                                         // push rcx (byte)
            emit8(0x31); emit8(0xD2);                            // xor edx, edx (bitIdx)
            emitLabel(bitLoopL);
            emit8(0x48); emit8(0x8B); emit8(0x0C); emit8(0x24);  // mov rcx, [rsp] (byte)
            emit8(0xF6); emit8(0xC1); emit8(0x01);               // test cl, 1
            emitJcc("==", bitOff);
            emit8(0x48); emit8(0x8B); emit8(0x44); emit8(0x24); emit8(0x08);  // mov rax, [rsp+8] (row)
            emit8(0x4C); emit8(0x01); emit8(0xC8);               // add rax, r9 (y+row)
            emit8(0x49); emit8(0x0F); emit8(0xAF); emit8(0xC6);  // imul rax, r14 (width)
            emit8(0xB9); emit32(4);                              // mov rcx, 4
            emit8(0x48); emit8(0x29); emit8(0xD1);               // sub rcx, rdx (col)
            emit8(0x48); emit8(0x01); emit8(0xD9);               // add rcx, rbx (px+col)
            emit8(0x48); emit8(0x01); emit8(0xC8);               // add rax, rcx
            emit8(0x48); emit8(0xC1); emit8(0xE0); emit8(0x02);  // shl rax, 2
            emit8(0x4C); emit8(0x01); emit8(0xE8);               // add rax, r13 (+fb)
            emit8(0x44); emit8(0x89); emit8(0x18);               // mov [rax], r11d
            emitLabel(bitOff);
            emit8(0x48); emit8(0x8B); emit8(0x0C); emit8(0x24);  // mov rcx, [rsp] (byte)
            emit8(0x48); emit8(0xD1); emit8(0xE9);               // shr rcx, 1
            emit8(0x48); emit8(0x89); emit8(0x0C); emit8(0x24);  // mov [rsp], rcx
            emit8(0x48); emit8(0xFF); emit8(0xC2);               // inc rdx
            emit8(0x48); emit8(0x83); emit8(0xFA); emit8(0x05);  // cmp rdx, 5
            emitJcc("<", bitLoopL);
            emit8(0x59);                                         // pop rcx (discard byte)
            emit8(0x58);                                         // pop rax (row)
            emit8(0x48); emit8(0xFF); emit8(0xC0);               // inc rax
            emit8(0x48); emit8(0x83); emit8(0xF8); emit8(0x07);  // cmp rax, 7
            emitJcc("<", rowLoopL);

            emit8(0x41); emit8(0xFF); emit8(0xC7);               // inc r15d
            emitJmp(charLoopL);
            emitLabel(doneText);
            emitLabel(skipText);

            regsUsed = 1;
            emitMovRegImm(0, 0);
            resultReg = 0;
            return true;
        }
    }

    // ============== GAME functions (app gui game) ==============
    if (prog.appCategory == AppCategory::Game) {
        // --- rand() -> int ---
        if (call->name == "rand" && call->args.empty()) {
            spillRegs();
            regsUsed = 0;
            emit8(0x48); emit8(0x8D); emit8(0x05);               // lea rax, [rip+seed]
            heapFixups.push_back({code.size(), randSeedRVA});
            emit32(0);
            emit8(0x48); emit8(0x8B); emit8(0x08);               // mov rcx, [rax]
            emit8(0x48); emit8(0x69); emit8(0xC9); emit32(0x41C64E6D);  // imul rcx, rcx, 1103515245
            emit8(0x48); emit8(0x81); emit8(0xC1); emit32(12345);       // add rcx, 12345
            emit8(0x48); emit8(0x89); emit8(0x08);               // mov [rax], rcx
            emit8(0x48); emit8(0x8B); emit8(0xC1);               // mov rax, rcx
            emit8(0x48); emit8(0xC1); emit8(0xE8); emit8(0x10);  // shr rax, 16
            emit8(0x25); emit32(0x7FFFFFFF);                     // and eax, 0x7FFFFFFF
            regsUsed = 1;
            resultReg = 0;
            return true;
        }

        // --- getTime() -> int ---
        if (call->name == "getTime" && call->args.empty()) {
            spillRegs();
            regsUsed = 0;
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);  // sub rsp, 0x20
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "GetTickCount", "kernel32.dll"});
            emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);  // add rsp, 0x20
            emit8(0x89); emit8(0xC0);                            // mov eax, eax (zero-extend)
            regsUsed = 1;
            resultReg = 0;
            return true;
        }

        // --- playTone(freq, duration) ---
        if (call->name == "playTone" && call->args.size() == 2) {
            spillRegs();
            regsUsed = 0;
            int durReg = emitExpr(call->args[1].get());
            if (durReg != 0) { emitMovReg(0, durReg); freeReg(durReg); } else freeReg(0);
            emit8(0x50);
            int freqReg = emitExpr(call->args[0].get());
            if (freqReg != 0) { emitMovReg(0, freqReg); freeReg(freqReg); } else freeReg(0);
            emit8(0x50);
            emit8(0x59);  // pop rcx (freq)
            emit8(0x5A);  // pop rdx (duration)
            emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x20);  // sub rsp, 0x20
            emit8(0xFF); emit8(0x15);
            importCallFixups.push_back({code.size(), "Beep", "kernel32.dll"});
            emit32(0);
            emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x20);  // add rsp, 0x20
            regsUsed = 1;
            emitMovRegImm(0, 0);
            resultReg = 0;
            return true;
        }
    }

    return false;
}
