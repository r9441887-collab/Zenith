#include "codegen.h"
#include "ast.h"
#include "parser.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <set>

using namespace std;

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

// ============== String Collection ==============

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
    } else if (auto fs = dynamic_cast<ForStmt*>(stmt)) {
        collectExprStrings(fs->start.get());
        collectExprStrings(fs->end.get());
        if (fs->step) collectExprStrings(fs->step.get());
        for (auto& s : fs->body.stmts) collectStmtStrings(s.get());
    }
}

void Codegen::collectStrings() {
    for (auto& func : prog.functions) {
        if (!func->isExtern) {
            for (auto& stmt : func->body.stmts) collectStmtStrings(stmt.get());
        }
    }

    // Pre-add DX11 IID bytes to stringPool so they have valid stringOffsets
    if (prog.renderType == RenderType::DX11) {
        std::string iidBytes(16, '\0');
        iidBytes[0] = '\xF2'; iidBytes[1] = '\xAA'; iidBytes[2] = '\x15'; iidBytes[3] = '\x6F';
        iidBytes[4] = '\x08'; iidBytes[5] = '\xD2'; iidBytes[6] = '\x89'; iidBytes[7] = '\x4E';
        iidBytes[8] = '\x9A'; iidBytes[9] = '\xB4'; iidBytes[10] = '\x48'; iidBytes[11] = '\x95';
        iidBytes[12] = '\x35'; iidBytes[13] = '\xD3'; iidBytes[14] = '\x4F'; iidBytes[15] = '\x9C';
        bool found = false;
        for (auto& s : stringPool) { if (s == iidBytes) { found = true; break; } }
        if (!found) stringPool.push_back(iidBytes);
    }
}

// ============== Fixup Resolution ==============

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

// ============== Section RVA Computation ==============

void Codegen::computeSectionRVAs() {
    rdataRVA = textRVA + 0x1000;
    dataRVA = rdataRVA + 0x1000;
}

