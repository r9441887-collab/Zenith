#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "optimizer.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <cstdio>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <locale>

namespace fs = std::filesystem;

static const char* argv0 = nullptr;

static bool hasNoMain(const std::string& source) {
    bool inString = false;
    bool inLineComment = false;
    for (size_t i = 0; i < source.size(); i++) {
        char c = source[i];
        if (inLineComment) {
            if (c == '\n') inLineComment = false;
            continue;
        }
        if (inString) {
            if (c == '\\') { i++; continue; }
            if (c == '"') inString = false;
            continue;
        }
        if (c == '"') { inString = true; continue; }
        if (c == '#') {
            size_t j = i + 1;
            while (j < source.size() && (source[j] == ' ' || source[j] == '\t')) j++;
            if (source.substr(j, 9) == "[no_main]") return true;
            // Also check for inline: "# [no_main]" anywhere on the line
            while (j < source.size() && source[j] != '\n') {
                if (source[j] == '[' && source.substr(j, 9) == "[no_main]") return true;
                j++;
            }
        }
    }
    return false;
}

static std::string readFile(const std::string& path) {
    // Use _wfopen to handle Cyrillic paths correctly on MinGW
    std::wstring wpath;
    {
        int wlen = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, NULL, 0);
        if (wlen > 0) {
            wpath.resize(wlen - 1);
            MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, &wpath[0], wlen);
        }
    }
    FILE* f = wpath.empty() ? nullptr : _wfopen(wpath.c_str(), L"rb");
    if (!f) {
        std::cerr << "Error: cannot open file '" << path << "'" << std::endl;
        exit(1);
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return ""; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return ""; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return ""; }
    std::string content(sz, '\0');
    if (sz > 0) {
        size_t read = fread(&content[0], 1, sz, f);
        content.resize(read);
    }
    fclose(f);
    // Strip UTF-8 BOM if present
    if (content.size() >= 3 &&
        (uint8_t)content[0] == 0xEF &&
        (uint8_t)content[1] == 0xBB &&
        (uint8_t)content[2] == 0xBF) {
        content = content.substr(3);
    }
    return content;
}

static void writeFile(const std::string& path, const std::string& content) {
    fs::path p(path);
    fs::create_directories(p.parent_path());
    std::ofstream f{p, std::ios::binary};
    if (!f.is_open()) {
        std::cerr << "Error: cannot write file '" << path << "'" << std::endl;
        exit(1);
    }
    f << content;
}

static void printUsage() {
    std::cout << "Zenith Compiler v2.0" << std::endl;
    std::cout << "Usage:" << std::endl;
    std::cout << "  zenith <input.z> -o <output>       Compile a single file" << std::endl;
    std::cout << "  zenith <input.z> --lib -o out.dll  Compile as DLL (custom output)" << std::endl;
    std::cout << "  zenith <input.z> --libs            Compile as DLL into libs/ folder" << std::endl;
    std::cout << "  zenith <input.z> --embed           Embed system DLLs into output" << std::endl;
    std::cout << "  zenith new <name>                   Create a new project" << std::endl;
    std::cout << "  zenith new --lib <name>             Create a new DLL project" << std::endl;
    std::cout << "  zenith build                        Build project from workspace.zen" << std::endl;
    std::cout << "  zenith build --lib                  Build as DLL and pack into libs.dll" << std::endl;
}

static void printVersion() {
    std::cout << "Zenith Compiler v2.0" << std::endl;
    std::cout << "Zero-dependency x86_64 Windows compiler" << std::endl;
}

