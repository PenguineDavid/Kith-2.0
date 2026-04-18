#include "lexer.h"
#include <cctype>
#include <stdexcept>

Lexer::Lexer(const std::string &src) : source(src), position(0) {}

char Lexer::currentChar()
{
    return position < source.size() ? source[position] : '\0';
}
char Lexer::peek(int offset)
{
    size_t p = position + offset;
    return p < source.size() ? source[p] : '\0';
}
void Lexer::advance() { position++; }

void Lexer::skipWhitespace()
{
    while (std::isspace(currentChar()))
        advance();
}

Token Lexer::identifierOrKeyword()
{
    std::string v;
    while (std::isalnum(currentChar()) || currentChar() == '_')
    {
        v += currentChar();
        advance();
    }
    // keywords — order doesn't matter here since we compare full strings
    if (v == "func")
        return {TokenType::FUNC, v};
    if (v == "int")
        return {TokenType::INT_TYPE, v};
    if (v == "str")
        return {TokenType::STR_TYPE, v};
    if (v == "float")
        return {TokenType::FLOAT_TYPE, v};
    if (v == "bool")
        return {TokenType::BOOL_TYPE, v};
    if (v == "if")
        return {TokenType::IF, v};
    if (v == "else")
        return {TokenType::ELSE, v};
    if (v == "while")
        return {TokenType::WHILE, v};
    if (v == "do")
        return {TokenType::DO, v};
    if (v == "for")
        return {TokenType::FOR, v};
    if (v == "foreach")
        return {TokenType::FOREACH, v};
    if (v == "switch")
        return {TokenType::SWITCH, v};
    if (v == "case")
        return {TokenType::CASE, v};
    if (v == "default")
        return {TokenType::DEFAULT, v};
    if (v == "break")
        return {TokenType::BREAK, v};
    if (v == "struct")
        return {TokenType::STRUCT, v};
    if (v == "try")
        return {TokenType::TRY, v};
    if (v == "catch")
        return {TokenType::CATCH, v};
    if (v == "throw")
        return {TokenType::THROW, v};
    if (v == "finally")
        return {TokenType::FINALLY, v};
    if (v == "include")
        return {TokenType::INCLUDE, v};
    if (v == "return")
        return {TokenType::RETURN, v};
    if (v == "stop")
        return {TokenType::STOP, v};
    if (v == "true")
        return {TokenType::TRUE, v};
    if (v == "false")
        return {TokenType::FALSE, v};
    if (v == "and")
        return {TokenType::AND, v};
    if (v == "or")
        return {TokenType::OR, v};
    if (v == "xor")
        return {TokenType::XOR, v};
    if (v == "not")
        return {TokenType::NOT, v};
    if (v == "typeof")
        return {TokenType::TYPEOF, v};
    if (v == "instanceof")
        return {TokenType::INSTANCEOF, v};
    if (v == "in")
        return {TokenType::IN_KW, v};
    if (v == "delete")
        return {TokenType::DELETE_KW, v};
    return {TokenType::IDENTIFIER, v};
}

Token Lexer::number()
{
    std::string v;
    while (std::isdigit(currentChar()))
    {
        v += currentChar();
        advance();
    }
    // decimal part
    if (currentChar() == '.' && std::isdigit(peek()))
    {
        v += currentChar();
        advance();
        while (std::isdigit(currentChar()))
        {
            v += currentChar();
            advance();
        }
    }
    return {TokenType::NUMBER, v};
}

Token Lexer::parseStringLiteral(const std::string &kind)
{
    std::string content;
    std::string finalKind = kind;

    // Triple-quoted multiline
    if (currentChar() == '"' && peek() == '"' && peek(2) == '"')
    {
        if (finalKind == "normal")
            finalKind = "m";
        advance();
        advance();
        advance();
        while (!(currentChar() == '"' && peek() == '"' && peek(2) == '"') && currentChar() != '\0')
        {
            content += currentChar();
            advance();
        }
        advance();
        advance();
        advance();
    }
    else
    {
        advance(); // opening "
        while (currentChar() != '"' && currentChar() != '\0')
        {
            if (currentChar() == '\\')
            {
                advance();
                switch (currentChar())
                {
                case 'n':
                    content += '\n';
                    break;
                case 't':
                    content += '\t';
                    break;
                case '"':
                    content += '"';
                    break;
                case '\\':
                    content += '\\';
                    break;
                default:
                    content += currentChar();
                    break;
                }
            }
            else
            {
                content += currentChar();
            }
            advance();
        }
        advance(); // closing "
    }
    std::string val = (finalKind == "normal") ? content : finalKind + ":" + content;
    return {TokenType::STRING, val};
}

Token Lexer::stringLiteral() { return parseStringLiteral("normal"); }

