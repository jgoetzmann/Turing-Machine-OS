#ifndef TURINGOS_COMPILER_H
#define TURINGOS_COMPILER_H
/* Tiny-C v2 compiler (host-side "ROM service"). Function prototypes are frozen; the enums may be
 * extended by the compiler slice (append only — never remove or reorder existing members). */
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
    CC_TOK_NOT,
    /* v2 additions (append only) */
    CC_TOK_AMP,
    CC_TOK_PIPE,
    CC_TOK_CARET,
    CC_TOK_TILDE,
    CC_TOK_SHL,
    CC_TOK_SHR,
    CC_TOK_INC,
    CC_TOK_DEC,
    CC_TOK_PLUS_ASSIGN,
    CC_TOK_MINUS_ASSIGN,
    CC_TOK_STAR_ASSIGN,
    CC_TOK_SLASH_ASSIGN,
    CC_TOK_PERCENT_ASSIGN,
    CC_TOK_AMP_ASSIGN,
    CC_TOK_PIPE_ASSIGN,
    CC_TOK_CARET_ASSIGN,
    CC_TOK_SHL_ASSIGN,
    CC_TOK_SHR_ASSIGN,
    CC_TOK_KW_BREAK,
    CC_TOK_KW_CONTINUE,
    CC_TOK_KW_DO,
    CC_TOK_KW_AT          /* __at */
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
    CC_AST_IDENT,
    /* v2 additions (append only) */
    CC_AST_INDEX,
    CC_AST_BREAK,
    CC_AST_CONTINUE,
    CC_AST_DO_WHILE,
    CC_AST_ARRAY_DECL,
    CC_AST_POSTFIX
} cc_ast_kind_t;

typedef struct {
    cc_ast_kind_t kind;
    int32_t value;
    int32_t left;
    int32_t right;
    int32_t third;
    int32_t next;
    cc_token_kind_t op;
    uint32_t token_index;
} cc_ast_node_t;

int cc_lex(const char *src, cc_token_t *tokens, int max_tokens);
int cc_parse(const char *src,
             const cc_token_t *tokens,
             int token_count,
             cc_ast_node_t *nodes,
             int max_nodes);
int cc_compile(const char *src_path, const char *out_path);   /* host CLI convenience; 0 ok, -1 error (message on stderr) */
/* Buffer form used by the BIOS and the API: returns output length or -1; on error writes "file:line:col: message"
 * (file = "src.c") into err. */
int cc_compile_buf(const char *src, uint32_t len, uint8_t *out, uint32_t cap, char *err, uint32_t errcap);

#endif
