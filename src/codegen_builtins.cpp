#include "codegen.h"
#include "ast.h"
#include "parser.h"
#include <iostream>
#include <cstring>
#include <algorithm>

using namespace std;

bool Codegen::tryBuiltinCall(CallExpr* call, int& resultReg) {
    if (call->name == "alloc" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        int sizeReg = emitExpr(call->args[0].get());
        if (sizeReg != 1) { emitMovReg(1, sizeReg); freeReg(sizeReg); sizeReg = 1; }
        // rcx = size
        // totalSize = ((size + 15) & ~15) + 16
        emit8(0x48); emit8(0x83); emit8(0xC1); emit8(15);  // add rcx, 15
        emit8(0x48); emit8(0x83); emit8(0xE1); emit8(0xF0); // and rcx, -16
        emit8(0x48); emit8(0x83); emit8(0xC1); emit8(16);  // add rcx, 16
        emit8(0x48); emit8(0x89); emit8(0xCB);  // mov rbx, rcx (save totalSize)

        emit8(0x48); emit8(0x8D); emit8(0x05);
        heapFixups.push_back({code.size(), heapAreaRVA}); emit32(0);  // rax = heapArea

        int bumpLabel = newLabel();
        int failLabel = newLabel();
        int doneLabel = newLabel();

        // Check free list
        emit8(0x48); emit8(0x8B); emit8(0x15);
        heapFixups.push_back({code.size(), heapFreeHeadRVA}); emit32(0);  // rdx = freeHead
        emit8(0x48); emit8(0x85); emit8(0xD2);  // test rdx, rdx
        emit8(0x0F); emit8(0x84);  // je bumpLabel
        jmpFixups.push_back({code.size(), bumpLabel}); emit32(0);

        emit8(0x48); emit8(0x8B); emit8(0x4A); emit8(8);  // mov rcx, [rdx+8] (blockSize)
        emit8(0x48); emit8(0x39); emit8(0xD9);  // cmp rcx, rbx
        emit8(0x0F); emit8(0x82);  // jb bumpLabel (blockSize < totalSize)
        jmpFixups.push_back({code.size(), bumpLabel}); emit32(0);

        // Use this free block — remove from free list
        emit8(0x48); emit8(0x8B); emit8(0x0A);  // mov rcx, [rdx] (nextFree)
        emit8(0x48); emit8(0x89); emit8(0x0D);
        heapFixups.push_back({code.size(), heapFreeHeadRVA}); emit32(0);  // freeHead = nextFree

        emit8(0x31); emit8(0xC9);  // xor ecx, ecx
        emit8(0x48); emit8(0x89); emit8(0x0A);  // mov [rdx], rcx (mark in-use)
        emit8(0x48); emit8(0x8D); emit8(0x42); emit8(0x10);  // lea rax, [rdx+16]
        emitJmp(doneLabel);

        // Bump allocate
        emitLabel(bumpLabel);
        emit8(0x48); emit8(0x8B); emit8(0x15);
        heapFixups.push_back({code.size(), heapOffsetRVA}); emit32(0);  // rdx = offset
        emit8(0x48); emit8(0x89); emit8(0xD1);  // mov rcx, rdx
        emit8(0x48); emit8(0x01); emit8(0xD9);  // add rcx, rbx (newOffset)
        emit8(0x48); emit8(0x81); emit8(0xF9); emit32(64 * 1024);
        emit8(0x0F); emit8(0x87);
        jmpFixups.push_back({code.size(), failLabel}); emit32(0);

        emit8(0x48); emit8(0x89); emit8(0x0D);
        heapFixups.push_back({code.size(), heapOffsetRVA}); emit32(0);  // store newOffset

        // Store totalSize at [heapArea + offset + 8]
        emit8(0x48); emit8(0x89); emit8(0x5C); emit8(0x10); emit8(8);  // mov [rax+rdx+8], rbx

        // Return heapArea + offset + 16
        emit8(0x48); emit8(0x8D); emit8(0x44); emit8(0x10); emit8(16);  // lea rax, [rax+rdx+16]
        emitJmp(doneLabel);

        emitLabel(failLabel);
        emit8(0x48); emit8(0x31); emit8(0xC0);  // xor eax, eax
        emitLabel(doneLabel);
        freeReg(1); freeReg(2); freeReg(3);
        regsUsed = (uint8_t)saved;
        reloadRegs();
        int r = allocReg(); if (r != 0) { emitMovReg(r, 0); freeReg(0); }
        resultReg = r >= 0 ? r : 0;
        return true;
    }
    if (call->name == "free" && call->args.size() == 1) {
        int r = emitExpr(call->args[0].get());
        if (r != 1) { emitMovReg(1, r); freeReg(r); }
        regsUsed = 0;
        // rcx = ptr
        emit8(0x48); emit8(0x8D); emit8(0x41); emit8(0xF0);  // lea rax, [rcx-16] (blockStart)
        emit8(0x48); emit8(0x8B); emit8(0x15);
        heapFixups.push_back({code.size(), heapFreeHeadRVA}); emit32(0);  // rdx = old freeHead
        emit8(0x48); emit8(0x89); emit8(0x10);  // mov [rax], rdx (link to free list)
        emit8(0x48); emit8(0x89); emit8(0x05);
        heapFixups.push_back({code.size(), heapFreeHeadRVA}); emit32(0);  // freeHead = blockStart
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // ============== Arena Allocator Builtins ==============
    // Arena header: [capacity:8][used:8], data at handle+16

    auto emitHeapAlloc = [this](int sizeReg) {
        if (sizeReg != 1) { emitMovReg(1, sizeReg); freeReg(sizeReg); }
        // rcx = size
        emit8(0x48); emit8(0x83); emit8(0xC1); emit8(15);  // add rcx, 15
        emit8(0x48); emit8(0x83); emit8(0xE1); emit8(0xF0); // and rcx, -16
        emit8(0x48); emit8(0x83); emit8(0xC1); emit8(16);  // add rcx, 16 (header)
        // rcx = totalSize
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
        // rcx = newOffset, rdx = old_offset, r8 = heapArea
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
        resultReg = r >= 0 ? r : 0;
        return true;
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
        resultReg = r;
        return true;
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
        resultReg = 0;
        return true;
    }

    // arenaDestroy(arena) -> void (free the arena block back to heap)
    if (call->name == "arenaDestroy" && call->args.size() == 1) {
        int r = emitExpr(call->args[0].get());
        if (r != 1) { emitMovReg(1, r); freeReg(r); }
        regsUsed = 0;
        emit8(0x48); emit8(0x8D); emit8(0x41); emit8(0xF0); // lea rax, [rcx-16] (blockStart)
        emit8(0x48); emit8(0x8B); emit8(0x15);
        heapFixups.push_back({code.size(), heapFreeHeadRVA}); emit32(0); // rdx = old freeHead
        emit8(0x48); emit8(0x89); emit8(0x10); // mov [rax], rdx (link to free list)
        emit8(0x48); emit8(0x89); emit8(0x05);
        heapFixups.push_back({code.size(), heapFreeHeadRVA}); emit32(0); // freeHead = blockStart
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
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
        resultReg = r >= 0 ? r : 0;
        return true;
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
        resultReg = r >= 0 ? r : 0;
        return true;
    }

    // poolFree(pool, ptr) -> void (no-op, use poolReset to free all)
    if (call->name == "poolFree" && call->args.size() == 2) {
        int r1 = emitExpr(call->args[0].get());
        int r2 = emitExpr(call->args[1].get());
        freeReg(r1); freeReg(r2);
        regsUsed = 1;
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
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
        resultReg = 0;
        return true;
    }

    // poolDestroy(pool) -> void (no-op)
    if (call->name == "poolDestroy" && call->args.size() == 1) {
        int r = emitExpr(call->args[0].get());
        freeReg(r);
        regsUsed = 1;
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
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
        // rcx = newOffset, rdx = old_offset, r8 = heapArea
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
        resultReg = r >= 0 ? r : 0;
        return true;
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
        resultReg = r >= 0 ? r : 0;
        return true;
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
        resultReg = 0;
        return true;
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
        resultReg = r >= 0 ? r : 0;
        return true;
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
        resultReg = 0;
        return true;
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
        resultReg = r >= 0 ? r : 0;
        return true;
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
        resultReg = 0;
        return true;
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
        resultReg = r >= 0 ? r : 0;
        return true;
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
        resultReg = 0;
        return true;
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
        resultReg = 0;
        return true;
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
        resultReg = 0;
        return true;
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
        resultReg = 0;
        return true;
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
        resultReg = 0;
        return true;
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
        resultReg = 0;
        return true;
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
        resultReg = 0;
        return true;
    }


    // ============== EFI Builtins ==============
    // efi_image_handle() — returns EFI_HANDLE
    if (call->name == "efi_image_handle" && call->args.empty()) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        emit8(0x48); emit8(0x8B); emit8(0x05);
        heapFixups.push_back({code.size(), win32GlobalsRVA});
        emit32(0);
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // efi_system_table() — returns EFI_SYSTEM_TABLE*
    if (call->name == "efi_system_table" && call->args.empty()) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        emit8(0x48); emit8(0x8B); emit8(0x05);
        heapFixups.push_back({code.size(), win32GlobalsRVA + 8});
        emit32(0);
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // efi_exit(status) — returns status from EfiMain
    if (call->name == "efi_exit" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        int statusReg = emitExpr(call->args[0].get());
        if (statusReg != 0) { emitMovReg(0, statusReg); freeReg(statusReg); }
        else freeReg(0);
        emit8(0xC9);  // leave
        emit8(0xC3);  // ret
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // efi_print(text) — prints string via EFI SystemTable ConOut->OutputString
    if (call->name == "efi_print" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        int strReg = emitExpr(call->args[0].get());
        if (strReg != 0) { emitMovReg(0, strReg); freeReg(strReg); }
        else freeReg(0);

        emit8(0x48); emit8(0x81); emit8(0xEC); emit32(0x220); // sub rsp, 0x220
        emit8(0x48); emit8(0x89); emit8(0xC6); // mov rsi, rax
        emit8(0x48); emit8(0x8D); emit8(0x7C); emit8(0x24); emit8(0x20); // lea rdi, [rsp + 0x20]
        emit8(0x48); emit8(0xC7); emit8(0xC1); emit32(255); // mov rcx, 255

        int loopLabel = newLabel();
        int endLabel = newLabel();
        emitLabel(loopLabel);
        emit8(0x8A); emit8(0x06); // mov al, [rsi]
        emit8(0x84); emit8(0xC0); // test al, al
        emitJcc("e", endLabel);
        emit8(0x66); emit8(0x89); emit8(0x07); // mov [rdi], ax
        emit8(0x48); emit8(0xFF); emit8(0xC6); // inc rsi
        emit8(0x48); emit8(0x83); emit8(0xC7); emit8(0x02); // add rdi, 2
        emit8(0x48); emit8(0xFF); emit8(0xC9); // dec rcx
        emitJcc("ne", loopLabel);

        emitLabel(endLabel);
        emit8(0x66); emit8(0xC7); emit8(0x07); emit16(0); // mov word ptr [rdi], 0

        emit8(0x48); emit8(0x8B); emit8(0x05);
        heapFixups.push_back({code.size(), win32GlobalsRVA + 8});
        emit32(0); // rax = SystemTable

        emit8(0x48); emit8(0x8B); emit8(0x48); emit8(0x40); // mov rcx, [rax + 0x40] (rcx = ConOut)
        emit8(0x48); emit8(0x8D); emit8(0x54); emit8(0x24); emit8(0x20); // lea rdx, [rsp + 0x20]
        emit8(0xFF); emit8(0x51); emit8(0x08); // call qword ptr [rcx + 8] (OutputString)

        emit8(0x48); emit8(0x81); emit8(0xC4); emit32(0x220); // add rsp, 0x220
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // ============== Bare-Metal / VGA Builtins ==============
    // Note: vga_clear/vga_putc/vga_print are handled by the cursor-aware
    // implementations in tryEFICall (EFI/Bare) and tryBIOSCall (BIOS).

    // halt() — cli; hlt loop
    if (call->name == "halt" && call->args.empty()) {
        emit8(0xFA); // cli
        emit8(0xF4); // hlt
        int loopLbl = newLabel();
        emitLabel(loopLbl);
        emit8(0xEB); emit8(0xFE); // jmp $
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // outb(port, val)
    if (call->name == "outb" && call->args.size() == 2) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        int portReg = emitExpr(call->args[0].get());
        emit8(0x50); // push port
        int valReg = emitExpr(call->args[1].get());
        emit8(0x50); // push val
        emit8(0x58); // pop rax (val)
        emit8(0x5A); // pop rdx (port)
        emit8(0xEE); // out dx, al
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // inb(port)
    if (call->name == "inb" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;
        int portReg = emitExpr(call->args[0].get());
        if (portReg != 0) { emitMovReg(2, portReg); freeReg(portReg); }
        else freeReg(0);
        emit8(0xEC); // in al, dx
        emit8(0x0F); emit8(0xB6); emit8(0xC0); // movzx eax, al
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    return false;
}