std::vector<Token> Lexer::tokenize()
{
    std::vector<Token> tokens;

    auto emit = [&](TokenType t, const std::string &v, int extra = 0)
    {
        tokens.push_back({t, v});
        for (int i = 0; i < extra; i++)
            advance();
    };

    while (currentChar() != '\0')
    {
        skipWhitespace();
        char c = currentChar();
        if (c == '\0')
            break;

        // --- Comments ---
        if (c == '/' && peek() == '/')
        {
            while (currentChar() != '\n' && currentChar() != '\0')
                advance();
            continue;
        }
        if (c == '/' && peek() == '*')
        {
            advance();
            advance();
            while (!(currentChar() == '*' && peek() == '/') && currentChar() != '\0')
                advance();
            if (currentChar() == '\0')
                throw std::runtime_error("Unterminated block comment");
            advance();
            advance();
            continue;
        }

        // --- Prefixed strings ---
        if (c == 'r' && peek() == '"')
        {
            advance();
            tokens.push_back(parseStringLiteral("r"));
            continue;
        }
        if (c == '$' && peek() == '"')
        {
            advance();
            tokens.push_back(parseStringLiteral("$"));
            continue;
        }
        if (c == '"')
        {
            tokens.push_back(stringLiteral());
            continue;
        }

        // --- Identifiers / keywords ---
        if (std::isalpha(c) || c == '_')
        {
            tokens.push_back(identifierOrKeyword());
            continue;
        }

        // --- Numbers ---
        if (std::isdigit(c))
        {
            tokens.push_back(number());
            continue;
        }

        // --- Multi-char operators (longest match first) ---
        switch (c)
        {
        case '=':
            if (peek() == '=' && peek(2) == '=')
            {
                emit(TokenType::STRICT_EQ, "===");
                advance();
                advance();
                advance();
            }
            else if (peek() == '=')
            {
                emit(TokenType::EQ, "==");
                advance();
                advance();
            }
            else
            {
                emit(TokenType::ASSIGN, "=");
                advance();
            }
            break;
        case '!':
            if (peek() == '=' && peek(2) == '=')
            {
                emit(TokenType::STRICT_NE, "!==");
                advance();
                advance();
                advance();
            }
            else if (peek() == '=')
            {
                emit(TokenType::NE, "!=");
                advance();
                advance();
            }
            else
            {
                emit(TokenType::LOGICAL_NOT, "!");
                advance();
            }
            break;
        case '&':
            if (peek() == '&')
            {
                emit(TokenType::LOGICAL_AND, "&&");
                advance();
                advance();
            }
            else
            {
                emit(TokenType::BIT_AND, "&");
                advance();
            }
            break;
        case '|':
            if (peek() == '|')
            {
                emit(TokenType::LOGICAL_OR, "||");
                advance();
                advance();
            }
            else
            {
                emit(TokenType::BIT_OR, "|");
                advance();
            }
            break;
        case '^':
            if (peek() == '^')
            {
                emit(TokenType::LOGICAL_XOR, "^^");
                advance();
                advance();
            }
            else
            {
                emit(TokenType::BIT_XOR, "^");
                advance();
            }
            break;
        case '*':
            if (peek() == '*')
            {
                emit(TokenType::POWER, "**");
                advance();
                advance();
            }
            else
            {
                emit(TokenType::STAR, "*");
                advance();
            }
            break;
        case '+':
            if (peek() == '+')
            {
                emit(TokenType::INC, "++");
                advance();
                advance();
            }
            else
            {
                emit(TokenType::PLUS, "+");
                advance();
            }
            break;
        case '-':
            if (peek() == '-')
            {
                emit(TokenType::DEC, "--");
                advance();
                advance();
            }
            else if (peek() == '>')
            {
                emit(TokenType::ARROW, "->");
                advance();
                advance();
            }
            else
            {
                emit(TokenType::MINUS, "-");
                advance();
            }
            break;
        case '>':
            if (peek() == '>' && peek(2) == '>')
            {
                emit(TokenType::URSH, ">>>");
                advance();
                advance();
                advance();
            }
            else if (peek() == '>')
            {
                emit(TokenType::RSH, ">>");
                advance();
                advance();
            }
            else if (peek() == '=')
            {
                emit(TokenType::GE, ">=");
                advance();
                advance();
            }
            else
            {
                emit(TokenType::GT, ">");
                advance();
            }
            break;
        case '<':
            if (peek() == '<')
            {
                emit(TokenType::LSH, "<<");
                advance();
                advance();
            }
            else if (peek() == '=')
            {
                emit(TokenType::LE, "<=");
                advance();
                advance();
            }
            else
            {
                emit(TokenType::LT, "<");
                advance();
            }
            break;
        case '?':
            if (peek() == '?')
            {
                emit(TokenType::NCO, "??");
                advance();
                advance();
            }
            else
            {
                emit(TokenType::QUESTION, "?");
                advance();
            }
            break;
        case '(':
            emit(TokenType::LPAREN, "(");
            advance();
            break;
        case ')':
            emit(TokenType::RPAREN, ")");
            advance();
            break;
        case '{':
            emit(TokenType::LBRACE, "{");
            advance();
            break;
        case '}':
            emit(TokenType::RBRACE, "}");
            advance();
            break;
        case '[':
            emit(TokenType::LBRACKET, "[");
            advance();
            break;
        case ']':
            emit(TokenType::RBRACKET, "]");
            advance();
            break;
        case '.':
            emit(TokenType::DOT, ".");
            advance();
            break;
        case ',':
            emit(TokenType::COMMA, ",");
            advance();
            break;
        case ':':
            if (peek() == ':')
            {
                emit(TokenType::COLONCOLON, "::");
                advance();
                advance();
            }
            else
            {
                emit(TokenType::COLON, ":");
                advance();
            }
            break;
        case ';':
            emit(TokenType::SEMICOLON, ";");
            advance();
            break;
        case '/':
            emit(TokenType::SLASH, "/");
            advance();
            break;
        case '%':
            emit(TokenType::PERCENT, "%");
            advance();
            break;
        case '~':
            emit(TokenType::BIT_NOT, "~");
            advance();
            break;
        default:
            advance();
            break; // skip unknown
        }
    }

    tokens.push_back({TokenType::END_OF_FILE, ""});
    return tokens;
}