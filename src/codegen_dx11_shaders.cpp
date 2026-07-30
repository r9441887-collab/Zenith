#include "codegen.h"
#include "ast.h"
#include "parser.h"
#include <iostream>
#include <cstring>
#include <algorithm>

using namespace std;

static int ensureString(Codegen* cg, const string& s) {
    for (size_t i = 0; i < cg->stringPool.size(); i++) {
        if (cg->stringPool[i] == s) return (int)i;
    }
    int idx = (int)cg->stringPool.size();
    cg->stringPool.push_back(s);
    return idx;
}

bool Codegen::tryDX11Call(CallExpr* call, int& resultReg) {
    // === dxCreateVertexShader(hlsl) ===
    if (call->name == "dxCreateVertexShader" && call->args.size() == 1) {
        if (!call->args[0]->isString) return false;
        string hlsl = ((StringExpr*)call->args[0].get())->value;
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        int hlslIdx = -1;
        for (size_t i = 0; i < stringPool.size(); i++) {
            if (stringPool[i] == hlsl) { hlslIdx = (int)i; break; }
        }
        if (hlslIdx < 0) {
            hlslIdx = (int)stringPool.size();
            stringPool.push_back(hlsl);
        }
        int mainIdx = ensureString(this, "main");
        int vsTargetIdx = ensureString(this, "vs_5_0");

        emit8(0x48); emit8(0x81); emit8(0xEC); emit32(0x80);

        emit8(0x48); emit8(0x8D); emit8(0x1D);
        heapFixups.push_back({code.size(), win32GlobalsRVA});
        emit32(0);

        // Setup D3DCompile(hlsl,len,NULL,NULL,NULL,"main","vs_5_0",0,0,&blob,NULL)
        emit8(0x4C); emit8(0x8D); emit8(0x05);
        heapFixups.push_back({code.size(), stringRVA + stringOffsets[hlslIdx]});
        emit32(0);
        emit8(0x48); emit8(0x89); emit8(0xC1);  // rcx = hlsl
        emit8(0xBA); emit32((uint32_t)hlsl.size());  // rdx = len
        emit8(0x45); emit8(0x33); emit8(0xC0);  // r8 = NULL
        emit8(0x45); emit8(0x33); emit8(0xC9);  // r9 = NULL
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(0); // pInclude = NULL
        emit8(0x4C); emit8(0x8D); emit8(0x05);
        heapFixups.push_back({code.size(), stringRVA + stringOffsets[mainIdx]});
        emit32(0);
        emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x28); // pEntrypoint
        emit8(0x4C); emit8(0x8D); emit8(0x05);
        heapFixups.push_back({code.size(), stringRVA + stringOffsets[vsTargetIdx]});
        emit32(0);
        emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x30); // pTarget
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x38); emit32(0); // Flags1
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x40); emit32(0); // Flags2
        emit8(0x48); emit8(0x8D); emit8(0x44); emit8(0x24); emit8(0x60);
        emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x48); // &blob
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x50); emit32(0); // ppErrorMsgs = NULL

        emit8(0xFF); emit8(0x15);
        importCallFixups.push_back({code.size(), "D3DCompile", "d3dcompiler_47.dll"});
        emit32(0);

        int compileFailed = newLabel();
        int compileDone = newLabel();
        emit8(0x85); emit8(0xC0);
        emitJcc("<", compileFailed);

        // blob->GetBufferPointer() -> pBuffer
        emit8(0x48); emit8(0x8B); emit8(0x44); emit8(0x24); emit8(0x60); // rax = blob
        emit8(0x48); emit8(0x8B); emit8(0x00);
        emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x18); // GetBufferPointer
        emit8(0x48); emit8(0x8B); emit8(0x4C); emit8(0x24); emit8(0x60);
        emit8(0xFF); emit8(0xD0);
        emit8(0x49); emit8(0x89); emit8(0xC1);  // r9 = pBuffer (save in r9)

        // blob->GetBufferSize() -> size
        emit8(0x48); emit8(0x8B); emit8(0x44); emit8(0x24); emit8(0x60);
        emit8(0x48); emit8(0x8B); emit8(0x00);
        emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x20); // GetBufferSize
        emit8(0x48); emit8(0x8B); emit8(0x4C); emit8(0x24); emit8(0x60);
        emit8(0xFF); emit8(0xD0);
        emit8(0x49); emit8(0x89); emit8(0xC0);  // r8 = size

        // device->CreateVertexShader(device, pBuffer, size, NULL, &vs)
        // [rsp+0x68] = vs output
        emit8(0x4C); emit8(0x8B); emit8(0x53); emit8(0x38); // r10 = device
        emit8(0x4D); emit8(0x8B); emit8(0x12);              // r10 = [r10] = vtable
        emit8(0x4D); emit8(0x8B); emit8(0x52); emit8(0x60); // r10 = [r10+96] = CreateVertexShader (12*8)
        emit8(0x4C); emit8(0x8B); emit8(0x5B); emit8(0x38); // r11 = device (save this before fn call)
        emit8(0x4C); emit8(0x89); emit8(0xD9);              // rcx = r11 = device
        emit8(0x4C); emit8(0x89); emit8(0xCA);              // rdx = r9 = pBuffer
        // r8 already = size
        emit8(0x45); emit8(0x33); emit8(0xC9);              // r9d = 0 (pClassLinkage)
        emit8(0x48); emit8(0x8D); emit8(0x44); emit8(0x24); emit8(0x68);
        emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x20); // [rsp+0x20] = &vs
        emit8(0x41); emit8(0xFF); emit8(0xD2);              // call r10

        // Release blob
        emit8(0x48); emit8(0x8B); emit8(0x44); emit8(0x24); emit8(0x60);
        emit8(0x48); emit8(0x85); emit8(0xC0);
        int skipRelease = newLabel();
        emit8(0x74); emit8(0x0D);
        emit8(0x48); emit8(0x8B); emit8(0x00);
        emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x10);
        emit8(0x48); emit8(0x8B); emit8(0x4C); emit8(0x24); emit8(0x60);
        emit8(0xFF); emit8(0xD0);
        emitLabel(skipRelease);

        emit8(0x48); emit8(0x8B); emit8(0x44); emit8(0x24); emit8(0x68);
        emitJmp(compileDone);
        emitLabel(compileFailed);
        emit8(0x33); emit8(0xC0);
        emitLabel(compileDone);

        emit8(0x48); emit8(0x81); emit8(0xC4); emit32(0x80);
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // === dxCreatePixelShader(hlsl) ===
    if (call->name == "dxCreatePixelShader" && call->args.size() == 1) {
        if (!call->args[0]->isString) return false;
        string hlsl = ((StringExpr*)call->args[0].get())->value;
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        int hlslIdx = -1;
        for (size_t i = 0; i < stringPool.size(); i++) {
            if (stringPool[i] == hlsl) { hlslIdx = (int)i; break; }
        }
        if (hlslIdx < 0) {
            hlslIdx = (int)stringPool.size();
            stringPool.push_back(hlsl);
        }
        int mainIdx = ensureString(this, "main");
        int psTargetIdx = ensureString(this, "ps_5_0");

        emit8(0x48); emit8(0x81); emit8(0xEC); emit32(0x80);

        emit8(0x48); emit8(0x8D); emit8(0x1D);
        heapFixups.push_back({code.size(), win32GlobalsRVA});
        emit32(0);

        emit8(0x4C); emit8(0x8D); emit8(0x05);
        heapFixups.push_back({code.size(), stringRVA + stringOffsets[hlslIdx]});
        emit32(0);
        emit8(0x48); emit8(0x89); emit8(0xC1);
        emit8(0xBA); emit32((uint32_t)hlsl.size());
        emit8(0x45); emit8(0x33); emit8(0xC0);
        emit8(0x45); emit8(0x33); emit8(0xC9);
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(0);
        emit8(0x4C); emit8(0x8D); emit8(0x05);
        heapFixups.push_back({code.size(), stringRVA + stringOffsets[mainIdx]});
        emit32(0);
        emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x28);
        emit8(0x4C); emit8(0x8D); emit8(0x05);
        heapFixups.push_back({code.size(), stringRVA + stringOffsets[psTargetIdx]});
        emit32(0);
        emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x30);
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x38); emit32(0);
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x40); emit32(0);
        emit8(0x48); emit8(0x8D); emit8(0x44); emit8(0x24); emit8(0x60);
        emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x48);
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x50); emit32(0);

        emit8(0xFF); emit8(0x15);
        importCallFixups.push_back({code.size(), "D3DCompile", "d3dcompiler_47.dll"});
        emit32(0);

        int compileFailed = newLabel();
        int compileDone = newLabel();
        emit8(0x85); emit8(0xC0);
        emitJcc("<", compileFailed);

        emit8(0x48); emit8(0x8B); emit8(0x44); emit8(0x24); emit8(0x60);
        emit8(0x48); emit8(0x8B); emit8(0x00);
        emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x18);
        emit8(0x48); emit8(0x8B); emit8(0x4C); emit8(0x24); emit8(0x60);
        emit8(0xFF); emit8(0xD0);
        emit8(0x49); emit8(0x89); emit8(0xC1);  // r9 = pBuffer

        emit8(0x48); emit8(0x8B); emit8(0x44); emit8(0x24); emit8(0x60);
        emit8(0x48); emit8(0x8B); emit8(0x00);
        emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x20);
        emit8(0x48); emit8(0x8B); emit8(0x4C); emit8(0x24); emit8(0x60);
        emit8(0xFF); emit8(0xD0);
        emit8(0x49); emit8(0x89); emit8(0xC0);  // r8 = size

        // CreatePixelShader(device, pBuffer, size, NULL, &ps) - vtable index 15
        emit8(0x4C); emit8(0x8B); emit8(0x53); emit8(0x38);
        emit8(0x4D); emit8(0x8B); emit8(0x12);
        emit8(0x4D); emit8(0x8B); emit8(0x52); emit8(0x78); // [r10+120] = CreatePixelShader (15*8)
        emit8(0x4C); emit8(0x8B); emit8(0x5B); emit8(0x38); // r11 = device
        emit8(0x4C); emit8(0x89); emit8(0xD9);  // rcx = device
        emit8(0x4C); emit8(0x89); emit8(0xCA);  // rdx = pBuffer
        emit8(0x45); emit8(0x33); emit8(0xC9);  // r9d = 0
        emit8(0x48); emit8(0x8D); emit8(0x44); emit8(0x24); emit8(0x68);
        emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x20);
        emit8(0x41); emit8(0xFF); emit8(0xD2);

        emit8(0x48); emit8(0x8B); emit8(0x44); emit8(0x24); emit8(0x60);
        emit8(0x48); emit8(0x85); emit8(0xC0);
        int skipRelease = newLabel();
        emit8(0x74); emit8(0x0D);
        emit8(0x48); emit8(0x8B); emit8(0x00);
        emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x10);
        emit8(0x48); emit8(0x8B); emit8(0x4C); emit8(0x24); emit8(0x60);
        emit8(0xFF); emit8(0xD0);
        emitLabel(skipRelease);

        emit8(0x48); emit8(0x8B); emit8(0x44); emit8(0x24); emit8(0x68);
        emitJmp(compileDone);
        emitLabel(compileFailed);
        emit8(0x33); emit8(0xC0);
        emitLabel(compileDone);

        emit8(0x48); emit8(0x81); emit8(0xC4); emit32(0x80);
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // === dxCreateInputLayout(vs_hlsl) ===
    if (call->name == "dxCreateInputLayout" && call->args.size() == 1) {
        if (!call->args[0]->isString) return false;
        string hlsl = ((StringExpr*)call->args[0].get())->value;
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        int hlslIdx = -1;
        for (size_t i = 0; i < stringPool.size(); i++) {
            if (stringPool[i] == hlsl) { hlslIdx = (int)i; break; }
        }
        if (hlslIdx < 0) {
            hlslIdx = (int)stringPool.size();
            stringPool.push_back(hlsl);
        }
        int mainIdx = ensureString(this, "main");
        int vsTargetIdx = ensureString(this, "vs_5_0");
        int posIdx = ensureString(this, "POSITION");

        emit8(0x48); emit8(0x81); emit8(0xEC); emit32(0xA0);

        emit8(0x48); emit8(0x8D); emit8(0x1D);
        heapFixups.push_back({code.size(), win32GlobalsRVA});
        emit32(0);

        // D3DCompile to get vertex shader blob for reflection
        emit8(0x4C); emit8(0x8D); emit8(0x05);
        heapFixups.push_back({code.size(), stringRVA + stringOffsets[hlslIdx]});
        emit32(0);
        emit8(0x48); emit8(0x89); emit8(0xC1);
        emit8(0xBA); emit32((uint32_t)hlsl.size());
        emit8(0x45); emit8(0x33); emit8(0xC0);
        emit8(0x45); emit8(0x33); emit8(0xC9);
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x20); emit32(0);
        emit8(0x4C); emit8(0x8D); emit8(0x05);
        heapFixups.push_back({code.size(), stringRVA + stringOffsets[mainIdx]});
        emit32(0);
        emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x28);
        emit8(0x4C); emit8(0x8D); emit8(0x05);
        heapFixups.push_back({code.size(), stringRVA + stringOffsets[vsTargetIdx]});
        emit32(0);
        emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x30);
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x38); emit32(0);
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x40); emit32(0);
        emit8(0x48); emit8(0x8D); emit8(0x44); emit8(0x24); emit8(0x78);
        emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x48);
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x50); emit32(0);

        emit8(0xFF); emit8(0x15);
        importCallFixups.push_back({code.size(), "D3DCompile", "d3dcompiler_47.dll"});
        emit32(0);

        int compileFailed = newLabel();
        int compileDone = newLabel();
        emit8(0x85); emit8(0xC0);
        emitJcc("<", compileFailed);

        // Build D3D11_INPUT_ELEMENT_DESC at [rsp+0x80] (32 bytes)
        emit8(0x4C); emit8(0x8D); emit8(0x05);
        heapFixups.push_back({code.size(), stringRVA + stringOffsets[posIdx]});
        emit32(0);
        emit8(0x48); emit8(0x89); emit8(0x84); emit8(0x24); emit32(0x80); // SemanticName
        emit8(0x48); emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0x88); emit32(0); // SemanticIndex
        emit8(0x48); emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0x8C); emit32(16); // Format=R32G32_FLOAT
        emit8(0x48); emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0x90); emit32(0); // InputSlot
        emit8(0x48); emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0x94); emit32(0); // AlignedByteOffset=0 (APPEND)
        emit8(0x48); emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0x98); emit32(0); // InputSlotClass=PER_VERTEX_DATA
        emit8(0x48); emit8(0xC7); emit8(0x84); emit8(0x24); emit32(0x9C); emit32(0); // InstanceDataStepRate

        // Get pBuffer from blob
        emit8(0x48); emit8(0x8B); emit8(0x44); emit8(0x24); emit8(0x78);
        emit8(0x48); emit8(0x8B); emit8(0x00);
        emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x18);
        emit8(0x48); emit8(0x8B); emit8(0x4C); emit8(0x24); emit8(0x78);
        emit8(0xFF); emit8(0xD0);
        emit8(0x49); emit8(0x89); emit8(0xC1);  // r9 = pBuffer

        emit8(0x48); emit8(0x8B); emit8(0x44); emit8(0x24); emit8(0x78);
        emit8(0x48); emit8(0x8B); emit8(0x00);
        emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x20);
        emit8(0x48); emit8(0x8B); emit8(0x4C); emit8(0x24); emit8(0x78);
        emit8(0xFF); emit8(0xD0);
        emit8(0x49); emit8(0x89); emit8(0xC0);  // r8 = size

        // CreateInputLayout(device, &desc, 1, pBuffer, size, &layout)
        // vtable index 11: [rax+88]
        emit8(0x4C); emit8(0x8B); emit8(0x53); emit8(0x38);
        emit8(0x4D); emit8(0x8B); emit8(0x12);
        emit8(0x4D); emit8(0x8B); emit8(0x52); emit8(0x58); // [r10+88] = CreateInputLayout (11*8)
        emit8(0x4C); emit8(0x8B); emit8(0x5B); emit8(0x38); // r11 = device
        emit8(0x4C); emit8(0x89); emit8(0xD9);  // rcx = device
        emit8(0x48); emit8(0x8D); emit8(0x94); emit8(0x24); emit32(0x80); // rdx = &desc
        emit8(0x41); emit8(0xB8); emit32(1);    // r8d = NumElements = 1
        emit8(0x4C); emit8(0x89); emit8(0xCB);  // r9d... no, r9 = pBuffer (but r9 already has pBuffer)
        // Actually r9 was set to pBuffer from GetBufferPointer result
        // But we need r9 = pBuffer, and [rsp+0x20] = size
        // Let's fix: r9 = pShaderBytecode, [rsp+0x20] = BytecodeLength, [rsp+0x28] = &layout
        // r9 already = pBuffer from earlier
        emit8(0x4C); emit8(0x89); emit8(0x4C); emit8(0x24); emit8(0x20); // [rsp+0x20] = BytecodeLength
        emit8(0x48); emit8(0x8D); emit8(0x44); emit8(0x24); emit8(0x70);
        emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x28); // [rsp+0x28] = &layout
        emit8(0x41); emit8(0xFF); emit8(0xD2);

        // Release blob
        emit8(0x48); emit8(0x8B); emit8(0x44); emit8(0x24); emit8(0x78);
        emit8(0x48); emit8(0x85); emit8(0xC0);
        int skipRelease = newLabel();
        emit8(0x74); emit8(0x0D);
        emit8(0x48); emit8(0x8B); emit8(0x00);
        emit8(0x48); emit8(0x8B); emit8(0x40); emit8(0x10);
        emit8(0x48); emit8(0x8B); emit8(0x4C); emit8(0x24); emit8(0x78);
        emit8(0xFF); emit8(0xD0);
        emitLabel(skipRelease);

        emit8(0x48); emit8(0x8B); emit8(0x44); emit8(0x24); emit8(0x70);
        emitJmp(compileDone);
        emitLabel(compileFailed);
        emit8(0x33); emit8(0xC0);
        emitLabel(compileDone);

        emit8(0x48); emit8(0x81); emit8(0xC4); emit32(0xA0);
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // === dxSetShaders(vs, ps) ===
    if (call->name == "dxSetShaders" && call->args.size() == 2) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        int psReg = emitExpr(call->args[1].get());
        int vsReg = emitExpr(call->args[0].get());
        if (psReg != 0) { emitMovReg(1, psReg); freeReg(psReg); }
        else freeReg(1);
        if (vsReg != 0) { emitMovReg(0, vsReg); freeReg(vsReg); }
        else freeReg(0);

        emit8(0x48); emit8(0x8D); emit8(0x1D);
        heapFixups.push_back({code.size(), win32GlobalsRVA});
        emit32(0);

        // VSSetShader(context, vs, NULL, 0) - vtable[11]
        emit8(0x4C); emit8(0x8B); emit8(0x53); emit8(0x40); // r10 = context
        emit8(0x4D); emit8(0x8B); emit8(0x12);              // r10 = vtable
        emit8(0x4D); emit8(0x8B); emit8(0x52); emit8(0x58); // r10 = [r10+88] = VSSetShader (11*8)
        emit8(0x4C); emit8(0x8B); emit8(0x5B); emit8(0x40); // r11 = context (for this)
        emit8(0x4C); emit8(0x89); emit8(0xD9);  // rcx = context
        emit8(0x48); emit8(0x89); emit8(0xC2);  // rdx = rax = vs (r0)
        emit8(0x45); emit8(0x33); emit8(0xC0);  // r8d = 0
        emit8(0x45); emit8(0x33); emit8(0xC9);  // r9d = 0
        emit8(0x41); emit8(0xFF); emit8(0xD2);  // call r10

        // PSSetShader(context, ps, NULL, 0) - vtable[9]
        emit8(0x4C); emit8(0x8B); emit8(0x53); emit8(0x40); // r10 = context
        emit8(0x4D); emit8(0x8B); emit8(0x12);              // r10 = vtable
        emit8(0x4D); emit8(0x8B); emit8(0x52); emit8(0x48); // r10 = [r10+72] = PSSetShader (9*8)
        emit8(0x4C); emit8(0x8B); emit8(0x5B); emit8(0x40); // r11 = context
        emit8(0x4C); emit8(0x89); emit8(0xD9);  // rcx = context
        emit8(0x48); emit8(0x89); emit8(0xF2);  // rdx = rsi = ps (r1)
        emit8(0x45); emit8(0x33); emit8(0xC0);  // r8d = 0
        emit8(0x45); emit8(0x33); emit8(0xC9);  // r9d = 0
        emit8(0x41); emit8(0xFF); emit8(0xD2);  // call r10

        regsUsed = 1;
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // === dxSetInputLayout(layout) ===
    if (call->name == "dxSetInputLayout" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        int layoutReg = emitExpr(call->args[0].get());
        if (layoutReg != 0) { emitMovReg(0, layoutReg); freeReg(layoutReg); }
        else freeReg(0);

        emit8(0x48); emit8(0x8D); emit8(0x1D);
        heapFixups.push_back({code.size(), win32GlobalsRVA});
        emit32(0);

        // IASetInputLayout(context, layout) - vtable[17]
        emit8(0x4C); emit8(0x8B); emit8(0x53); emit8(0x40); // r10 = context
        emit8(0x4D); emit8(0x8B); emit8(0x12);
        emit8(0x4D); emit8(0x8B); emit8(0x52); emit8(0x88); // [r10+136] = IASetInputLayout (17*8)
        emit8(0x4C); emit8(0x8B); emit8(0x5B); emit8(0x40); // r11 = context
        emit8(0x4C); emit8(0x89); emit8(0xD9);  // rcx = context
        emit8(0x48); emit8(0x89); emit8(0xC2);  // rdx = layout
        emit8(0x41); emit8(0xFF); emit8(0xD2);  // call r10

        regsUsed = 1;
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // === dxCreateBuffer(size, data) ===
    if (call->name == "dxCreateBuffer" && call->args.size() == 2) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        int sizeReg = emitExpr(call->args[0].get());
        int dataReg = emitExpr(call->args[1].get());
        if (sizeReg != 0) { emitMovReg(0, sizeReg); freeReg(sizeReg); }
        else freeReg(0);
        if (dataReg != 0) { emitMovReg(1, dataReg); freeReg(dataReg); }
        else freeReg(1);

        emit8(0x48); emit8(0x8D); emit8(0x1D);
        heapFixups.push_back({code.size(), win32GlobalsRVA});
        emit32(0);

        // Allocate stack: desc + subres + output
        emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x40); // sub rsp, 0x40

        // [rsp+0x20]: D3D11_BUFFER_DESC start
        // ByteWidth = r0 (size)
        emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x20);
        // Usage = D3D11_USAGE_DEFAULT = 0
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x24); emit32(0);
        // BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER = 3
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x28); emit32(3);
        // CPUAccessFlags = 0
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x2C); emit32(0);
        // MiscFlags = 0
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x30); emit32(0);
        // StructureByteStride = 0
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x34); emit32(0);

        // [rsp+0x38]: D3D11_SUBRESOURCE_DATA start
        // pSysMem = r1 (data)
        emit8(0x48); emit8(0x89); emit8(0x74); emit8(0x24); emit8(0x38);
        // SysMemPitch = 0
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x40); emit32(0);
        // SysMemSlicePitch = 0
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x44); emit32(0);

        // [rsp+0x48]: output buffer ptr
        // device->CreateBuffer(device, &desc, &subres, &buffer)
        emit8(0x4C); emit8(0x8B); emit8(0x53); emit8(0x38); // r10 = device
        emit8(0x4D); emit8(0x8B); emit8(0x12);              // r10 = vtable
        emit8(0x4D); emit8(0x8B); emit8(0x52); emit8(0x18); // r10 = [r10+24] = CreateBuffer (3*8)
        emit8(0x4C); emit8(0x8B); emit8(0x5B); emit8(0x38); // r11 = device
        emit8(0x4C); emit8(0x89); emit8(0xD9);  // rcx = device
        emit8(0x48); emit8(0x8D); emit8(0x54); emit8(0x24); emit8(0x20); // rdx = &desc
        emit8(0x48); emit8(0x8D); emit8(0x44); emit8(0x24); emit8(0x38); // rax = &subres
        emit8(0x49); emit8(0x89); emit8(0xC0);  // r8 = &subres
        emit8(0x48); emit8(0x8D); emit8(0x44); emit8(0x24); emit8(0x48); // rax = &buffer
        emit8(0x49); emit8(0x89); emit8(0xC1);  // r9 = &buffer
        emit8(0x41); emit8(0xFF); emit8(0xD2);  // call r10

        // Result = buffer ptr
        emit8(0x48); emit8(0x8B); emit8(0x44); emit8(0x24); emit8(0x48);

        emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x40);
        regsUsed = 1;
        resultReg = 0;
        return true;
    }

    // === dxSetVertexBuffer(buffer, stride) ===
    if (call->name == "dxSetVertexBuffer" && call->args.size() == 2) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        int strideReg = emitExpr(call->args[1].get());
        int bufReg = emitExpr(call->args[0].get());
        if (strideReg != 0) { emitMovReg(1, strideReg); freeReg(strideReg); }
        else freeReg(1);
        if (bufReg != 0) { emitMovReg(0, bufReg); freeReg(bufReg); }
        else freeReg(0);

        emit8(0x48); emit8(0x8D); emit8(0x1D);
        heapFixups.push_back({code.size(), win32GlobalsRVA});
        emit32(0);

        // Allocate stack: &buffer, &stride, &offset
        emit8(0x48); emit8(0x83); emit8(0xEC); emit8(0x30);

        // [rsp+0x20] = buffer ptr
        emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x20);
        // [rsp+0x28] = stride
        emit8(0x48); emit8(0x89); emit8(0x74); emit8(0x24); emit8(0x28);
        // [rsp+0x2C] = offset = 0
        emit8(0x48); emit8(0xC7); emit8(0x44); emit8(0x24); emit8(0x2C); emit32(0);

        // IASetVertexBuffers(context, 0, 1, &buffer, &stride, &offset) - vtable[18]
        emit8(0x4C); emit8(0x8B); emit8(0x53); emit8(0x40); // r10 = context
        emit8(0x4D); emit8(0x8B); emit8(0x12);
        emit8(0x4D); emit8(0x8B); emit8(0x52); emit8(0x90); // [r10+144] = IASetVertexBuffers (18*8)
        emit8(0x4C); emit8(0x8B); emit8(0x5B); emit8(0x40); // r11 = context
        emit8(0x4C); emit8(0x89); emit8(0xD9);  // rcx = context
        emit8(0x33); emit8(0xD2);                             // rdx = 0 (StartSlot)
        emit8(0x41); emit8(0xB8); emit32(1);                  // r8d = 1 (NumBuffers)
        emit8(0x4C); emit8(0x8D); emit8(0x4C); emit8(0x24); emit8(0x20); // r9 = &buffer
        // 5th param: &strides
        emit8(0x48); emit8(0x8D); emit8(0x44); emit8(0x24); emit8(0x28);
        emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x20); // [rsp+0x20] = &strides
        // 6th param: &offsets
        emit8(0x48); emit8(0x8D); emit8(0x44); emit8(0x24); emit8(0x2C);
        emit8(0x48); emit8(0x89); emit8(0x44); emit8(0x24); emit8(0x28); // [rsp+0x28] = &offsets
        emit8(0x41); emit8(0xFF); emit8(0xD2);  // call r10

        emit8(0x48); emit8(0x83); emit8(0xC4); emit8(0x30);
        regsUsed = 1;
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // === dxSetIndexBuffer(buffer) ===
    if (call->name == "dxSetIndexBuffer" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        int bufReg = emitExpr(call->args[0].get());
        if (bufReg != 0) { emitMovReg(0, bufReg); freeReg(bufReg); }
        else freeReg(0);

        emit8(0x48); emit8(0x8D); emit8(0x1D);
        heapFixups.push_back({code.size(), win32GlobalsRVA});
        emit32(0);

        // IASetIndexBuffer(context, buffer, DXGI_FORMAT_R16_UINT=57, Offset=0) - vtable[19]
        emit8(0x4C); emit8(0x8B); emit8(0x53); emit8(0x40); // r10 = context
        emit8(0x4D); emit8(0x8B); emit8(0x12);
        emit8(0x4D); emit8(0x8B); emit8(0x52); emit8(0x98); // [r10+152] = IASetIndexBuffer (19*8)
        emit8(0x4C); emit8(0x8B); emit8(0x5B); emit8(0x40); // r11 = context
        emit8(0x4C); emit8(0x89); emit8(0xD9);  // rcx = context
        emit8(0x48); emit8(0x89); emit8(0xC2);  // rdx = buffer
        emit8(0x41); emit8(0xB8); emit32(57);   // r8d = DXGI_FORMAT_R16_UINT
        emit8(0x45); emit8(0x33); emit8(0xC9);  // r9d = 0
        emit8(0x41); emit8(0xFF); emit8(0xD2);  // call r10

        regsUsed = 1;
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // === dxDrawIndexed(indexCount) ===
    if (call->name == "dxDrawIndexed" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        int countReg = emitExpr(call->args[0].get());
        if (countReg != 0) { emitMovReg(0, countReg); freeReg(countReg); }
        else freeReg(0);

        emit8(0x48); emit8(0x8D); emit8(0x1D);
        heapFixups.push_back({code.size(), win32GlobalsRVA});
        emit32(0);

        // DrawIndexed(context, IndexCount, 0, 0) - vtable[12]
        emit8(0x4C); emit8(0x8B); emit8(0x53); emit8(0x40);
        emit8(0x4D); emit8(0x8B); emit8(0x12);
        emit8(0x4D); emit8(0x8B); emit8(0x52); emit8(0x60); // [r10+96] = DrawIndexed (12*8)
        emit8(0x4C); emit8(0x8B); emit8(0x5B); emit8(0x40);
        emit8(0x4C); emit8(0x89); emit8(0xD9);  // rcx = context
        emit8(0x48); emit8(0x89); emit8(0xC2);  // rdx = IndexCount
        emit8(0x45); emit8(0x33); emit8(0xC0);  // r8d = 0
        emit8(0x45); emit8(0x33); emit8(0xC9);  // r9d = 0
        emit8(0x41); emit8(0xFF); emit8(0xD2);  // call r10

        regsUsed = 1;
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    // === dxDraw(vertexCount) ===
    if (call->name == "dxDraw" && call->args.size() == 1) {
        int saved = regsUsed;
        spillRegs();
        regsUsed = 0;

        int countReg = emitExpr(call->args[0].get());
        if (countReg != 0) { emitMovReg(0, countReg); freeReg(countReg); }
        else freeReg(0);

        emit8(0x48); emit8(0x8D); emit8(0x1D);
        heapFixups.push_back({code.size(), win32GlobalsRVA});
        emit32(0);

        // Draw(context, VertexCount, 0) - vtable[13]
        emit8(0x4C); emit8(0x8B); emit8(0x53); emit8(0x40);
        emit8(0x4D); emit8(0x8B); emit8(0x12);
        emit8(0x4D); emit8(0x8B); emit8(0x52); emit8(0x68); // [r10+104] = Draw (13*8)
        emit8(0x4C); emit8(0x8B); emit8(0x5B); emit8(0x40);
        emit8(0x4C); emit8(0x89); emit8(0xD9);  // rcx = context
        emit8(0x48); emit8(0x89); emit8(0xC2);  // rdx = VertexCount
        emit8(0x45); emit8(0x33); emit8(0xC0);  // r8d = 0
        emit8(0x41); emit8(0xFF); emit8(0xD2);  // call r10

        regsUsed = 1;
        emitMovRegImm(0, 0);
        resultReg = 0;
        return true;
    }

    return false;
}