uint32_t Codegen::estimateRdataSize() {
    uint32_t total = 0;

    bool isEfi = (prog.appType == AppType::EFI);

    // Import descriptors (skip for EFI)
    if (!isEfi) {
        uint32_t dllCount = 0;
        std::unordered_map<std::string, std::vector<std::string>> dllFuncMap;
        dllFuncMap["kernel32.dll"] = {"ExitProcess", "GetStdHandle", "WriteFile", "GetModuleHandleA", "Sleep"};
        if (prog.appType == AppType::GUI) {
            dllFuncMap["kernel32.dll"].push_back("AddVectoredExceptionHandler");
            dllFuncMap["user32.dll"] = {"CreateWindowExA", "DefWindowProcA", "RegisterClassExA",
                "DestroyWindow", "GetDC", "ReleaseDC", "PeekMessageA", "TranslateMessage",
                "DispatchMessageA", "GetAsyncKeyState", "PostQuitMessage", "BeginPaint",
                "EndPaint", "GetMessageA"};
            if (prog.renderType == RenderType::DX11) {
                dllFuncMap["d3d11.dll"] = {"D3D11CreateDeviceAndSwapChain", "D3D11CreateDevice"};
            } else {
                dllFuncMap["gdi32.dll"] = {"CreateDIBSection", "BitBlt", "SelectObject",
                    "DeleteObject", "DeleteDC", "CreateCompatibleDC"};
            }
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
    }

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

    // Embedded DLL strings from extern func declarations (non-system DLLs)
    for (auto& func : prog.functions) {
        if (func->isExtern && !func->dllName.empty()) {
            std::string dll = func->dllName;
            if (dll != "kernel32.dll" && dll != "user32.dll" && dll != "gdi32.dll" && dll != "ntdll.dll") {
                total += 64; // approximate per embedded DLL
            }
        }
    }

    // Embedded DLL blob sizes (each DLL binary + pointer slots for functions)
    for (auto& imp : prog.imports) {
        if (imp.dllName == "libs.dll") {
            total += 256; // approximate for embedded DLL data slots
        }
    }

    return total;
}

uint32_t Codegen::estimateDataSize() {
    if (prog.appType == AppType::EFI) {
        return 0x100; // Minimal data section for EFI
    }
    // ILT + IAT for each DLL, globals, heap offset
    uint32_t total = 0x400; // IAT/ILT entries
    total += 8;  // heap offset
    // heap area is in .bss (no .data space needed)
    if (prog.appType == AppType::GUI) {
        total += (prog.renderType == RenderType::DX11) ? 128 : 64; // win32 globals
    }
    return total + 0x2000; // safety margin
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
    if (prog.appType != AppType::EFI) {
        for (uint32_t i = 0; i < importDescCount; i++) {
            size_t base = i * 20;
            uint32_t ilt = rdDW(rdata, base);
            wrDW(rdata, base, ilt + dData);
            uint32_t name = rdDW(rdata, base + 12);
            wrDW(rdata, base + 12, name + dRdata);
            uint32_t iat = rdDW(rdata, base + 16);
            wrDW(rdata, base + 16, iat + dData);
        }

        // 3. Fix ILT/IAT entries in .data (8-byte RVAs to hint/name entries in .rdata)
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
    }

    // 4. Fix heap fixups target RVAs
    for (auto& hf : heapFixups) {
        if (hf.targetRVA >= oldRdataRVA && hf.targetRVA < oldRdataRVA + rdataSize) {
            hf.targetRVA += dRdata;
        } else if (hf.targetRVA >= oldDataRVA && hf.targetRVA <= oldDataRVA + dataSize) {
            hf.targetRVA += dData;
        }
    }

    // 5. Fix scalar RVAs
    stringRVA += dRdata;
    if (prog.appType == AppType::GUI) {
        classNameRVA += dRdata;
    }
    heapOffsetRVA += dData;
    heapFreeHeadRVA += dData;
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

// ============== DLL Export Parsing (static) ==============

static std::vector<std::string> parseDllExports(const std::vector<uint8_t>& bytes) {
    std::vector<std::string> result;
    if (bytes.size() < 64) return result;
    if (bytes[0] != 'M' || bytes[1] != 'Z') return result;

    uint32_t peOff = *(uint32_t*)&bytes[0x3C];
    if (peOff + 24 >= bytes.size()) return result;
    if (bytes[peOff] != 'P' || bytes[peOff+1] != 'E') return result;

    uint16_t numSections = *(uint16_t*)&bytes[peOff + 6];
    uint16_t optHdrSize = *(uint16_t*)&bytes[peOff + 20];
    uint32_t optStart = peOff + 24;

    uint32_t exportRVA = 0, exportSize = 0;
    if (optStart + 116 <= bytes.size()) {
        exportRVA = *(uint32_t*)&bytes[optStart + 112];
        exportSize = *(uint32_t*)&bytes[optStart + 116];
    }
    if (exportRVA == 0) return result;

    // Parse sections to find rva->offset mapping
    uint32_t secStart = optStart + optHdrSize;
    auto rvaToOff = [&](uint32_t rva) -> uint32_t {
        for (uint32_t i = 0; i < numSections; i++) {
            uint32_t s = secStart + i * 40;
            if (s + 40 > bytes.size()) break;
            uint32_t va = *(uint32_t*)&bytes[s + 12];
            uint32_t vs = *(uint32_t*)&bytes[s + 8];
            uint32_t ro = *(uint32_t*)&bytes[s + 20];
            if (rva >= va && rva < va + vs) return ro + (rva - va);
        }
        return 0;
    };

    uint32_t dirOff = rvaToOff(exportRVA);
    if (dirOff == 0 || dirOff + 40 > bytes.size()) return result;

    uint32_t numFuncs = *(uint32_t*)&bytes[dirOff + 20];
    uint32_t numNames = *(uint32_t*)&bytes[dirOff + 24];
    uint32_t addrFuncs = *(uint32_t*)&bytes[dirOff + 28];
    uint32_t addrNames = *(uint32_t*)&bytes[dirOff + 32];
    uint32_t addrOrds = *(uint32_t*)&bytes[dirOff + 36];

    uint32_t funcTableOff = rvaToOff(addrFuncs);
    uint32_t nameTableOff = rvaToOff(addrNames);
    uint32_t ordTableOff = rvaToOff(addrOrds);

    if (numNames == 0 || funcTableOff == 0 || nameTableOff == 0) return result;

    // Collect all function RVAs to detect by-ordinal-only exports
    std::set<uint32_t> namedFuncRVAs;

    for (uint32_t i = 0; i < numNames && i < 256; i++) {
        uint32_t namePtrRVA = *(uint32_t*)&bytes[nameTableOff + i * 4];
        uint16_t ord = *(uint16_t*)&bytes[ordTableOff + i * 2];
        uint32_t nameOff2 = rvaToOff(namePtrRVA);
        if (nameOff2 == 0 || nameOff2 >= bytes.size()) continue;

        // Read null-terminated name
        std::string fname;
        for (uint32_t j = 0; j < 256 && nameOff2 + j < bytes.size(); j++) {
            char c = (char)bytes[nameOff2 + j];
            if (c == '\0') break;
            fname += c;
        }
        if (!fname.empty() && ord < numFuncs) {
            result.push_back(fname);
            uint32_t funcRVA = *(uint32_t*)&bytes[funcTableOff + ord * 4];
            namedFuncRVAs.insert(funcRVA);
        }
    }

    return result;
}

// ============== Embedded Library Loading ==============

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

    // From extern func from "xxx.dll" — collect non-system DLLs (or all if embedDLLs)
    for (auto& func : prog.functions) {
        if (func->isExtern && !func->dllName.empty()) {
            std::string dll = func->dllName;
            if (embedDLLs || (dll != "kernel32.dll" && dll != "user32.dll" && dll != "gdi32.dll" && dll != "ntdll.dll")) {
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
        libsPath = compilerDir / "libs" / "libs.dll";
    }
    if (!std::filesystem::exists(libsPath) && !compilerDir.empty()) {
        libsPath = compilerDir / "libs.dll";
    }
    if (!std::filesystem::exists(libsPath) && !compilerDir.empty()) {
        libsPath = compilerDir / ".." / "libs" / "bin" / "libs.dll";
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
        // Try Windows\System32 for system DLLs
        if (!std::filesystem::exists(dllPath) && embedDLLs) {
            wchar_t sysDir[260] = {0};
            GetSystemDirectoryW(sysDir, 260);
            dllPath = std::filesystem::path(sysDir) / dll;
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

// ============== Import Data Builder ==============

void Codegen::printStructs() {
    // Placeholder: prints import table info for debugging
    for (auto& db : importDLLs) {
        std::cout << "Import DLL: " << db.dllName << "\n";
        for (auto& e : db.entries) {
            std::cout << "  " << e.funcName << " hint=0x" << std::hex << e.hintNameRVA
                      << " iat=0x" << e.iatRVA << std::dec << "\n";
        }
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

    // GUI apps: register Vectored Exception Handler to suppress D3D11/DXGI cleanup exceptions
    if (prog.appType == AppType::GUI) {
        dllFuncMap["kernel32.dll"].push_back("AddVectoredExceptionHandler");
    }

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

        if (prog.renderType == RenderType::DX11) {
            // DX11 mode: import d3d11.dll for D3D11CreateDeviceAndSwapChain
            dllFuncMap["d3d11.dll"].push_back("D3D11CreateDeviceAndSwapChain");
            dllFuncMap["d3d11.dll"].push_back("D3D11CreateDevice");
        } else {
            // Software mode: GDI functions
            dllFuncMap["gdi32.dll"].push_back("CreateDIBSection");
            dllFuncMap["gdi32.dll"].push_back("BitBlt");
            dllFuncMap["gdi32.dll"].push_back("SelectObject");
            dllFuncMap["gdi32.dll"].push_back("DeleteObject");
            dllFuncMap["gdi32.dll"].push_back("DeleteDC");
            dllFuncMap["gdi32.dll"].push_back("CreateCompatibleDC");
        }
    }

    // Collect from extern functions
    // First pass: determine which @import modules provide DLL targets for unknown funcs
    std::string libsImportModule;  // if we have @import("libs.dll::X"), unknown funcs go to libs_X.dll
    for (auto& imp : prog.imports) {
        if (imp.dllName == "libs.dll" && !imp.module.empty()) {
            libsImportModule = imp.module;
            break;
        }
    }

    for (auto& func : prog.functions) {
        if (func->isExtern) {
            std::string dllName;
            if (!func->dllName.empty()) {
                dllName = func->dllName;
            } else {
                dllName = mapFuncToDll(func->name);
                // If the function wasn't recognized by mapFuncToDll and we have a libs @import,
                // route it to the embedded libs DLL instead of kernel32.dll
                if (dllName == "kernel32.dll" && !libsImportModule.empty()) {
                    // Check it's not a known system function
                    static const std::set<std::string> knownFuncs = {
                        "ExitProcess", "GetStdHandle", "WriteFile", "ReadFile",
                        "HeapAlloc", "HeapFree", "GetProcessHeap", "GetModuleHandleA", "Sleep"
                    };
                    if (knownFuncs.find(func->name) == knownFuncs.end()) {
                        dllName = "libs_" + libsImportModule + ".dll";
                    }
                }
            }
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
        // Auto-declare: if no extern funcs were declared for this DLL,
        // parse its PE export table and add all exported functions
        if (emb.funcs.empty() && !emb.bytes.empty()) {
            std::vector<std::string> exports = parseDllExports(emb.bytes);
            for (auto& fname : exports) {
                emb.funcs.push_back({fname});
                externFuncMap[fname] = {"EMBEDDED_" + emb.dllName, 0};
            }
            if (!exports.empty()) {
                std::cout << "Auto-imported " << exports.size() << " functions from " << emb.dllName << std::endl;
            }
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

    // Win32 globals for 2D graphics built-ins (GUI only)
    if (prog.appType == AppType::GUI) {
        win32GlobalsRVA = dataRVA + (uint32_t)data.size();
        if (prog.renderType == RenderType::DX11) {
            // DX11 globals: 128 bytes
            for (int k = 0; k < 128; k++) data.push_back(0);
        } else {
            // Software globals: 56 bytes
            for (int k = 0; k < 56; k++) data.push_back(0);
        }
    }

    // === EMBEDDED DLL: .data entries ===
    if (!embeddedDLLs.empty()) {
        embeddedFullPathRVA = dataRVA + (uint32_t)data.size();
        for (int k = 0; k < 520; k++) data.push_back(0);
        embeddedHFileRVA = dataRVA + (uint32_t)data.size();
        for (int k = 0; k < 8; k++) data.push_back(0);
        embeddedHModuleRVA = dataRVA + (uint32_t)data.size();
        for (int k = 0; k < 8; k++) data.push_back(0);
        embeddedWrittenRVA = dataRVA + (uint32_t)data.size();
        for (int k = 0; k < 8; k++) data.push_back(0);

        for (auto& emb : embeddedDLLs) {
            emb.blobRVA = dataRVA + (uint32_t)data.size();
            data.insert(data.end(), emb.bytes.begin(), emb.bytes.end());
            while (data.size() % 8 != 0) data.push_back(0);

            emb.funcPtrRVAs.clear();
            for (auto& fn : emb.funcs) {
                uint32_t slotRVA = dataRVA + (uint32_t)data.size();
                emb.funcPtrRVAs.push_back(slotRVA);
                for (int k = 0; k < 8; k++) data.push_back(0);
            }
        }

        for (auto& emb : embeddedDLLs) {
            for (size_t f = 0; f < emb.funcs.size(); f++) {
                externFuncMap[emb.funcs[f].name] = {"EMBEDDED_" + emb.dllName, emb.funcPtrRVAs[f]};
            }
        }
    }

    // Heap offset (8 bytes in .data) — bump allocator state
    heapOffsetRVA = dataRVA + (uint32_t)data.size();
    for (int k = 0; k < 8; k++) data.push_back(0);
    // Heap free list head (8 bytes in .data) — 0 = no free blocks
    heapFreeHeadRVA = dataRVA + (uint32_t)data.size();
    for (int k = 0; k < 8; k++) data.push_back(0);
    // Heap area RVA — points to .bss (zero-init at runtime, no file space)
    heapAreaRVA = dataRVA + (uint32_t)data.size();

    // Replace heap fixup sentinels with actual RVAs (fixups were emitted before RVAs were known)
    for (auto& hf : heapFixups) {
        if (hf.targetRVA == 0xFFFFFF00) hf.targetRVA = heapAreaRVA;
        else if (hf.targetRVA == 0xFFFFFE00) hf.targetRVA = heapFreeHeadRVA;
        else if (hf.targetRVA == 0xFFFFFD00) hf.targetRVA = heapOffsetRVA;
    }
}

// ============== PE Builder ==============

void Codegen::buildPE(const std::string& outputPath) {
    bool isEfi = (prog.appType == AppType::EFI);

    // Patch ExitProcess call (EXE only, not EFI)
    if (!libOutput && !isEfi) {
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

    DOSHeader dos;
    IMAGE_FILE_HEADER coff;
    IMAGE_OPTIONAL_HEADER64 opt = {};

    // Heap area resides in .bss (zero bytes on disk, zero-initialized in memory)
    uint32_t rawDataEnd = dataRVA + dataSize;
    uint32_t bssSize = 64 * 1024;
    uint32_t bssRVA = (rawDataEnd + 0xFFF) & ~0xFFF;

    // Snap heapAreaRVA to .bss start and adjust fixups
    if (heapAreaRVA != bssRVA) {
        int32_t bssDelta = (int32_t)(bssRVA - heapAreaRVA);
        for (auto& hf : heapFixups) {
            if (hf.targetRVA == heapAreaRVA) {
                hf.targetRVA += bssDelta;
            }
        }
        heapAreaRVA = bssRVA;
    }

    coff.Machine = 0x8664;
    coff.NumberOfSections = 4;
    coff.SizeOfOptionalHeader = sizeof(opt);

    if (isEfi) {
        opt.Subsystem = 10;  // EFI_APPLICATION
        opt.DllCharacteristics = 0x0160;
        coff.Characteristics = 0x0022; // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE
    } else {
        opt.Subsystem = (prog.appType == AppType::GUI) ? 2 : 3;
        opt.DllCharacteristics = 0x0160; // NX_COMPAT | DYNAMIC_BASE | HIGH_ENTROPY_VA
        if (libOutput) {
            coff.Characteristics = 0x2022; // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE | DLL
            opt.Subsystem = 2; // GUI subsystem for DLL (works with DllMain)
        } else {
            coff.Characteristics = 0x0022; // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE
        }
    }

    opt.SizeOfCode = textRawSize;
    opt.SizeOfInitializedData = rdataRawSize + dataRawSize;
    opt.AddressOfEntryPoint = textRVA + (uint32_t)entryPointCodeOffset;

    // Calculate sections end for SizeOfImage
    uint32_t textEnd = textRVA + ((textSize + 0xFFF) & ~0xFFF);
    uint32_t rdataEnd = rdataRVA + ((rdataSize + 0xFFF) & ~0xFFF);
    uint32_t dataEnd = dataRVA + ((dataSize + 0xFFF) & ~0xFFF);
    uint32_t bssEnd = bssRVA + ((bssSize + 0xFFF) & ~0xFFF);
    uint32_t lastSectionEnd = bssEnd;
    if (dataEnd > lastSectionEnd) lastSectionEnd = dataEnd;
    if (rdataEnd > lastSectionEnd) lastSectionEnd = rdataEnd;
    if (textEnd > lastSectionEnd) lastSectionEnd = textEnd;
    opt.SizeOfImage = (lastSectionEnd + 0xFFF) & ~0xFFF;
    // Compute raw offsets after COFF/opt fields are populated
    uint32_t headerRawSize = (dos.e_lfanew + 4 + sizeof(coff) + sizeof(opt) + coff.NumberOfSections * sizeof(IMAGE_SECTION_HEADER) + 0x1FF) & ~0x1FF;
    uint32_t textRawOfs = headerRawSize;
    uint32_t rdataRawOfs = textRawOfs + textRawSize;
    uint32_t dataRawOfs = rdataRawOfs + rdataRawSize;
    opt.SizeOfHeaders = headerRawSize;

    // Import directory entry — point to descriptors in .rdata
    // EFI has no imports, skip import directory
    uint32_t importDescSize = (importDescCount + 1) * 20;
    if (!isEfi) {
        opt.DataDirectory[1].VirtualAddress = rdataRVA;
        opt.DataDirectory[1].Size = importDescSize;
    }

    // Export directory entry for DLL mode
    if (libOutput && exportDirSize > 0) {
        opt.DataDirectory[0].VirtualAddress = exportDirRVA;
        opt.DataDirectory[0].Size = exportDirSize;
    }

    IMAGE_SECTION_HEADER textSec{}, rdataSec{}, dataSec{}, bssSec{};

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

    memcpy(bssSec.Name, ".bss", 5);
    bssSec.VirtualSize = bssSize;
    bssSec.VirtualAddress = bssRVA;
    bssSec.SizeOfRawData = 0;
    bssSec.PointerToRawData = 0;
    bssSec.Characteristics = 0xC0000080;

    std::ofstream f{safeNarrowToPath(outputPath), std::ios::binary};
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
    f.write((const char*)&bssSec, sizeof(bssSec));

    size_t hEnd = dos.e_lfanew + 4 + sizeof(coff) + sizeof(opt) + sizeof(textSec) + sizeof(rdataSec) + sizeof(dataSec) + sizeof(bssSec);
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

// ============== DLL Entry Point ==============

void Codegen::emitDllEntryPoint() {
    entryPointCodeOffset = code.size();
    // DllMain(hinstDLL, fdwReason, lpvReserved)
    // return TRUE (1) — x64 uses caller-managed stack, plain ret
    emit8(0x33); emit8(0xC0);  // xor eax, eax
    emit8(0xFF); emit8(0xC0);  // inc eax
    emit8(0xC3);               // ret
}

// ============== Export Directory ==============

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

    // PE spec requires Name Pointer Table sorted alphabetically for binary search
    std::vector<uint32_t> sortedIdx(N);
    for (uint32_t i = 0; i < N; i++) sortedIdx[i] = i;
    std::sort(sortedIdx.begin(), sortedIdx.end(), [&](uint32_t a, uint32_t b) {
        return exportEntries[a].name < exportEntries[b].name;
    });

    for (auto idx : sortedIdx) {
        writeAt(nameStrRVAs[idx]);
    }

    for (auto idx : sortedIdx) {
        rdata.push_back((uint8_t)(idx & 0xFF));
        rdata.push_back((uint8_t)((idx >> 8) & 0xFF));
    }

    exportDirSize = (uint32_t)(rdata.size() - (exportDirRVA - rdataRVA));
}

// ============== Embedded DLL Loader ==============

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

// ============== Win64 WinAPI Calling Convention Helper ==============

void Codegen::emitWin64WinAPI(int x64Convention, bool isFloat, const std::vector<std::pair<Type,int>>& args, int stackBytes) {
    // Emit a Win64 API call with the given arguments.
    // x64Convention: 0 = __stdcall (default for Win64), 1 = __cdecl
    // isFloat: true if the return value is a float (returned in xmm0)
    // args: vector of (Type, registerOrStackSlot) pairs
    // stackBytes: additional stack bytes to allocate for shadow space

    int intArgCount = 0;
    int floatArgCount = 0;

    for (size_t i = 0; i < args.size(); i++) {
        bool isFloatArg = (args[i].first.kind == TypeKind::Float);
        if (isFloatArg) {
            if (floatArgCount < 4) {
                floatArgCount++;
            }
        } else {
            if (intArgCount < 4) {
                intArgCount++;
            }
        }
    }

    // Shadow space is always 0x20 bytes for Win64
    int totalShadow = 0x20;
    if (stackBytes > totalShadow) totalShadow = stackBytes;

    // Align to 16 bytes
    int frameAlloc = (totalShadow + 15) & ~15;

    if (frameAlloc <= 127) {
        emit8(0x48); emit8(0x83); emit8(0xEC); emit8((uint8_t)frameAlloc);
    } else {
        emit8(0x48); emit8(0x81); emit8(0xEC); emit32((uint32_t)frameAlloc);
    }

    // After the call, restore stack
    if (frameAlloc <= 127) {
        emit8(0x48); emit8(0x83); emit8(0xC4); emit8((uint8_t)frameAlloc);
    } else {
        emit8(0x48); emit8(0x81); emit8(0xC4); emit32((uint32_t)frameAlloc);
    }
}
