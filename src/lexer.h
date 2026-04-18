#pragma once
#include <string>
#include <vector>

enum class TokenType
{
    // Keywords
    FUNC,
    INT_TYPE,
    STR_TYPE,
    FLOAT_TYPE,
    BOOL_TYPE,
    IF,
    ELSE,
    WHILE,
    DO,
    FOR,
    FOREACH,
    SWITCH,
    CASE,
    DEFAULT,
    BREAK,
    STRUCT,
    TRY,
    CATCH,
    THROW,
    FINALLY,
    INCLUDE,
    RETURN,
    STOP,
    TRUE,
    FALSE,
    // Word operators / specials
    AND,
    OR,
    XOR,
    NOT,
    TYPEOF,
    INSTANCEOF,
    IN_KW,
    DELETE_KW,
    // Literals / identifiers
    IDENTIFIER,
    NUMBER,
    STRING,
    // Delimiters
    LPAREN,
    RPAREN,
    LBRACE,
    RBRACE,
    LBRACKET,
    RBRACKET,
    DOT,
    COMMA,
    COLON,
    SEMICOLON,
    COLONCOLON, // ::
    ARROW,      // ->
           // Symbolic operators — assignment
    ASSIGN, // =
            // Symbolic operators — equality
    EQ,        // ==
    STRICT_EQ, // ===
    NE,        // !=
    STRICT_NE, // !==
               // Symbolic operators — comparison
    LT,
    GT,
    LE,
    GE,
    // Symbolic operators — arithmetic
    PLUS,
    MINUS,
    STAR,
    SLASH,
    PERCENT,
    POWER, // ** = POWER
    INC,
    DEC, // ++ --
         // Symbolic operators — logical (symbol variants)
    LOGICAL_AND, // &&
    LOGICAL_OR,  // ||
    LOGICAL_XOR, // ^^
    LOGICAL_NOT, // !
                 // Symbolic operators — bitwise
    BIT_AND,
    BIT_OR,
    BIT_XOR,
    BIT_NOT, // & | ^ ~
             // Symbolic operators — shift
    LSH,
    RSH,
    URSH, // << >> >>>
          // Null-coalescing / ternary
    NCO,      // ??
    QUESTION, // ?
              //
    END_OF_FILE
};

struct Token
{
    TokenType type;
    std::string value;
};

class Lexer
{
public:
    Lexer(const std::string &src);
    std::vector<Token> tokenize();

private:
    std::string source;
    size_t position;

    char currentChar();
    char peek(int offset = 1);
    void advance();
    void skipWhitespace();
    Token identifierOrKeyword();
    Token number();
    Token stringLiteral();
    Token parseStringLiteral(const std::string &kind);
};