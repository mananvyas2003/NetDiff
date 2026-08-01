    #include "lexer.h"

    Lexer::Lexer(const char* source_buffer)
        : cursor(source_buffer),
        line(1),
        column(1)
    {
    }

    void Lexer::Advance()
    {
        if (*cursor == '\n')
        {
            line++;
            column = 1;
        }
        else
        {
            column++;
        }

        cursor++;
    }

    bool Lexer::IsValidSymbolStart(char c)
    {
        return (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            c == '_' ||
            c == ':' ||
            c == '/';
    }

    bool Lexer::IsValidSymbolBody(char c)
    {
        return IsValidSymbolStart(c) ||
            (c >= '0' && c <= '9') ||
            c == '-' ||
            c == '.';
    }

    Token Lexer::ScanQuotedString()
    {
        Token t;

        t.type = TokenType::String;
        t.line = line;
        t.column = column;

        Advance(); // consume opening quote

        const char* start_ptr = cursor;

        while (*cursor != '"' && *cursor != '\0')
        {
            if (*cursor == '\\' && *(cursor + 1) == '"')
            {
                Advance();
                Advance();
            }
            else
            {
                Advance();
            }
        }

        t.text =
        {
            start_ptr,
            static_cast<size_t>(cursor - start_ptr)
        };

        if (*cursor == '"')
        {
            Advance(); // consume closing quote
        }
        else
        {
            t.type = TokenType::Error;
        }

        return t;
    }

    Token Lexer::ScanSymbol()
    {
        Token t;

        t.type = TokenType::Symbol;
        t.line = line;
        t.column = column;

        const char* start_ptr = cursor;

        while (IsValidSymbolBody(*cursor))
        {
            Advance();
        }

        t.text =
        {
            start_ptr,
            static_cast<size_t>(cursor - start_ptr)
        };

        return t;
    }

    Token Lexer::ScanNumber()
    {
        Token t;

        t.type = TokenType::Number;
        t.line = line;
        t.column = column;

        const char* start_ptr = cursor;

        double sign = 1.0;

        if (*cursor == '-')
        {
            sign = -1.0;
            Advance();
        }

        double value = 0.0;

        while (*cursor >= '0' && *cursor <= '9')
        {
            value = value * 10.0 + (*cursor - '0');
            Advance();
        }

        if (*cursor == '.')
        {
            Advance();

            double fraction = 0.1;

            while (*cursor >= '0' && *cursor <= '9')
            {
                value += (*cursor - '0') * fraction;
                fraction *= 0.1;
                Advance();
            }
        }

        t.text =
        {
            start_ptr,
            static_cast<size_t>(cursor - start_ptr)
        };

        t.number_value = value * sign;

        return t;
    }

    Token Lexer::NextToken()
    {
        while (true)
        {
            char c = *cursor;

            // Skip whitespace
            if (c == ' ' ||
                c == '\t' ||
                c == '\r' ||
                c == '\n')
            {
                Advance();
                continue;
            }

            // EOF
            if (c == '\0')
            {
                Token t;

                t.type = TokenType::Eof;
                t.line = line;
                t.column = column;
                t.text = { cursor, 0 };

                return t;
            }

            // (
            if (c == '(')
            {
                Token t;

                t.type = TokenType::LParem;
                t.line = line;
                t.column = column;
                t.text = { cursor, 1 };

                Advance();

                return t;
            }

            // )
            if (c == ')')
            {
                Token t;

                t.type = TokenType::RParem;
                t.line = line;
                t.column = column;
                t.text = { cursor, 1 };

                Advance();

                return t;
            }

            // String
            if (c == '"')
            {
                return ScanQuotedString();
            }

            // Number
            if ((c >= '0' && c <= '9') ||
                c == '-' ||
                c == '.')
            {
                char next = *(cursor + 1);

                if ((c >= '0' && c <= '9') ||
                    (next >= '0' && next <= '9'))
                {
                    return ScanNumber();
                }
            }

            // Symbol
            if (IsValidSymbolStart(c))
            {
                return ScanSymbol();
            }

            // Error token
            Token error_token;

            error_token.type = TokenType::Error;
            error_token.line = line;
            error_token.column = column;
            error_token.text = { cursor, 1 };

            Advance();

            return error_token;
        }
    }