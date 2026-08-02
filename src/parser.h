#pragma once
#include "ast.h"
#include "lexer.h"
#include <memory>
#include <string>
#include <unordered_map>

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens);
    Program parse();

private:
    Token peek() const;
    Token peekNext() const;
    Token previous() const;
    Token advance();
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    Token consume(TokenKind kind, const std::string& msg);

    std::unique_ptr<FunctionDecl> parseFunction();
    std::unique_ptr<FunctionDecl> parseExternFunc();
    std::unique_ptr<StructDecl> parseStruct();
    std::unique_ptr<VarDecl> parseVarDecl();
    std::unique_ptr<Stmt> parseStatement();
    std::unique_ptr<Stmt> parseIf();
    std::unique_ptr<Stmt> parseWhile();
    std::unique_ptr<Stmt> parseFor();
    std::unique_ptr<Expr> parseExpression();
    std::unique_ptr<Expr> parseLogicalOr();
    std::unique_ptr<Expr> parseLogicalAnd();
    std::unique_ptr<Expr> parseComparison();
    std::unique_ptr<Expr> parseTerm();
    std::unique_ptr<Expr> parseUnary();
    std::unique_ptr<Expr> parseFactor();
    std::unique_ptr<Expr> parsePrimary();
    std::unique_ptr<Expr> parseCallOrIdent();
    std::unique_ptr<Expr> parseDotChain(std::unique_ptr<Expr> left);
    std::string readAsmOperand();
    void parseAppType(Program& prog);
    Block parseBlock(TokenKind terminator = TokenKind::End);
    Type parseType();
    Param parseParam();
    void skipToSyncPoint();

    const std::vector<Token>& tokens;
    size_t pos;
    AppType appType = AppType::Console;
    Mode mode = Mode::Hard;
};
