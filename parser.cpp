#include "parser.h"
#include <iostream>
#include <filesystem>

Parser::Parser(const std::vector<Token>& tokens) : tokens(tokens), pos(0) {}

Token Parser::peek() const { if (tokens.empty() || pos >= tokens.size()) return {TokenKind::Eof, "", 0, 0.0, 0, 0}; return tokens[pos]; }
Token Parser::peekNext() const {
    if (pos + 1 < tokens.size()) return tokens[pos + 1];
    return Token{TokenKind::Eof, "", 0, 0.0, 0, 0};
}
Token Parser::previous() const { if (pos > 0) return tokens[pos - 1]; return {TokenKind::Error, "", 0, 0.0, 0, 0}; }
Token Parser::advance() { if (tokens.empty() || pos >= tokens.size()) return {TokenKind::Eof, "", 0, 0.0, 0, 0}; Token t = tokens[pos]; if (!check(TokenKind::Eof)) pos++; return t; }
bool Parser::check(TokenKind kind) const { return peek().kind == kind; }

bool Parser::match(TokenKind kind) {
    if (check(kind)) { advance(); return true; }
    return false;
}

Token Parser::consume(TokenKind kind, const std::string& msg) {
    if (check(kind)) return advance();
    std::cerr << "Error at line " << peek().line << ": " << msg << std::endl;
    throw std::runtime_error(msg);
}

Type Parser::parseType() {
    // phys/virt qualifiers
    AddressSpace addrSpace = AddressSpace::Virtual;
    if (check(TokenKind::Virt)) { advance(); }
    else if (check(TokenKind::Phys)) {
        advance(); addrSpace = AddressSpace::Physical;
        if (mode == Mode::Easy) { throw std::runtime_error("phys not in easy mode"); }
        if (appType != AppType::EFI && appType != AppType::Bare)
            throw std::runtime_error("phys requires EFI/bare");
    }
    // ptr<T> handling (accepts both `ptr T` and `ptr<T>`)
    if (check(TokenKind::Ptr)) {
        advance(); Type inner;
        bool angled = false;
        if (check(TokenKind::Lt)) { advance(); angled = true; }
        if (check(TokenKind::TypeInt))    { advance(); inner = {TypeKind::Int}; }
        else if (check(TokenKind::TypeFloat))  { advance(); inner = {TypeKind::Float}; }
        else if (check(TokenKind::TypeBool))   { advance(); inner = {TypeKind::Bool}; }
        else if (check(TokenKind::TypeString)) { advance(); inner = {TypeKind::String}; }
        else if (check(TokenKind::TypeVoid))   { advance(); inner = {TypeKind::Void}; }
        else if (check(TokenKind::Ident)) { inner.kind=TypeKind::Struct; inner.structName=advance().text; }
        else throw std::runtime_error("Expected type after ptr");
        if (angled) consume(TokenKind::Gt, "Expected '>' after ptr<type>");
        inner.isPtr=true; inner.addrSpace=addrSpace; return inner;
    }
    switch (peek().kind) {
        case TokenKind::TypeInt:    advance(); return {TypeKind::Int};
        case TokenKind::TypeFloat:  advance(); return {TypeKind::Float};
        case TokenKind::TypeBool:   advance(); return {TypeKind::Bool};
        case TokenKind::TypeString: advance(); return {TypeKind::String};
        case TokenKind::TypeVoid:   advance(); return {TypeKind::Void};
        case TokenKind::TypeVec2:
            if (appType != AppType::GUI) {
                std::cerr << "Error at line " << peek().line << ": 'vec2' type is only available in GUI applications (add 'app gui' at top)\n";
                throw std::runtime_error("vec2 requires GUI");
            }
            advance(); return {TypeKind::Vec2};
        case TokenKind::TypeVec3:
            if (appType != AppType::GUI) {
                std::cerr << "Error at line " << peek().line << ": 'vec3' type is only available in GUI applications (add 'app gui' at top)\n";
                throw std::runtime_error("vec3 requires GUI");
            }
            advance(); return {TypeKind::Vec3};
        case TokenKind::TypeColor:
            if (appType != AppType::GUI) {
                std::cerr << "Error at line " << peek().line << ": 'color' type is only available in GUI applications (add 'app gui' at top)\n";
                throw std::runtime_error("color requires GUI");
            }
            advance(); return {TypeKind::Color};
        case TokenKind::TypeEntity: advance(); return {TypeKind::Entity};
        case TokenKind::Ident: {
            std::string typeName = advance().text;
            Type t;
            t.kind = TypeKind::Struct;
            t.structName = typeName;
            return t;
        }
        default:
            throw std::runtime_error("Expected type at line " + std::to_string(peek().line));
    }
}

Param Parser::parseParam() {
    Param p;
    p.name = consume(TokenKind::Ident, "Expected parameter name").text;
    consume(TokenKind::Colon, "Expected ':' after parameter name");
    p.type = parseType();
    return p;
}

