#pragma once
#include <cstdint>
#include <cstddef>

enum class TokenType : uint8_t
{
    LParem,
    RParem,
    String,
    Symbol,
    Number,
    Eof,
    Error
};

struct TokenView
{
    const char* start = nullptr;
    size_t length = 0;
};

struct Token
{
    TokenType type = TokenType::Error;

    TokenView text;

    double number_value = 0.0;

    uint32_t line = 0;
    uint32_t column = 0;
};

class Lexer
{
private:

    const char* cursor;

    uint32_t line;
    uint32_t column;

    bool IsValidSymbolStart(char c);
    bool IsValidSymbolBody(char c);

    void Advance();
  


public:

    explicit Lexer(const char* source_buffer);

    Token ScanQuotedString();
    Token ScanSymbol();
    Token ScanNumber();
    Token NextToken();
};