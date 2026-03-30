#ifndef TURINGOS_COMPILER_H
#define TURINGOS_COMPILER_H

#include <stdint.h>

typedef enum {
    CC_TOK_EOF = 0,
    CC_TOK_IDENT,
    CC_TOK_NUMBER,
    CC_TOK_STRING,
    CC_TOK_CHAR,
    CC_TOK_KW_CHAR,
    CC_TOK_KW_INT,
    CC_TOK_KW_IF,
    CC_TOK_KW_ELSE,
    CC_TOK_KW_WHILE,
    CC_TOK_KW_FOR,
    CC_TOK_KW_RETURN,
    CC_TOK_LPAREN,
    CC_TOK_RPAREN,
    CC_TOK_LBRACE,
    CC_TOK_RBRACE,
    CC_TOK_LBRACKET,
    CC_TOK_RBRACKET,
    CC_TOK_COMMA,
    CC_TOK_SEMI,
    CC_TOK_PLUS,
    CC_TOK_MINUS,
    CC_TOK_STAR,
    CC_TOK_SLASH,
    CC_TOK_PERCENT,
    CC_TOK_ASSIGN,
    CC_TOK_EQ,
    CC_TOK_NE,
    CC_TOK_LT,
    CC_TOK_LE,
    CC_TOK_GT,
    CC_TOK_GE,
    CC_TOK_AND_AND,
    CC_TOK_OR_OR,
    CC_TOK_NOT
} cc_token_kind_t;

typedef struct {
    cc_token_kind_t kind;
    uint32_t line;
    uint32_t col;
    uint32_t offset;
    uint32_t length;
} cc_token_t;

typedef enum {
    CC_AST_UNKNOWN = 0,
    CC_AST_PROGRAM,
    CC_AST_FUNC_DECL,
    CC_AST_VAR_DECL,
    CC_AST_ASSIGN,
    CC_AST_BINOP,
    CC_AST_UNOP,
    CC_AST_IF,
    CC_AST_WHILE,
    CC_AST_FOR,
    CC_AST_CALL,
    CC_AST_RETURN,
    CC_AST_LITERAL,
    CC_AST_IDENT
} cc_ast_kind_t;

typedef struct {
    cc_ast_kind_t kind;
    int32_t value;
} cc_ast_node_t;

int cc_lex(const char *src, cc_token_t *tokens, int max_tokens);
int cc_compile(const char *src_path, const char *out_path);

#endif