void Parser::skipToSyncPoint() {
    while (!check(TokenKind::Eof)) {
        if (check(TokenKind::Newline)) { advance(); return; }
        if (check(TokenKind::End)) return;
        if (check(TokenKind::Else)) return;
        if (check(TokenKind::Func)) return;
        advance();
    }
}

Block Parser::parseBlock(TokenKind terminator) {
    Block block;
    while (!check(TokenKind::Eof) && !check(terminator) && !check(TokenKind::Func)) {
        if (check(TokenKind::Else)) break;
        if (check(TokenKind::Newline)) { advance(); continue; }
        if (check(TokenKind::Struct)) {
            std::cerr << "Error at line " << peek().line << ": 'struct' cannot be declared inside a block" << std::endl;
            parseStruct();
            continue;
        }
        block.stmts.push_back(parseStatement());
    }
    return block;
}

std::unique_ptr<FunctionDecl> Parser::parseFunction() {
    auto func = std::make_unique<FunctionDecl>();
    consume(TokenKind::Func, "Expected 'func'");
    func->name = consume(TokenKind::Ident, "Expected function name").text;
    consume(TokenKind::LParen, "Expected '('");

    if (!check(TokenKind::RParen)) {
        func->params.push_back(parseParam());
        while (match(TokenKind::Comma)) {
            func->params.push_back(parseParam());
        }
    }
    consume(TokenKind::RParen, "Expected ')'");

    if (match(TokenKind::Arrow)) {
        func->returnType = parseType();
    } else if (check(TokenKind::TypeInt) || check(TokenKind::TypeFloat) ||
               check(TokenKind::TypeBool) || check(TokenKind::TypeString) ||
               check(TokenKind::TypeVoid) || check(TokenKind::TypeVec2) ||
               check(TokenKind::TypeVec3) || check(TokenKind::TypeColor) ||
               check(TokenKind::TypeEntity) || check(TokenKind::Ident)) {
        func->returnType = parseType();
    } else {
        func->returnType = {TypeKind::Void};
    }

    if (check(TokenKind::Newline)) advance();
    else if (!check(TokenKind::Eof) && !check(TokenKind::End)) {
        consume(TokenKind::Newline, "Expected newline after function signature");
    }

    func->body = parseBlock(TokenKind::End);
    consume(TokenKind::End, "Expected 'end' after function body");
    if (check(TokenKind::Newline)) advance();
    return func;
}

std::unique_ptr<FunctionDecl> Parser::parseExternFunc() {
    consume(TokenKind::Extern, "Expected 'extern'");
    auto func = std::make_unique<FunctionDecl>();
    func->isExtern = true;
    consume(TokenKind::Func, "Expected 'func' after 'extern'");
    func->name = consume(TokenKind::Ident, "Expected function name").text;
    consume(TokenKind::LParen, "Expected '('");

    if (!check(TokenKind::RParen)) {
        func->params.push_back(parseParam());
        while (match(TokenKind::Comma)) {
            func->params.push_back(parseParam());
        }
    }
    consume(TokenKind::RParen, "Expected ')'");

    // Return type: accept "-> type" or just "type" directly
    if (match(TokenKind::Arrow)) {
        func->returnType = parseType();
    } else if (check(TokenKind::TypeInt) || check(TokenKind::TypeFloat) ||
               check(TokenKind::TypeBool) || check(TokenKind::TypeString) ||
               check(TokenKind::TypeVoid) || check(TokenKind::TypeVec2) ||
               check(TokenKind::TypeVec3) || check(TokenKind::TypeColor) ||
               check(TokenKind::TypeEntity) || check(TokenKind::Ident)) {
        func->returnType = parseType();
    } else {
        func->returnType = {TypeKind::Void};
    }

    // Optional: from "dll.dll"
    if (check(TokenKind::From)) {
        advance();
        Token dllTok = consume(TokenKind::StringLit, "Expected DLL name after 'from'");
        func->dllName = dllTok.text;
    }

    if (check(TokenKind::Newline)) advance();
    return func;
}

