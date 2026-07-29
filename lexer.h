#pragma once
#include <string>
#include <vector>
#include <cstdint>

enum class TokenKind {
    Func, Var, Let, If, Else, While, For, Return, End, True, False, Import,
    Struct, Extern, App, From, Type,
    TypeInt, TypeFloat, TypeBool, TypeString, TypeVoid,
    TypeVec2, TypeVec3, TypeColor, TypeEntity,
    Ident, Number, FloatLit, StringLit,
    Plus, Minus, Star, Slash, Dot, Bang,
    Eq, EqEq, NotEq, Lt, Gt, LtEq, GtEq, Arrow,
    AmpAmp, PipePipe,
    LParen, RParen, LBrace, RBrace, LBrack, RBrack,
    Comma, Colon, At, Newline, Eof, Error
};

struct Token {
    TokenKind kind;
    std::string text;
    int64_t intVal = 0;
    double floatVal = 0.0;
    int line = 0;
    int col = 0;
};

class Lexer {
public:
    explicit Lexer(const std::string& source);
    Token next();
    const std::vector<Token>& all();

private:
    void skipWhitespace();
    Token scanToken();
    Token scanNumber();
    Token scanIdentOrKeyword();
    Token scanString();
    void advance();
    char peek() const;
    char peekNext() const;
    bool match(char c);
    bool isAtEnd() const;
    Token makeToken(TokenKind kind);
    Token makeError(const std::string& msg);

    std::string source;
    size_t start;
    size_t current;
    int line;
    int col;
    std::vector<Token> tokens;
    bool tokenized;
};