static int cmdNew(const std::string& projectName, bool libMode = false) {
    if (projectName.empty()) {
        std::cerr << "Error: project name required" << std::endl;
        std::cout << "Usage: zenith new [--lib] <name>" << std::endl;
        return 1;
    }

    fs::path projectDir = fs::current_path() / projectName;

    if (fs::exists(projectDir)) {
        std::cerr << "Error: directory '" << projectName << "' already exists" << std::endl;
        return 1;
    }

    // Create directories
    fs::create_directories(projectDir / "src");
    fs::create_directories(projectDir / "exe");

    if (libMode) {
        // Create workspace.zen for DLL output
        writeFile((projectDir / "workspace.zen").string(),
            "# DLL project\n"
            "output dll\n"
            "src\n"
        );

        // Create src/main.z with [no_main] marker
        writeFile((projectDir / "src" / "main.z").string(),
            "app console\n"
            "\n"
            "# [no_main]\n"
            "\n"
            "extern func add(a: int, b: int) -> int\n"
            "\n"
            "func add(a: int, b: int) -> int\n"
            "    return a + b\n"
            "end\n"
        );

        std::cout << "DLL project '" << projectName << "' created:" << std::endl;
        std::cout << "  " << projectName << "/src/main.z    - library source" << std::endl;
        std::cout << "  " << projectName << "/workspace.zen - project config (output dll)" << std::endl;
        std::cout << "  " << projectName << "/exe/           - build output" << std::endl;
    } else {
        // Create workspace.zen
        writeFile((projectDir / "workspace.zen").string(),
            "src\n"
        );

        // Create src/main.z with Hello World
        writeFile((projectDir / "src" / "main.z").string(),
            "app console\n"
            "\n"
            "func main() -> int\n"
            "    print(\"Hello, World!\")\n"
            "    return 0\n"
            "end\n"
        );

        std::cout << "Project '" << projectName << "' created:" << std::endl;
        std::cout << "  " << projectName << "/src/main.z    - source code" << std::endl;
        std::cout << "  " << projectName << "/workspace.zen - project config" << std::endl;
        std::cout << "  " << projectName << "/exe/           - build output" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Next steps:" << std::endl;
    std::cout << "  cd " << projectName << std::endl;
    std::cout << "  zenith build" << (libMode ? " --lib" : "") << std::endl;

    return 0;
}

static int cmdBuild(bool libMode = false) {
    fs::path cwd = fs::current_path();
    fs::path workspaceFile = cwd / "workspace.zen";

    if (!fs::exists(workspaceFile)) {
        std::cerr << "Error: workspace.zen not found in current directory" << std::endl;
        std::cerr << "Run 'zenith new <name>' to create a project, or run this from a project directory." << std::endl;
        return 1;
    }

    // Parse workspace.zen
    std::ifstream wf(workspaceFile);
    if (!wf.is_open()) {
        std::cerr << "Error: cannot open workspace.zen" << std::endl;
        return 1;
    }

    std::vector<std::string> sourceDirs;
    std::string line;
    while (std::getline(wf, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        size_t end = line.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start, end - start + 1);
        // Skip comments after content (only # preceded by whitespace)
        size_t commentPos = std::string::npos;
        for (size_t i = 0; i < line.size(); i++) {
            if (line[i] == '#' && (i == 0 || line[i-1] == ' ' || line[i-1] == '\t')) {
                commentPos = i;
                break;
            }
        }
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
            end = line.find_last_not_of(" \t\r\n");
            if (end == std::string::npos) continue;
            line = line.substr(0, end + 1);
        }
        // Check for "output dll" directive (after comment stripping)
        size_t trail = line.find_last_not_of(" \t\r\n");
        if (trail != std::string::npos) line = line.substr(0, trail + 1);
        if (line == "output dll") {
            libMode = true;
            continue;
        }
        if (!line.empty()) {
            sourceDirs.push_back(line);
        }
    }
    wf.close();

    if (sourceDirs.empty()) {
        std::cerr << "Error: no source directories listed in workspace.zen" << std::endl;
        std::cerr << "Add directory names to workspace.zen, one per line." << std::endl;
        return 1;
    }

    // Collect all .z files from listed directories
    std::vector<fs::path> sourceFiles;
    for (auto& dir : sourceDirs) {
        fs::path dirPath = cwd / dir;
        if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
            std::cerr << "Warning: directory '" << dir << "' not found, skipping" << std::endl;
            continue;
        }
        for (auto& entry : fs::directory_iterator(dirPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".z") {
                sourceFiles.push_back(entry.path());
            }
        }
    }

    // Sort for deterministic compilation order
    std::sort(sourceFiles.begin(), sourceFiles.end());

    if (sourceFiles.empty()) {
        std::cerr << "Error: no .z files found in source directories" << std::endl;
        return 1;
    }

        // Determine output name from project directory
    std::string projectName = cwd.filename().string();
    fs::path exeDir = cwd / "exe";
    fs::create_directories(exeDir);

    if (libMode) {
        // In --lib mode: compile each .z file separately, then pack into libs.dll
        fs::path compilerPath;
        {
            // Use wide API to avoid encoding issues with Cyrillic paths
            wchar_t exePathW[MAX_PATH] = {0};
            GetModuleFileNameW(NULL, exePathW, MAX_PATH);
            fs::path p(exePathW);
            if (p.has_parent_path()) {
                compilerPath = p.parent_path();
            } else {
                compilerPath = fs::current_path();
            }
        }

        // Temp dir for intermediate DLLs
        fs::path libDir = cwd / "lib";
        fs::create_directories(libDir);

        // Collect all compiled DLL binaries
        struct DLLBinary {
            std::string name;
            std::vector<uint8_t> data;
        };
        std::vector<DLLBinary> dllBinaries;

        for (auto& file : sourceFiles) {
            std::string baseName = file.stem().string();
            std::string dllName = "libs_" + baseName + ".dll";
            std::string dllPath = (libDir / dllName).string();

            // Read source
            std::string source = readFile(file.string());
            bool fileIsLib = hasNoMain(source);

            // Lex
            Lexer lexer(source);
            std::vector<Token> tokens;
            try {
                tokens = lexer.all();
            } catch (const std::exception& e) {
                std::cerr << "Lexer error in " << file << ": " << e.what() << std::endl;
                return 1;
            }
            for (auto& t : tokens) {
                if (t.kind == TokenKind::Error) {
                    std::cerr << "Lexer error in " << file << " at line " << t.line << ": " << t.text << std::endl;
                    return 1;
                }
            }

            // Parse
            Parser parser(tokens);
            Program prog;
            try {
                prog = parser.parse();
            } catch (const std::exception& e) {
                std::cerr << "Parser error in " << file << ": " << e.what() << std::endl;
                return 1;
            }

            prog.isLibrary = fileIsLib;
            if (prog.functions.empty()) {
                std::cerr << "Error: no functions found in " << file << std::endl;
                return 1;
            }

            // Generate code as DLL
            Codegen codegen(prog);
            codegen.setCompilerDir(compilerPath);
            codegen.isLibrary = true;
            codegen.libOutput = true;
            try {
                codegen.generate(dllPath);
            } catch (const std::exception& e) {
                std::cerr << "Codegen error in " << file << ": " << e.what() << std::endl;
                return 1;
            }

            // Read compiled DLL into memory
            DLLBinary db;
            db.name = dllName;
            std::ifstream dllFile(dllPath, std::ios::binary | std::ios::ate);
            if (dllFile.is_open()) {
                std::streamsize size = dllFile.tellg();
                if (size > 0) {
                    dllFile.seekg(0, std::ios::beg);
                    db.data.resize(size);
                    dllFile.read((char*)db.data.data(), size);
                }
                dllFile.close();
            } else {
                std::cerr << "Error: cannot read compiled DLL " << dllPath << std::endl;
                return 1;
            }
            if (db.data.empty()) {
                std::cerr << "Error: compiled DLL is empty: " << dllPath << std::endl;
                return 1;
            }
            dllBinaries.push_back(std::move(db));
            std::cout << "Compiled: " << file << " -> " << dllName << std::endl;
        }

        // Pack all DLLs into libs.dll (in compiler's libs/ folder, overwriting the stub)
        fs::path libsDir = compilerPath / "libs";
        fs::create_directories(libsDir);
        std::string libsPath = (libsDir / "libs.dll").string();
        std::ofstream libsOut{fs::path(libsPath), std::ios::binary};
        if (!libsOut.is_open()) {
            std::cerr << "Error: cannot create libs.dll" << std::endl;
            return 1;
        }

        // Header: magic + count
        libsOut.write("ZLIBS", 5);
        uint8_t pad[3] = {0, 0, 0};
        libsOut.write((char*)pad, 3);
        uint32_t count = (uint32_t)dllBinaries.size();
        libsOut.write((char*)&count, 4);

        // Write each entry: name_len, name, data_len, data
        for (auto& db : dllBinaries) {
            uint32_t nameLen = (uint32_t)db.name.size();
            libsOut.write((char*)&nameLen, 4);
            libsOut.write(db.name.c_str(), nameLen);
            uint32_t dataLen = (uint32_t)db.data.size();
            libsOut.write((char*)&dataLen, 4);
            libsOut.write((char*)db.data.data(), dataLen);
        }
        libsOut.close();

        std::cout << "Packed " << dllBinaries.size() << " DLLs into " << libsPath << std::endl;

        // Delete individual .dll files after successful packing
        for (auto& file : sourceFiles) {
            std::string baseName = file.stem().string();
            std::string dllName = "libs_" + baseName + ".dll";
            fs::path dllPath = libDir / dllName;
            if (fs::exists(dllPath)) {
                fs::remove(dllPath);
            }
        }
        // Remove temp lib/ dir if empty
        if (fs::exists(libDir) && fs::is_directory(libDir)) {
            bool empty = true;
            for (auto& _ : fs::directory_iterator(libDir)) { empty = false; break; }
            if (empty) fs::remove(libDir);
        }
        std::cout << "Cleaned up temporary DLL files" << std::endl;

        return 0;
    }

    // Non-lib mode: concatenate all source files into single binary
    std::string outputFile = (exeDir / (projectName + ".exe")).string();
    fs::path compilerPath;
    {
        wchar_t exePathW[MAX_PATH] = {0};
        GetModuleFileNameW(NULL, exePathW, MAX_PATH);
        fs::path p(exePathW);
        if (p.has_parent_path()) {
            compilerPath = p.parent_path();
        } else {
            compilerPath = fs::current_path();
        }
    }

    // Concatenate all source files
    // Strip "app" directives from non-first files (only first file keeps its app type)
    std::string combinedSource;
    bool firstFile = true;
    bool combinedIsLib = false;
    // Track which original file each combined line belongs to
    std::vector<std::string> lineSourceFile; // index = combined line (0-based)
    for (auto& file : sourceFiles) {
        std::string content = readFile(file.string());
        if (hasNoMain(content)) combinedIsLib = true;
        std::string fileStr = file.string();
        if (!firstFile) {
            combinedSource += "\n";
            lineSourceFile.push_back(fileStr);
            std::istringstream iss(content);
            std::string line;
            std::string filtered;
            bool firstLine = true;
            while (std::getline(iss, line)) {
                std::string trimmed = line;
                size_t s = trimmed.find_first_not_of(" \t");
                if (s != std::string::npos) trimmed = trimmed.substr(s);
                if (trimmed.find("app ") == 0 || trimmed.find("@import") == 0) continue;
                if (trimmed == "# [no_main]") continue;
                if (!firstLine) filtered += "\n";
                filtered += line;
                lineSourceFile.push_back(fileStr);
                firstLine = false;
            }
            combinedSource += filtered;
        } else {
            combinedSource += content;
            int lines = 0;
            for (char c : content) { if (c == '\n') lines++; }
            if (!content.empty() && content.back() != '\n') lines++;
            for (int i = 0; i < lines; i++) lineSourceFile.push_back(fileStr);
        }
        firstFile = false;
    }

    // Helper to find original file from combined line number
    auto findOriginalFile = [&](int combinedLine) -> std::string {
        if (combinedLine > 0 && combinedLine - 1 < (int)lineSourceFile.size())
            return lineSourceFile[combinedLine - 1];
        return "";
    };

    // Lex
    Lexer lexer(combinedSource);
    std::vector<Token> tokens;
    try {
        tokens = lexer.all();
    } catch (const std::exception& e) {
        std::cerr << "Lexer error: " << e.what() << std::endl;
        return 1;
    }

    // Check for lexer errors
    for (auto& t : tokens) {
        if (t.kind == TokenKind::Error) {
            std::string f = findOriginalFile(t.line);
            std::cerr << "Lexer error at line " << t.line;
            if (!f.empty()) std::cerr << " in " << f;
            std::cerr << ": " << t.text << std::endl;
            return 1;
        }
    }

    // Parse
    Parser parser(tokens);
    Program prog;
    try {
        prog = parser.parse();
    } catch (const std::exception& e) {
        std::cerr << "Parser error: " << e.what() << std::endl;
        return 1;
    }

    if (prog.functions.empty()) {
        std::cerr << "Error: no functions found in source" << std::endl;
        return 1;
    }

    // Optimize: remove unused functions and globals
    Optimizer optimizer;
    OptResult optResult = optimizer.optimize(prog);
    for (auto& w : optResult.warnings) {
        std::cerr << w << std::endl;
    }
    if (optResult.removedFunctions > 0 || optResult.removedGlobals > 0) {
        std::cout << "Optimized: removed " << optResult.removedFunctions << " function(s), "
                  << optResult.removedGlobals << " global(s)" << std::endl;
    }

    // Generate code
    prog.isLibrary = combinedIsLib;
    Codegen codegen(prog);
    codegen.setCompilerDir(compilerPath);
    codegen.isLibrary = combinedIsLib;
    try {
        codegen.generate(outputFile);
    } catch (const std::exception& e) {
        std::cerr << "Codegen error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Build successful: " << outputFile << std::endl;

    return 0;
}

