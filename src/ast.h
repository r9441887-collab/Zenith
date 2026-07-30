#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

enum class TypeKind { Int, Float, Bool, Void, String, Vec2, Vec3, Color, Entity, Struct };

enum class AppType { Console, GUI, EFI, Bare };
enum class RenderType { Software, DX11 };
enum class AddressSpace { Virtual, Physical };
enum class Mode { Easy, Hard };

struct Type {
    TypeKind kind = TypeKind::Void;
    std::string structName;
    bool isPtr = false;
    AddressSpace addrSpace = AddressSpace::Virtual;
};

struct Node {
    virtual ~Node() = default;
};

struct Expr : Node {};
struct Stmt : Node {};

struct NumberExpr : Expr {
    int64_t value = 0;
};

struct FloatExpr : Expr {
    double value = 0.0;
};

struct IdentExpr : Expr {
    std::string name;
};

struct MemberExpr : Expr {
    std::unique_ptr<Expr> object;
    std::string member;
};

struct BinaryExpr : Expr {
    std::unique_ptr<Expr> left;
    std::string op;
    std::unique_ptr<Expr> right;
};

struct DerefExpr : Expr {
    std::unique_ptr<Expr> ptr;
};

struct AddressOfExpr : Expr {
    std::string name;
};

struct UnaryExpr : Expr {
    std::string op;
    std::unique_ptr<Expr> operand;
};

struct ArrayAccessExpr : Expr {
    std::unique_ptr<Expr> array;
    std::unique_ptr<Expr> index;
};

struct CallExpr : Expr {
    std::string name;
    std::vector<std::unique_ptr<Expr>> args;
    std::unique_ptr<Expr> receiver;
};

struct StringExpr : Expr {
    std::string value;
};

struct Param {
    std::string name;
    Type type;
};

struct Block {
    std::vector<std::unique_ptr<Stmt>> stmts;
};

struct VarDecl : Stmt {
    std::string name;
    Type type;
    std::unique_ptr<Expr> init;
    int arraySize = 0;
};

struct ReturnStmt : Stmt {
    std::unique_ptr<Expr> value;
};

struct ExprStmt : Stmt {
    std::unique_ptr<Expr> expr;
};

struct AssignStmt : Stmt {
    std::string name;
    std::vector<std::string> memberPath;
    std::unique_ptr<Expr> indexExpr;
    std::unique_ptr<Expr> value;
};

struct PtrAssignStmt : Stmt {
    std::unique_ptr<Expr> ptr;
    std::unique_ptr<Expr> value;
};

struct IfStmt : Stmt {
    std::unique_ptr<Expr> condition;
    Block thenBlock;
    Block elseBlock;
};

struct WhileStmt : Stmt {
    std::unique_ptr<Expr> condition;
    Block body;
};

struct AsmInstr {
    std::string mnemonic;
    std::string op1;
    std::string op2;
};

struct AsmStmt : Stmt {
    std::vector<AsmInstr> instrs;
};

struct ForStmt : Stmt {
    std::string varName;
    std::unique_ptr<Expr> start;
    std::unique_ptr<Expr> end;
    std::unique_ptr<Expr> step;   // optional, nullptr means step=1
    Block body;
};

struct FunctionDecl : Node {
    std::string name;
    std::vector<Param> params;
    Type returnType;
    Block body;
    bool isExtern = false;
    std::string dllName;
};

struct StructField {
    std::string name;
    Type type;
};

struct StructDecl : Node {
    std::string name;
    std::vector<StructField> fields;
};

struct ImportDecl {
    std::string dllName;
    std::string module;  // e.g. "thread" from @import("libs.dll::thread")
};

struct Program {
    std::vector<std::unique_ptr<FunctionDecl>> functions;
    std::vector<std::unique_ptr<VarDecl>> globals;
    std::vector<ImportDecl> imports;
    std::vector<std::unique_ptr<StructDecl>> structs;
    AppType appType = AppType::Console;
    RenderType renderType = RenderType::Software;
    Mode mode = Mode::Hard;
    bool isLibrary = false;  // true if # [no_main] is present
};
