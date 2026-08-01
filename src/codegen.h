#pragma once
#include "ast.h"
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <filesystem>

struct StructLayout {
    std::string name;
    int totalSize;
    std::unordered_map<std::string, int> fieldOffsets;
    std::unordered_map<std::string, Type> fieldTypes;
};

struct VarInfo {
    int offset;
    Type type;
};

class Codegen {
public:
    explicit Codegen(Program& prog);
    void generate(const std::string& outputPath);
    void generateWide(const std::wstring& outputPath);
    void setCompilerDir(const std::string& dir);
    void setCompilerDir(const std::filesystem::path& dir) { compilerDir = dir; }
    bool isLibrary = false;
    bool libOutput = false;
    bool embedDLLs = false;

    // ===== codegen_builtins.cpp =====
    bool tryBuiltinCall(CallExpr* call, int& resultReg);

    // ===== codegen_gui.cpp =====
    bool tryGUICall(CallExpr* call, int& resultReg);

    // ===== codegen_dx11.cpp =====
    void emitDX11Init();
    void emitDX11Present();
    void emitDX11Cleanup();

    // ===== codegen_dx11_shaders.cpp =====
    bool tryDX11Call(CallExpr* call, int& resultReg);
    bool tryEFICall(CallExpr* call, int& resultReg);
    bool tryBIOSCall(CallExpr* call, int& resultReg);
    int ensureString(const std::string& s);

    // ===== codegen_sw.cpp =====
    void emitSWInit();
    void emitSWPresent();
    void emitSWCleanup();

    void emit8(uint8_t b);

private:
    void emit16(uint16_t v);
    void emit32(uint32_t v);
    void emit64(uint64_t v);

    int allocReg();
    void freeReg(int r);
    int allocXmmReg();
    void freeXmmReg(int r);
    uint8_t regsUsed = 0;
    uint8_t xmmRegsUsed = 0;

    void emitMovReg(int dst, int src);
    void emitMovRegImm(int r, uint32_t val);
    void emitLoadRegFromBP(int r, int offset);
    void emitStoreToBP(int offset);
    void emitStoreRegToBP(int r, int offset);
    void emitLoadRegFromBP64(int r, int offset);
    void emitStoreToBP64(int offset);

    void emitAdd(int dst, int src);
    void emitSub(int dst, int src);
    void emitImul(int dst, int src);

    void emitMovssXmm(int xmmDst, int xmmSrc);
    void emitMovssXmmFromMem(int xmmDst, int gpReg, int offset);
    void emitMovssXmmToMem(int xmmDst, int gpReg, int offset);
    void emitMovssXmmImm(int xmmDst, float val);
    void emitAddss(int xmmDst, int xmmSrc);
    void emitSubss(int xmmDst, int xmmSrc);
    void emitMulss(int xmmDst, int xmmSrc);
    void emitDivss(int xmmDst, int xmmSrc);
    void emitUcomiss(int xmmA, int xmmB);
    void emitCvtsi2ss(int xmmDst, int gpSrc);
    void emitCvtss2si(int gpDst, int xmmSrc);
    void emitMovdGpFromXmm(int gpDst, int xmmSrc);

    int nextLabel = 0;
    std::vector<int> labelPositions;
    int newLabel();
    void emitLabel(int label);
    void emitJmp(int label);
    void emitJcc(const std::string& cond, int label);

    bool isFloatExpr(Expr* expr);

    void emitEntryPoint();
    void emitFunction(FunctionDecl* func);
    int emitExpr(Expr* expr);
    void emitStmt(Stmt* stmt, const Type* stmtType = nullptr);
    int emitFloatExpr(Expr* expr);
    int emitBinaryExpr(BinaryExpr* bin, bool isFloat);
    void emitFloatStoreToBP(int xmm, int offset);
    void emitFloatLoadFromBP(int xmm, int offset);
    void emitLeaR10FromBP(int offset);
    void emitLoadFromAddr(int gpDst, int addrReg, int offset);
    void emitStoreToAddr(int gpSrc, int addrReg, int offset);
    void emitLoadQwordDisp8(int dstReg, int baseReg, int disp);
    void emitStoreQwordDisp8(int srcReg, int baseReg, int disp);
    void emitIncQwordDisp8(int baseReg, int disp);
    void emitMovQwordDisp8Imm32(int baseReg, int disp, int32_t imm);

    void spillRegs();
    void reloadRegs();
    int spillBase = 0;
    int locals = 0;