std::unique_ptr<StructDecl> Parser::parseStruct() {
    auto sd = std::make_unique<StructDecl>();
    consume(TokenKind::Struct, "Expected 'struct'");
    sd->name = consume(TokenKind::Ident, "Expected struct name").text;
    while (check(TokenKind::Newline)) advance();
    if (check(TokenKind::LBrace)) {
        advance();
        while (!check(TokenKind::Eof) && !check(TokenKind::RBrace)) {
            if (check(TokenKind::Newline)) { advance(); continue; }
            if (check(TokenKind::Var) || check(TokenKind::Let)) {
                advance();
                StructField f;
                f.name = consume(TokenKind::Ident, "Expected field name").text;
                consume(TokenKind::Colon, "Expected ':'");
                f.type = parseType();
                sd->fields.push_back(f);
                if (check(TokenKind::Newline)) advance();
            } else {
                std::cerr << "Error at line " << peek().line << ": expected 'var'/'let' keyword for struct field, or '}' to end struct" << std::endl;
                break;
            }
        }
        consume(TokenKind::RBrace, "Expected '}'");
    } else {
        while (!check(TokenKind::Eof) && !check(TokenKind::End)) {
            if (check(TokenKind::Newline)) { advance(); continue; }
    if (check(TokenKind::Var) || check(TokenKind::Let)) {
        advance();
        StructField f;
        f.name = consume(TokenKind::Ident, "Expected field name").text;
        consume(TokenKind::Colon, "Expected ':'");
        f.type = parseType();
        sd->fields.push_back(f);
                if (check(TokenKind::Newline)) advance();
            } else {
                break;
            }
        }
        consume(TokenKind::End, "Expected 'end' after struct");
    }
    if (check(TokenKind::Newline)) advance();
    return sd;
}

std::unique_ptr<VarDecl> Parser::parseVarDecl() {
    auto vd = std::make_unique<VarDecl>();
    if (!check(TokenKind::Var) && !check(TokenKind::Let) && !check(TokenKind::Const)) {
        std::cerr << "Error at line " << peek().line << ": Expected 'var', 'let' or 'const'" << std::endl;
        throw std::runtime_error("Expected 'var', 'let' or 'const'");
    }
    advance();
    vd->name = consume(TokenKind::Ident, "Expected variable name").text;
    consume(TokenKind::Colon, "Expected ':'");

    if (check(TokenKind::LBrack)) {
        advance();
        vd->arraySize = (int)consume(TokenKind::Number, "Expected array size").intVal;
        if (vd->arraySize <= 0) {
            std::cerr << "Error at line " << previous().line << ": array size must be positive" << std::endl;
            throw std::runtime_error("Invalid array size");
        }
        consume(TokenKind::RBrack, "Expected ']'");
    }

    vd->type = parseType();

    if (match(TokenKind::Eq)) {
        vd->init = parseExpression();
    }

    if (check(TokenKind::Newline)) advance();
    return vd;
}

std::unique_ptr<Stmt> Parser::parseStatement() {
    if (check(TokenKind::Var) || check(TokenKind::Let) || check(TokenKind::Const)) return parseVarDecl();
    if (check(TokenKind::If)) return parseIf();
    if (check(TokenKind::While)) return parseWhile();
    if (check(TokenKind::Loop)) return parseLoop();
    if (check(TokenKind::Switch)) return parseSwitch();
    if (check(TokenKind::Break)) { advance(); if (check(TokenKind::Newline)) advance(); return std::make_unique<BreakStmt>(); }
    if (check(TokenKind::Continue)) { advance(); if (check(TokenKind::Newline)) advance(); return std::make_unique<ContinueStmt>(); }
    if (check(TokenKind::For)) return parseFor();
    if (check(TokenKind::Asm)) {
        advance();
        if (mode == Mode::Easy) { throw std::runtime_error("asm not in easy mode"); }
        if (appType != AppType::EFI && appType != AppType::Bare)
            throw std::runtime_error("asm requires EFI or bare");
        consume(TokenKind::LBrace, "Expected '{' after 'asm'");
        auto stmt = std::make_unique<AsmStmt>();
        while (!check(TokenKind::Eof) && !check(TokenKind::RBrace)) {
            if (check(TokenKind::Newline)) { advance(); continue; }
            AsmInstr instr;
            if (check(TokenKind::Ident) || check(TokenKind::TypeInt)) {
                instr.mnemonic = advance().text;
            } else {
                consume(TokenKind::Ident, "Expected mnemonic");
            }
            for (auto& c : instr.mnemonic) c = (char)tolower((unsigned char)c);
            if (!check(TokenKind::Newline) && !check(TokenKind::RBrace)) {
                instr.op1 = readAsmOperand();
                if (check(TokenKind::Comma)) {
                    advance();
                    instr.op2 = readAsmOperand();
                }
            }
            stmt->instrs.push_back(std::move(instr));
            if (check(TokenKind::Newline)) advance();
        }
        consume(TokenKind::RBrace, "Expected '}' after asm");
        if (check(TokenKind::Newline)) advance();
        return stmt;
    }
    if (check(TokenKind::Return)) {
        advance();
        auto stmt = std::make_unique<ReturnStmt>();
        if (!check(TokenKind::Newline) && !check(TokenKind::End) &&
            !check(TokenKind::Else) && !check(TokenKind::Eof)) {
            stmt->value = parseExpression();
        }
        if (check(TokenKind::Newline)) advance();
        return stmt;
    }
    if (check(TokenKind::Ident) && peekNext().kind == TokenKind::Eq) {
        auto stmt = std::make_unique<AssignStmt>();
        stmt->name = advance().text;
        advance();
        stmt->value = parseExpression();
        if (check(TokenKind::Newline)) advance();
        return stmt;
    }
    if (check(TokenKind::Ident) && peekNext().kind == TokenKind::LBrack) {
        auto stmt = std::make_unique<AssignStmt>();
        stmt->name = advance().text;
        advance();
        stmt->indexExpr = parseExpression();
        consume(TokenKind::RBrack, "Expected ']'");
        consume(TokenKind::Eq, "Expected '='");
        stmt->value = parseExpression();
        if (check(TokenKind::Newline)) advance();
        return stmt;
    }
    if (check(TokenKind::Ident) && peekNext().kind == TokenKind::Dot) {
        size_t scan = pos + 2;
        while (scan + 1 < tokens.size() &&
               tokens[scan].kind == TokenKind::Ident &&
               tokens[scan + 1].kind == TokenKind::Dot) {
            scan += 2;
        }
        if (scan < tokens.size() &&
            tokens[scan].kind == TokenKind::Ident &&
            scan + 1 < tokens.size() &&
            tokens[scan + 1].kind == TokenKind::Eq) {
            std::string firstName = advance().text;
            auto stmt = std::make_unique<AssignStmt>();
            stmt->name = firstName;
            while (check(TokenKind::Dot)) {
                advance();
                stmt->memberPath.push_back(
                    consume(TokenKind::Ident, "Expected field name").text);
            }
            consume(TokenKind::Eq, "Expected '='");
            stmt->value = parseExpression();
            if (check(TokenKind::Newline)) advance();
            return stmt;
        }
    }
    // *ptr = value (pointer assignment)
    if (check(TokenKind::Star)) {
        advance();
        auto stmt = std::make_unique<PtrAssignStmt>();
        stmt->ptr = parseUnary();
        consume(TokenKind::Eq, "Expected '=' after pointer expression");
        stmt->value = parseExpression();
        if (check(TokenKind::Newline)) advance();
        return stmt;
    }

    auto stmt = std::make_unique<ExprStmt>();
    stmt->expr = parseExpression();
    if (check(TokenKind::Newline)) advance();
    return stmt;
}