int main(int argc, char* argv[]) {
    argv0 = argv[0];
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string arg1 = argv[1];

    // zenith new [--lib] <name>
    if (arg1 == "new") {
        bool libMode = false;
        std::string name;
        for (int i = 2; i < argc; i++) {
            std::string a = argv[i];
            if (a == "--lib") libMode = true;
            else if (!a.empty() && a[0] != '-') {
                if (!name.empty()) {
                    std::cerr << "Warning: extra argument '" << a << "' ignored" << std::endl;
                } else {
                    name = a;
                }
            }
        }
        return cmdNew(name, libMode);
    }

    // zenith build [--lib]
    if (arg1 == "build") {
        bool libMode = false;
        for (int i = 2; i < argc; i++) {
            if (std::string(argv[i]) == "--lib") libMode = true;
        }
        try {
            return cmdBuild(libMode);
        } catch (const std::exception& e) {
            std::cerr << "Build error: " << e.what() << std::endl;
            return 1;
        }
    }

    // zenith <input.z> -o <output> [--lib]  (legacy single-file mode)
    std::string inputFile;
    std::string outputFile = "a.exe";
    bool libMode = false;
    bool libsMode = false;
    bool embedMode = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--version" || arg == "-v") {
            printVersion();
            return 0;
        }
        if (arg == "--lib") {
            libMode = true;
        } else if (arg == "--libs") {
            libsMode = true;
        } else if (arg == "--embed") {
            embedMode = true;
        } else if (arg == "--efi") {
            // handled below
        } else if (arg == "-o" && i + 1 < argc) {
            outputFile = argv[++i];
        } else if (arg == "-o") {
            std::cerr << "Error: -o requires an output filename" << std::endl;
            return 1;
        } else if (!arg.empty() && arg[0] != '-') {
            if (!inputFile.empty()) {
                std::cerr << "Warning: extra input file '" << arg << "' ignored" << std::endl;
            } else {
                inputFile = arg;
            }
        }
    }

    if (inputFile.empty()) {
        std::cerr << "Error: no input file specified" << std::endl;
        printUsage();
        return 1;
    }

    // --libs: compile DLL into <exe_dir>/libs/<basename>.dll
    fs::path exeDir;
    {
        wchar_t exePathW[MAX_PATH] = {0};
        GetModuleFileNameW(NULL, exePathW, MAX_PATH);
        fs::path p(exePathW);
        exeDir = p.has_parent_path() ? p.parent_path() : fs::current_path();
    }

    if (libsMode) {
        libMode = true;
        fs::path libsDir = exeDir / L"libs";
        fs::create_directories(libsDir);
        // Extract basename from input file using wide API to avoid encoding issues
        std::wstring wInput;
        {
            // Convert input to wide using ACP (system codepage)
            int wlen = MultiByteToWideChar(CP_ACP, 0, inputFile.c_str(), -1, NULL, 0);
            if (wlen > 0) {
                wInput.resize(wlen - 1);
                MultiByteToWideChar(CP_ACP, 0, inputFile.c_str(), -1, &wInput[0], wlen);
            } else {
                // Fallback: try UTF-8
                wlen = MultiByteToWideChar(CP_UTF8, 0, inputFile.c_str(), -1, NULL, 0);
                if (wlen > 0) {
                    wInput.resize(wlen - 1);
                    MultiByteToWideChar(CP_UTF8, 0, inputFile.c_str(), -1, &wInput[0], wlen);
                } else {
                    wInput = L"output";
                }
            }
        }
        // Extract basename from wide path
        fs::path wSrcPath(wInput);
        std::wstring wBase = wSrcPath.stem().wstring();
        fs::path outPath = libsDir / (wBase + L".dll");
        // Convert output path to narrow using ACP (system codepage for std::ofstream)
        int ulen = WideCharToMultiByte(CP_ACP, 0, outPath.c_str(), -1, NULL, 0, NULL, NULL);
        if (ulen > 1) {
            outputFile.resize(ulen - 1);
            WideCharToMultiByte(CP_ACP, 0, outPath.c_str(), -1, &outputFile[0], ulen, NULL, NULL);
        }
    }

    // Read source
    std::string source = readFile(inputFile);

    // Detect [no_main] before lexing
    bool sourceIsLib = hasNoMain(source);

    // Lex
    Lexer lexer(source);
    std::vector<Token> tokens;
    try {
        tokens = lexer.all();
    } catch (const std::exception& e) {
        std::cerr << "Lexer error: " << e.what() << std::endl;
        return 1;
    }

    // Check for lexer errors
    for (auto& t : tokens) {
        if (t.kind == TokenKind::Error) {
            std::cerr << "Lexer error at line " << t.line << ": " << t.text << std::endl;
            return 1;
        }
    }

    // Parse
    Parser parser(tokens);
    Program prog;
    try {
        prog = parser.parse();
    } catch (const std::exception& e) {
        std::cerr << "Parser error: " << e.what() << std::endl;
        return 1;
    }

    prog.isLibrary = sourceIsLib || libMode;

    if (prog.functions.empty()) {
        std::cerr << "Error: no functions found in source" << std::endl;
        return 1;
    }

    // Optimize: remove unused functions and globals
    Optimizer optimizer;
    OptResult optResult = optimizer.optimize(prog);
    for (auto& w : optResult.warnings) {
        std::cerr << w << std::endl;
    }
    if (optResult.removedFunctions > 0 || optResult.removedGlobals > 0) {
        std::cout << "Optimized: removed " << optResult.removedFunctions << " function(s), "
                  << optResult.removedGlobals << " global(s)" << std::endl;
    }

    // Generate code
    Codegen codegen(prog);
    codegen.setCompilerDir(exeDir);
    codegen.isLibrary = sourceIsLib || libMode;
    codegen.libOutput = libMode;
    codegen.embedDLLs = embedMode;
    try {
        codegen.generate(outputFile);
    } catch (const std::exception& e) {
        std::cerr << "Codegen error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