    struct CallFixup { size_t codePos; std::string target; };
    struct FuncRefFixup { size_t codePos; std::string target; };
    struct JmpFixup { size_t codePos; int targetPos; };
    struct StrFixup { size_t codePos; int stringIndex; };
    struct ImportCallFixup { size_t codePos; std::string funcName; std::string dllName; };
    struct EmbeddedLEAFixup { size_t codePos; bool isRdata; };
    std::vector<EmbeddedLEAFixup> embeddedLEAFixups;
    std::vector<CallFixup> callFixups;
    std::vector<FuncRefFixup> funcRefFixups;
    std::vector<JmpFixup> jmpFixups;
    std::vector<StrFixup> strFixups;
    std::vector<ImportCallFixup> importCallFixups;
    void resolveFixups();
    void resolveJmpFixups();
    void computeSectionRVAs();
    uint32_t estimateRdataSize();
    uint32_t estimateDataSize();

    void buildImportData();
    void fixupSectionRVAs();
    void emitDllEntryPoint();
    void buildExportDir();
    void emitWin64WinAPI(int x64Convention, bool isFloat, const std::vector<std::pair<Type,int>>& args, int stackBytes);
    void printStructs();

    void collectStrings();
    void collectStmtStrings(Stmt* stmt);
    void collectExprStrings(Expr* expr);

    struct ExportEntry {
        std::string name;
        uint32_t funcRVA;
    };
    std::vector<ExportEntry> exportEntries;
    uint32_t exportDirRVA = 0;
    uint32_t exportDirSize = 0;

    void buildPE(const std::string& path);

    Program& prog;
    std::vector<uint8_t> code;
    std::vector<uint8_t> rdata;
    std::vector<uint8_t> data;

    std::unordered_map<std::string, size_t> funcOffsets;
    size_t entryPointCodeOffset;
    size_t entryExitProcessFixup;

    VarInfo* getVarInfo(const std::string& name);

    std::unordered_map<std::string, VarInfo> varInfos;
    int frameSize = 0;
    int funcEndLabel = -1;

    std::vector<std::string> stringPool;
    std::vector<uint32_t> stringOffsets;
    uint32_t stringRVA = 0;

    struct HeapFixup { size_t codePos; uint32_t targetRVA; };
    std::vector<HeapFixup> heapFixups;
    uint32_t heapOffsetRVA = 0xFFFFFD00;
    uint32_t heapFreeHeadRVA = 0xFFFFFE00;
    uint32_t heapAreaRVA = 0xFFFFFF00;
    uint32_t win32GlobalsRVA = 0;

    size_t wndProcOffset = 0;
    uint32_t classNameRVA = 0;
    void emitWndProc();

    uint32_t iatRVA = 0;
    uint32_t dataRVA = 0x20000;

    uint32_t textRVA = 0x1000;
    uint32_t rdataRVA = 0x10000;

    std::unordered_map<std::string, StructLayout> structLayouts;
    void computeStructLayouts();
    void allocateBlockVars(const Block& block);

    struct ImportEntry {
        std::string funcName;
        std::string hintName;
        uint32_t hintNameRVA;
        uint32_t iatRVA;
    };
    struct ImportDLL {
        std::string dllName;
        std::vector<ImportEntry> entries;
        uint32_t descriptorRVA;
        uint32_t nameRVA;
        uint32_t iltRVA;
    };
    std::vector<ImportDLL> importDLLs;
    std::unordered_map<std::string, std::pair<std::string, uint32_t>> externFuncMap;

    struct EmbeddedDLL {
        std::string dllName;
        std::string moduleName;
        std::vector<uint8_t> bytes;
        struct FuncInfo { std::string name; };
        std::vector<FuncInfo> funcs;
        uint32_t blobRVA = 0;
        uint32_t blobSize = 0;
        uint32_t dllPathStrRVA = 0;
        std::vector<uint32_t> funcNameRVAs;
        std::vector<uint32_t> funcPtrRVAs;
    };
    std::vector<EmbeddedDLL> embeddedDLLs;
    uint32_t embeddedFullPathRVA = 0;
    uint32_t embeddedHFileRVA = 0;
    uint32_t embeddedHModuleRVA = 0;
    uint32_t embeddedWrittenRVA = 0;

    void readEmbeddedLibs();
    void emitEmbeddedLoader();

    uint32_t importDescCount = 0;
    uint32_t importDataSize = 0;

    std::filesystem::path compilerDir;
    std::filesystem::path outputDir;
};