std::string Parser::readAsmOperand() {
    std::string result;
    if (check(TokenKind::LBrack)) {
        std::vector<std::string> parts;
        parts.push_back(advance().text);
        int depth = 1;
        while (depth > 0) {
            if (check(TokenKind::Eof) || check(TokenKind::Newline)) break;
            if (check(TokenKind::RBrack)) { parts.push_back(advance().text); depth--; continue; }
            if (check(TokenKind::LBrack)) depth++;
            parts.push_back(advance().text);
        }
        for (size_t i = 0; i < parts.size(); i++) {
            if (i) result += ' ';
            result += parts[i];
        }
        return result;
    }
    if (check(TokenKind::Minus)) {
        result = advance().text;
        if (check(TokenKind::Number) || check(TokenKind::Ident)) result += advance().text;
        return result;
    }
    if (check(TokenKind::Ident) || check(TokenKind::Number)) {
        return advance().text;
    }
    return "";
}

std::unique_ptr<Stmt> Parser::parseIf() {
    advance();
    auto stmt = std::make_unique<IfStmt>();
    stmt->condition = parseExpression();
    if (check(TokenKind::Newline)) advance();

    stmt->thenBlock = parseBlock(TokenKind::End);

    if (check(TokenKind::Else)) {
        advance();
        if (check(TokenKind::Newline)) advance();
        if (check(TokenKind::If)) {
            Block elseIfBlock;
            elseIfBlock.stmts.push_back(parseIf());
            stmt->elseBlock = std::move(elseIfBlock);
            return stmt;
        }
        stmt->elseBlock = parseBlock(TokenKind::End);
    }

    consume(TokenKind::End, "Expected 'end' after if");
    if (check(TokenKind::Newline)) advance();
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseWhile() {
    advance();
    auto stmt = std::make_unique<WhileStmt>();
    stmt->condition = parseExpression();
    if (check(TokenKind::Newline)) advance();

    stmt->body = parseBlock(TokenKind::End);
    consume(TokenKind::End, "Expected 'end' after while");
    if (check(TokenKind::Newline)) advance();
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseLoop() {
    advance(); // 'loop'
    auto stmt = std::make_unique<LoopStmt>();
    if (check(TokenKind::Newline)) advance();
    stmt->body = parseBlock(TokenKind::End);
    consume(TokenKind::End, "Expected 'end' after loop");
    if (check(TokenKind::Newline)) advance();
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseSwitch() {
    advance(); // 'switch'
    auto stmt = std::make_unique<SwitchStmt>();
    stmt->condition = parseExpression();
    if (check(TokenKind::Newline)) advance();
    while (check(TokenKind::Case)) {
        advance();
        SwitchCase sc;
        if (!check(TokenKind::Newline) && !check(TokenKind::Colon)) {
            sc.condition = parseExpression();
        }
        if (check(TokenKind::Colon)) advance();
        if (check(TokenKind::Newline)) advance();
        while (!check(TokenKind::Eof) && !check(TokenKind::End) && !check(TokenKind::Case)) {
            if (check(TokenKind::Newline)) { advance(); continue; }
            sc.body.stmts.push_back(parseStatement());
        }
        stmt->cases.push_back(std::move(sc));
    }
    consume(TokenKind::End, "Expected 'end' after switch");
    if (check(TokenKind::Newline)) advance();
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseFor() {
    advance(); // consume 'for'
    auto stmt = std::make_unique<ForStmt>();
    stmt->varName = consume(TokenKind::Ident, "Expected loop variable name").text;
    consume(TokenKind::Eq, "Expected '=' after loop variable");
    stmt->start = parseExpression();
    consume(TokenKind::Comma, "Expected ',' after start value");
    stmt->end = parseExpression();
    if (match(TokenKind::Comma)) {
        stmt->step = parseExpression();
    }
    if (check(TokenKind::Newline)) advance();
    stmt->body = parseBlock(TokenKind::End);
    consume(TokenKind::End, "Expected 'end' after for");
    if (check(TokenKind::Newline)) advance();
    return stmt;
}

std::unique_ptr<Expr> Parser::parseExpression() {
    return parseLogicalOr();
}

std::unique_ptr<Expr> Parser::parseLogicalOr() {
    auto left = parseLogicalAnd();
    while (check(TokenKind::PipePipe)) {
        auto op = advance();
        auto right = parseLogicalAnd();
        auto bin = std::make_unique<BinaryExpr>();
        bin->left = std::move(left);
        bin->op = op.text;
        bin->right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseLogicalAnd() {
    auto left = parseBitOr();
    while (check(TokenKind::AmpAmp)) {
        auto op = advance();
        auto right = parseBitOr();
        auto bin = std::make_unique<BinaryExpr>();
        bin->left = std::move(left);
        bin->op = op.text;
        bin->right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseBitOr() {
    auto left = parseBitXor();
    while (check(TokenKind::Pipe)) {
        auto op = advance();
        auto right = parseBitXor();
        auto bin = std::make_unique<BinaryExpr>();
        bin->left = std::move(left);
        bin->op = op.text;
        bin->right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseBitXor() {
    auto left = parseBitAnd();
    while (check(TokenKind::Caret)) {
        auto op = advance();
        auto right = parseBitAnd();
        auto bin = std::make_unique<BinaryExpr>();
        bin->left = std::move(left);
        bin->op = op.text;
        bin->right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseBitAnd() {
    auto left = parseComparison();
    while (check(TokenKind::Amp)) {
        auto op = advance();
        auto right = parseComparison();
        auto bin = std::make_unique<BinaryExpr>();
        bin->left = std::move(left);
        bin->op = op.text;
        bin->right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseComparison() {
    auto left = parseShift();
    while (check(TokenKind::EqEq) || check(TokenKind::NotEq) ||
           check(TokenKind::Lt) || check(TokenKind::Gt) ||
           check(TokenKind::LtEq) || check(TokenKind::GtEq)) {
        auto op = advance();
        auto right = parseShift();
        auto bin = std::make_unique<BinaryExpr>();
        bin->left = std::move(left);
        bin->op = op.text;
        bin->right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseShift() {
    auto left = parseTerm();
    while (check(TokenKind::ShiftLeft) || check(TokenKind::ShiftRight)) {
        auto op = advance();
        auto right = parseTerm();
        auto bin = std::make_unique<BinaryExpr>();
        bin->left = std::move(left);
        bin->op = op.text;
        bin->right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseTerm() {
    auto left = parseFactor();
    while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
        auto op = advance();
        auto right = parseFactor();
        auto bin = std::make_unique<BinaryExpr>();
        bin->left = std::move(left);
        bin->op = op.text;
        bin->right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseFactor() {
    auto left = parseUnary();
    while (check(TokenKind::Star) || check(TokenKind::Slash) || check(TokenKind::Percent)) {
        auto op = advance();
        auto right = parseUnary();
        auto bin = std::make_unique<BinaryExpr>();
        bin->left = std::move(left);
        bin->op = op.text;
        bin->right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseUnary() {
    if (check(TokenKind::Star)) { advance(); auto d = std::make_unique<DerefExpr>(); d->ptr = parseUnary(); return d; }
    if (check(TokenKind::Amp)) { advance(); auto a = std::make_unique<AddressOfExpr>(); a->name = consume(TokenKind::Ident, "Expected var").text; return a; }
    if (check(TokenKind::Bang)) { advance(); auto u = std::make_unique<UnaryExpr>(); u->op = "!"; u->operand = parseUnary(); return u; }
    if (check(TokenKind::Tilde)) { advance(); auto u = std::make_unique<UnaryExpr>(); u->op = "~"; u->operand = parseUnary(); return u; }
    if (check(TokenKind::Minus)) {
        advance();
        if (check(TokenKind::Number)) { auto n = std::make_unique<NumberExpr>(); n->value = -advance().intVal; return n; }
        if (check(TokenKind::FloatLit)) { auto f = std::make_unique<FloatExpr>(); f->value = -advance().floatVal; return f; }
        auto u = std::make_unique<UnaryExpr>(); u->op = "-"; u->operand = parseUnary(); return u;
    }
    if (check(TokenKind::Plus)) { advance(); return parseUnary(); }
    return parsePrimary();
}

std::unique_ptr<Expr> Parser::parsePrimary() {
    std::unique_ptr<Expr> expr;
    if (check(TokenKind::Number)) {
        auto n = std::make_unique<NumberExpr>();
        n->value = advance().intVal;
        expr = std::move(n);
    } else if (check(TokenKind::FloatLit)) {
        auto f = std::make_unique<FloatExpr>();
        f->value = advance().floatVal;
        expr = std::move(f);
    } else if (check(TokenKind::StringLit)) {
        auto s = std::make_unique<StringExpr>();
        s->value = advance().text;
        expr = std::move(s);
    } else if (check(TokenKind::LParen)) {
        advance();
        expr = parseExpression();
        consume(TokenKind::RParen, "Expected ')'");
    } else if (check(TokenKind::Ident)) {
        std::string idName = peek().text;
        if (idName == "Red" || idName == "Green" || idName == "Blue" ||
            idName == "White" || idName == "Black" || idName == "Yellow" ||
            idName == "Cyan" || idName == "Magenta" || idName == "Gray") {
            advance();
            uint32_t c;
            if (idName == "Red")     c = 0xFFFF0000;
            else if (idName == "Green")  c = 0xFF00FF00;
            else if (idName == "Blue")   c = 0xFF0000FF;
            else if (idName == "White")  c = 0xFFFFFFFF;
            else if (idName == "Black")  c = 0xFF000000;
            else if (idName == "Yellow") c = 0xFFFFFF00;
            else if (idName == "Cyan")   c = 0xFF00FFFF;
            else if (idName == "Magenta")c = 0xFFFF00FF;
            else                         c = 0xFF808080;
            auto n = std::make_unique<NumberExpr>();
            n->value = c;
            expr = std::move(n);
        } else {
            expr = parseCallOrIdent();
        }
    } else if (check(TokenKind::True)) {
        advance();
        auto n = std::make_unique<NumberExpr>();
        n->value = 1;
        expr = std::move(n);
    } else if (check(TokenKind::False)) {
        advance();
        auto n = std::make_unique<NumberExpr>();
        n->value = 0;
        expr = std::move(n);
    } else if (check(TokenKind::TypeVec2) || check(TokenKind::TypeVec3) || check(TokenKind::TypeColor)) {
        if (appType != AppType::GUI) {
            std::cerr << "Error at line " << peek().line << ": '" << peek().text << "' type is only available in GUI applications (add 'app gui' at top)\n";
            throw std::runtime_error(std::string(peek().text) + " requires GUI");
        }
        std::string typeName = advance().text;
        if (check(TokenKind::LParen)) {
            advance();
            auto call = std::make_unique<CallExpr>();
            call->name = typeName;
            while (check(TokenKind::Newline)) advance();
            if (!check(TokenKind::RParen)) {
                call->args.push_back(parseExpression());
                while (match(TokenKind::Comma)) {
                    while (check(TokenKind::Newline)) advance();
                    call->args.push_back(parseExpression());
                }
            }
            while (check(TokenKind::Newline)) advance();
            consume(TokenKind::RParen, "Expected ')'");
            expr = std::move(call);
        } else {
            auto id = std::make_unique<IdentExpr>();
            id->name = typeName;
            expr = std::move(id);
        }
    } else {
        throw std::runtime_error("Unexpected token '" + peek().text + "' at line " + std::to_string(peek().line));
    }

    while (check(TokenKind::LBrack) || check(TokenKind::Dot)) {
        if (check(TokenKind::LBrack)) {
            advance();
            auto arr = std::make_unique<ArrayAccessExpr>();
            arr->array = std::move(expr);
            arr->index = parseExpression();
            consume(TokenKind::RBrack, "Expected ']'");
            expr = std::move(arr);
        } else {
            advance();
            auto memb = std::make_unique<MemberExpr>();
            memb->object = std::move(expr);
            memb->member = consume(TokenKind::Ident, "Expected field name after '.'").text;
            if (check(TokenKind::LParen)) {
                advance();
                auto call = std::make_unique<CallExpr>();
                call->name = memb->member;
                call->receiver = std::move(memb);
                if (!check(TokenKind::RParen)) {
                    call->args.push_back(parseExpression());
                    while (match(TokenKind::Comma)) {
                        call->args.push_back(parseExpression());
                    }
                }
                consume(TokenKind::RParen, "Expected ')'");
                expr = std::move(call);
            } else {
                expr = std::move(memb);
            }
        }
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseCallOrIdent() {
    std::string name = advance().text;
    if (check(TokenKind::LParen)) {
        advance();
        auto call = std::make_unique<CallExpr>();
        call->name = name;
        while (check(TokenKind::Newline)) advance();
        if (!check(TokenKind::RParen)) {
            call->args.push_back(parseExpression());
            while (match(TokenKind::Comma)) {
                while (check(TokenKind::Newline)) advance();
                call->args.push_back(parseExpression());
            }
        }
        while (check(TokenKind::Newline)) advance();
        consume(TokenKind::RParen, "Expected ')'");
        return parseDotChain(std::move(call));
    }
    auto id = std::make_unique<IdentExpr>();
    id->name = name;
    return parseDotChain(std::move(id));
}

std::unique_ptr<Expr> Parser::parseDotChain(std::unique_ptr<Expr> left) {
    while (check(TokenKind::Dot)) {
        advance();
        auto memb = std::make_unique<MemberExpr>();
        memb->object = std::move(left);
        memb->member = consume(TokenKind::Ident, "Expected field name after '.'").text;
        if (check(TokenKind::LParen)) {
            advance();
            auto call = std::make_unique<CallExpr>();
            call->name = memb->member;
            call->receiver = std::move(memb);
            while (check(TokenKind::Newline)) advance();
            if (!check(TokenKind::RParen)) {
                call->args.push_back(parseExpression());
                while (match(TokenKind::Comma)) {
                    while (check(TokenKind::Newline)) advance();
                    call->args.push_back(parseExpression());
                }
            }
            while (check(TokenKind::Newline)) advance();
            consume(TokenKind::RParen, "Expected ')'");
            left = std::move(call);
        } else {
            left = std::move(memb);
        }
    }
    return left;
}

void Parser::parseAppType(Program& prog) {
    consume(TokenKind::App, "Expected 'app' directive at top of file");
    if (check(TokenKind::Ident)) {
        std::string type = advance().text;
        if (type == "gui") {
            prog.appType = AppType::GUI;
            prog.renderType = RenderType::Software;  // default
            prog.appCategory = AppCategory::Tool;    // overwritten if 'tool'/'game' keyword present
            // Optional keywords in any order: 'sr'/'dx' (render type), 'tool'/'game' (category)
            bool sawCategory = false;
            while (check(TokenKind::Ident)) {
                std::string kw = peek().text;
                if (kw == "sr") {
                    advance();
                    prog.renderType = RenderType::Software;
                } else if (kw == "dx") {
                    advance();
                    prog.renderType = RenderType::DX11;
                } else if (kw == "tool") {
                    advance();
                    prog.appCategory = AppCategory::Tool;
                    sawCategory = true;
                } else if (kw == "game") {
                    advance();
                    prog.appCategory = AppCategory::Game;
                    sawCategory = true;
                } else {
                    break;
                }
            }
            if (!sawCategory) {
                std::cerr << "Error at line " << previous().line << ": 'app gui' requires a category: use 'app gui tool' or 'app gui game'\n";
                throw std::runtime_error("Missing app gui category");
            }
        } else if (type == "console") {
            prog.appType = AppType::Console;
            prog.appCategory = AppCategory::Tool;
        } else if (type == "efi") {
            prog.appType = AppType::EFI;
        } else if (type == "bios") {
            prog.appType = AppType::BIOS;
        } else if (type == "bare") {
            prog.appType = AppType::Bare;
        } else {
            std::cerr << "Error at line " << previous().line << ": expected 'gui', 'console', 'efi', 'bios', or 'bare' after 'app', got '" << type << "'\n";
            throw std::runtime_error("Invalid app type");
        }
    } else {
        std::cerr << "Error at line " << peek().line << ": expected 'gui', 'console', 'efi', 'bios', or 'bare' after 'app'\n";
        throw std::runtime_error("Expected app type");
    }
    if (check(TokenKind::Newline)) advance();

    // =============================================================
    // NEW: Parse optional kernel_mode: independent/dependent
    // =============================================================
    while (check(TokenKind::Newline)) advance();
    
    if (check(TokenKind::Ident) && peek().text == "kernel_mode") {
        advance();
        
        // Expect colon
        if (check(TokenKind::Colon)) {
            advance();
        } else {
            std::cerr << "Error at line " << peek().line << ": expected ':' after 'kernel_mode'\n";
            throw std::runtime_error("Expected ':' after kernel_mode");
        }
        
        // Expect independent or dependent
        if (check(TokenKind::Ident)) {
            std::string km = advance().text;
            if (km == "independent") {
                prog.kernelMode = KernelMode::Independent;
            } else if (km == "dependent") {
                prog.kernelMode = KernelMode::Dependent;
            } else {
                std::cerr << "Error at line " << previous().line << ": expected 'independent' or 'dependent' after 'kernel_mode:', got '" << km << "'\n";
                throw std::runtime_error("Invalid kernel_mode");
            }
        } else {
            std::cerr << "Error at line " << peek().line << ": expected 'independent' or 'dependent'\n";
            throw std::runtime_error("Expected kernel_mode value");
        }
        
        if (check(TokenKind::Newline)) advance();
    }
}

Program Parser::parse() {
    Program prog;
    appType = AppType::Console;

    // Check for [no_main] directive in source (detected before lexing)
    // The isLibrary flag is set from outside

    // Parse required app directive first
    while (!check(TokenKind::Eof)) {
        if (check(TokenKind::Newline)) { advance(); continue; }
        break;
    }

    if (check(TokenKind::App)) {
        parseAppType(prog);
        appType = prog.appType;
    } else {
        std::cerr << "Error at line " << peek().line << ": missing 'app' directive. Use 'app gui tool', 'app gui game', 'app gui dx tool', 'app console', 'app efi', or 'app bare' at the top of the file.\n";
        throw std::runtime_error("Missing app directive");
    }

    // Optional: vide easy | vide hard
    while (check(TokenKind::Newline)) advance();
    if (check(TokenKind::Vide)) {
        advance();
        if (check(TokenKind::Ident)) {
            std::string md = advance().text;
            if (md == "easy") { prog.mode = Mode::Easy; mode = Mode::Easy; }
            else if (md == "hard") { prog.mode = Mode::Hard; mode = Mode::Hard; }
            else throw std::runtime_error("Invalid vide mode");
        }
        if (check(TokenKind::Newline)) advance();
    }

    auto trySync = [&]() {
        while (!check(TokenKind::Eof)) {
            if (check(TokenKind::Newline)) { advance(); return true; }
            if (check(TokenKind::End)) return true;
            if (check(TokenKind::Else)) return true;
            if (check(TokenKind::Func)) return true;
            if (check(TokenKind::Struct)) return true;
            advance();
        }
        return false;
    };

    auto addBuiltinStruct = [&](const std::string& name, const std::vector<std::pair<std::string, TypeKind>>& fields) {
        auto sd = std::make_unique<StructDecl>();
        sd->name = name;
        for (auto& f : fields) {
            StructField sf;
            sf.name = f.first;
            sf.type = {f.second};
            sd->fields.push_back(sf);
        }
        prog.structs.push_back(std::move(sd));
    };

    if (prog.appType == AppType::GUI) {
        addBuiltinStruct("vec2", {{"x", TypeKind::Float}, {"y", TypeKind::Float}});
        addBuiltinStruct("vec3", {{"x", TypeKind::Float}, {"y", TypeKind::Float}, {"z", TypeKind::Float}});
        addBuiltinStruct("color", {{"r", TypeKind::Float}, {"g", TypeKind::Float}, {"b", TypeKind::Float}, {"a", TypeKind::Float}});
    }

    while (!check(TokenKind::Eof)) {
        try {
            if (check(TokenKind::Newline)) { advance(); continue; }

            if (check(TokenKind::At)) {
                advance();
                consume(TokenKind::Import, "Expected 'import' after '@'");
                consume(TokenKind::LParen, "Expected '('");
                Token dll = consume(TokenKind::StringLit, "Expected DLL name string");
                consume(TokenKind::RParen, "Expected ')'");
                if (check(TokenKind::Newline)) advance();
                ImportDecl imp;
                std::string raw = dll.text;
                size_t sep = raw.find("::");
                if (sep != std::string::npos) {
                    imp.dllName = raw.substr(0, sep);
                    imp.module  = raw.substr(sep + 2);
                } else {
                    imp.dllName = raw;
                }
                prog.imports.push_back(imp);
                continue;
            }

            if (check(TokenKind::Extern)) {
                prog.functions.push_back(parseExternFunc());
            } else if (check(TokenKind::Struct)) {
                prog.structs.push_back(parseStruct());
            } else if (check(TokenKind::Func)) {
                auto func = parseFunction();
                for (auto& f : prog.functions) {
                    if (f->name == func->name) {
                        throw std::runtime_error("Duplicate function '" + func->name + "'");
                    }
                }
                prog.functions.push_back(std::move(func));
            } else if (check(TokenKind::Var) || check(TokenKind::Let) || check(TokenKind::Const)) {
                prog.globals.push_back(parseVarDecl());
            } else {
                std::cerr << "Unexpected token '" << peek().text << "' at line " << peek().line << std::endl;
                advance();
            }
        } catch (const std::runtime_error&) {
            if (!trySync()) break;
        }
    }
    return prog;
}
