#include "parser.h"
#include <iostream>

Parser::Parser(const std::vector<Token>& tokens) : tokens(tokens), pos(0) {}

Token Parser::peek() const { if (tokens.empty() || pos >= tokens.size()) return {TokenKind::Eof, "", 0, 0.0, 0, 0}; return tokens[pos]; }
Token Parser::peekNext() const {
    if (pos + 1 < tokens.size()) return tokens[pos + 1];
    return Token{TokenKind::Eof, "", 0, 0.0, 0, 0};
}
Token Parser::previous() const { if (pos > 0) return tokens[pos - 1]; return {TokenKind::Error, "", 0, 0.0, 0, 0}; }
Token Parser::advance() { Token t = tokens[pos]; if (!check(TokenKind::Eof)) pos++; return t; }
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
    if (!check(TokenKind::Var) && !check(TokenKind::Let)) {
        std::cerr << "Error at line " << peek().line << ": Expected 'var' or 'let'" << std::endl;
        throw std::runtime_error("Expected 'var' or 'let'");
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
    if (check(TokenKind::Var) || check(TokenKind::Let)) return parseVarDecl();
    if (check(TokenKind::If)) return parseIf();
    if (check(TokenKind::While)) return parseWhile();
    if (check(TokenKind::For)) return parseFor();
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
            advance();
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
    auto stmt = std::make_unique<ExprStmt>();
    stmt->expr = parseExpression();
    if (check(TokenKind::Newline)) advance();
    return stmt;
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
    auto left = parseComparison();
    while (check(TokenKind::AmpAmp)) {
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
    auto left = parseTerm();
    while (check(TokenKind::EqEq) || check(TokenKind::NotEq) ||
           check(TokenKind::Lt) || check(TokenKind::Gt) ||
           check(TokenKind::LtEq) || check(TokenKind::GtEq)) {
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
    auto left = parsePrimary();
    while (check(TokenKind::Star) || check(TokenKind::Slash)) {
        auto op = advance();
        auto right = parsePrimary();
        auto bin = std::make_unique<BinaryExpr>();
        bin->left = std::move(left);
        bin->op = op.text;
        bin->right = std::move(right);
        left = std::move(bin);
    }
    return left;
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
    } else if (check(TokenKind::Minus)) {
        advance();
        if (check(TokenKind::Number)) {
            auto n = std::make_unique<NumberExpr>();
            n->value = -advance().intVal;
            expr = std::move(n);
        } else if (check(TokenKind::FloatLit)) {
            auto f = std::make_unique<FloatExpr>();
            f->value = -advance().floatVal;
            expr = std::move(f);
        } else {
            auto operand = parsePrimary();
            auto unary = std::make_unique<UnaryExpr>();
            unary->op = "-";
            unary->operand = std::move(operand);
            expr = std::move(unary);
        }
    } else if (check(TokenKind::Plus)) {
        advance();
        expr = parsePrimary();
    } else if (check(TokenKind::Bang)) {
        advance();
        auto operand = parsePrimary();
        auto unary = std::make_unique<UnaryExpr>();
        unary->op = "!";
        unary->operand = std::move(operand);
        expr = std::move(unary);
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
            if (!check(TokenKind::RParen)) {
                call->args.push_back(parseExpression());
                while (match(TokenKind::Comma)) {
                    call->args.push_back(parseExpression());
                }
            }
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
        if (!check(TokenKind::RParen)) {
            call->args.push_back(parseExpression());
            while (match(TokenKind::Comma)) {
                call->args.push_back(parseExpression());
            }
        }
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
            if (!check(TokenKind::RParen)) {
                call->args.push_back(parseExpression());
                while (match(TokenKind::Comma)) {
                    call->args.push_back(parseExpression());
                }
            }
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
            // Check for optional render type: "app gui sr" or "app gui dx"
            if (check(TokenKind::Ident)) {
                std::string rt = peek().text;
                if (rt == "sr") {
                    advance();
                    prog.renderType = RenderType::Software;
                } else if (rt == "dx") {
                    advance();
                    prog.renderType = RenderType::DX11;
                }
            }
        } else if (type == "console") {
            prog.appType = AppType::Console;
        } else if (type == "efi") {
            prog.appType = AppType::EFI;
        } else {
            std::cerr << "Error at line " << previous().line << ": expected 'gui', 'console', or 'efi' after 'app', got '" << type << "'\n";
            throw std::runtime_error("Invalid app type");
        }
    } else {
        std::cerr << "Error at line " << peek().line << ": expected 'gui', 'console', or 'efi' after 'app'\n";
        throw std::runtime_error("Expected app type");
    }
    if (check(TokenKind::Newline)) advance();
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
        std::cerr << "Error at line " << peek().line << ": missing 'app' directive. Use 'app gui', 'app gui dx', 'app gui sr', 'app console', or 'app efi' at the top of the file.\n";
        throw std::runtime_error("Missing app directive");
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
            } else if (check(TokenKind::Var) || check(TokenKind::Let)) {
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
