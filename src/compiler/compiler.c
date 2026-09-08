/* TuringOS v2 — tiny-C v2 compiler (host-side ROM service).
 *
 * Pipeline: lexer -> recursive-descent parser (flat AST in cc_ast_node_t[]) -> single-pass
 * 8080 code generator.  Expression values live in HL (16-bit); binary operators evaluate the
 * left operand, PUSH H, evaluate the right operand, POP D and combine DE (left) with HL
 * (right).  Locals live on the 8080 stack and are addressed with `LXI H,off ; DAD SP`, where
 * `off` includes the compile-time push depth.  Globals and string literals live in a data
 * segment placed after the code; runtime helper routines (mul/div/compare/...) are emitted
 * once, after the last function, only when referenced.  Image: `CALL main ; HLT`, code,
 * runtime, data.  The compiler never emits LXI SP and never touches fixed scratch addresses.
 *
 * Grammar per SPEC §S3 plus two harmless supersets: `#` lines are skipped (leading blanks allowed),
 * and a parameter list may be written `(void)` — this is what lets src/shell/shell_tpa.c compile
 * both as tiny-C and as host C99 (the Makefile used to glob every .c under src into libtos.a).
 *
 * <stdio.h> is used only by cc_compile() (the host CLI path wrapper). */
#include "compiler.h"

#include <string.h>
#include <stdio.h>

#define CC_MAX_SRC      32768u
#define CC_MAX_TOKENS   32768
#define CC_MAX_NODES    32768
#define CC_MAX_GLOBALS  256
#define CC_MAX_FUNCS    64
#define CC_MAX_LOCALS   32
#define CC_MAX_PARAMS   4
#define CC_MAX_FIXUPS   16384
#define CC_MAX_STRINGS  512
#define CC_STRPOOL      16384
#define CC_OUT_CAP      65536
#define CC_TPA_SIZE     16128
#define CC_CODE_BASE    0x0100u
#define CC_NAME_MAX     32
#define CC_LOOP_DEPTH   16
#define CC_LOOP_PATCHES 64
#define CC_SCOPE_DEPTH  64

typedef struct {
    uint32_t line;
    uint32_t col;
    const char *msg;
    char name[CC_NAME_MAX];
    int set;
} cc_diag_t;

static const char MSG_SEMI[]      = "expected ';'";
static const char MSG_RPAREN[]    = "expected ')'";
static const char MSG_UNDEF_FN[]  = "undefined function";
static const char MSG_UNDEF_VAR[] = "undefined variable";
static const char MSG_LOCALS[]    = "too many locals";
static const char MSG_LARGE[]     = "program too large";
static const char MSG_UNEXP[]     = "unexpected token";
static const char MSG_LOCARR[]    = "local arrays are not supported";
static const char MSG_NESTING[]   = "expression nests too deeply";
static const char MSG_ARGCOUNT[]  = "wrong number of arguments";
static const char MSG_AT_INIT[]   = "__at variables cannot have an initialiser";

static void diag_set(cc_diag_t *d, uint32_t line, uint32_t col, const char *msg, const char *name) {
    if (d == NULL || d->set) return;
    d->set = 1;
    d->line = line;
    d->col = col;
    d->msg = msg;
    d->name[0] = '\0';
    if (name != NULL) {
        size_t i;
        for (i = 0u; i + 1u < (size_t)CC_NAME_MAX && name[i] != '\0'; ++i) d->name[i] = name[i];
        d->name[i] = '\0';
    }
}

/* ------------------------------------------------------------------------------------------ */
/* Small string helpers (no stdio outside cc_compile)                                          */
/* ------------------------------------------------------------------------------------------ */

static void str_put(char *dst, uint32_t cap, uint32_t *pos, const char *s) {
    while (*s != '\0') {
        if (*pos + 1u < cap) dst[(*pos)++] = *s;
        s++;
    }
}

static void str_put_u32(char *dst, uint32_t cap, uint32_t *pos, uint32_t v) {
    char tmp[12];
    int n = 0;
    if (v == 0u) tmp[n++] = '0';
    while (v > 0u) {
        tmp[n++] = (char)('0' + (int)(v % 10u));
        v /= 10u;
    }
    while (n > 0) {
        char c[2];
        c[0] = tmp[--n];
        c[1] = '\0';
        str_put(dst, cap, pos, c);
    }
}

static void diag_format(const cc_diag_t *d, char *err, uint32_t errcap) {
    uint32_t pos = 0u;
    if (err == NULL || errcap == 0u) return;
    str_put(err, errcap, &pos, "src.c:");
    str_put_u32(err, errcap, &pos, d->line);
    str_put(err, errcap, &pos, ":");
    str_put_u32(err, errcap, &pos, d->col);
    str_put(err, errcap, &pos, ": ");
    str_put(err, errcap, &pos, d->msg != NULL ? d->msg : MSG_UNEXP);
    if (d->name[0] != '\0') {
        str_put(err, errcap, &pos, " '");
        str_put(err, errcap, &pos, d->name);
        str_put(err, errcap, &pos, "'");
    }
    err[pos < errcap ? pos : errcap - 1u] = '\0';
}

static int is_alpha(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int is_digit(unsigned char c) { return c >= '0' && c <= '9'; }
static int is_xdigit(unsigned char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static int xval(unsigned char c) {
    if (is_digit(c)) return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    return (int)(c - 'A') + 10;
}

/* ------------------------------------------------------------------------------------------ */
/* Lexer                                                                                       */
/* ------------------------------------------------------------------------------------------ */

static cc_token_kind_t keyword_kind(const char *s, uint32_t len) {
    if (len == 4u && strncmp(s, "char", 4u) == 0) return CC_TOK_KW_CHAR;
    if (len == 3u && strncmp(s, "int", 3u) == 0) return CC_TOK_KW_INT;
    if (len == 2u && strncmp(s, "if", 2u) == 0) return CC_TOK_KW_IF;
    if (len == 4u && strncmp(s, "else", 4u) == 0) return CC_TOK_KW_ELSE;
    if (len == 5u && strncmp(s, "while", 5u) == 0) return CC_TOK_KW_WHILE;
    if (len == 3u && strncmp(s, "for", 3u) == 0) return CC_TOK_KW_FOR;
    if (len == 6u && strncmp(s, "return", 6u) == 0) return CC_TOK_KW_RETURN;
    if (len == 5u && strncmp(s, "break", 5u) == 0) return CC_TOK_KW_BREAK;
    if (len == 8u && strncmp(s, "continue", 8u) == 0) return CC_TOK_KW_CONTINUE;
    if (len == 2u && strncmp(s, "do", 2u) == 0) return CC_TOK_KW_DO;
    if (len == 4u && strncmp(s, "__at", 4u) == 0) return CC_TOK_KW_AT;
    return CC_TOK_IDENT;
}

typedef struct {
    const char *src;
    uint32_t i;
    uint32_t line;
    uint32_t col;
} cc_lex_state_t;

static void lx_adv(cc_lex_state_t *L) {
    if (L->src[L->i] == '\n') {
        L->line++;
        L->col = 1u;
    } else {
        L->col++;
    }
    L->i++;
}

/* Returns token count (including the trailing EOF token) or -1 with the diagnostic set. */
static int lex_run(const char *src, cc_token_t *tokens, int max_tokens, cc_diag_t *d) {
    cc_lex_state_t L;
    int count = 0;

    if (src == NULL || tokens == NULL || max_tokens <= 0) {
        diag_set(d, 1u, 1u, MSG_UNEXP, NULL);
        return -1;
    }
    L.src = src;
    L.i = 0u;
    L.line = 1u;
    L.col = 1u;

    for (;;) {
        cc_token_t t;
        unsigned char ch = (unsigned char)src[L.i];
        unsigned char nx;

        if (ch == '\0') break;
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v') {
            lx_adv(&L);
            continue;
        }
        /* preprocessor lines ('#' first on the line, leading blanks allowed) are ignored */
        if (ch == '#') {
            uint32_t k = L.i;
            int only_blanks = 1;
            while (k > 0u && src[k - 1u] != '\n') {
                if (src[k - 1u] != ' ' && src[k - 1u] != '\t') {
                    only_blanks = 0;
                    break;
                }
                k--;
            }
            if (only_blanks) {
                while (src[L.i] != '\0' && src[L.i] != '\n') lx_adv(&L);
                continue;
            }
        }
        nx = (unsigned char)src[L.i + 1u];
        if (ch == '/' && nx == '/') {
            while (src[L.i] != '\0' && src[L.i] != '\n') lx_adv(&L);
            continue;
        }
        if (ch == '/' && nx == '*') {
            uint32_t sl = L.line, sc = L.col;
            lx_adv(&L);
            lx_adv(&L);
            for (;;) {
                if (src[L.i] == '\0') {
                    diag_set(d, sl, sc, MSG_UNEXP, NULL);
                    return -1;
                }
                if (src[L.i] == '*' && src[L.i + 1u] == '/') {
                    lx_adv(&L);
                    lx_adv(&L);
                    break;
                }
                lx_adv(&L);
            }
            continue;
        }

        t.line = L.line;
        t.col = L.col;
        t.offset = L.i;
        t.length = 1u;
        t.kind = CC_TOK_EOF;

        if (is_alpha(ch)) {
            uint32_t start = L.i;
            while (is_alpha((unsigned char)src[L.i]) || is_digit((unsigned char)src[L.i])) lx_adv(&L);
            t.offset = start;
            t.length = L.i - start;
            t.kind = keyword_kind(&src[start], t.length);
        } else if (is_digit(ch)) {
            uint32_t start = L.i;
            if (ch == '0' && (nx == 'x' || nx == 'X') && is_xdigit((unsigned char)src[L.i + 2u])) {
                lx_adv(&L);
                lx_adv(&L);
                while (is_xdigit((unsigned char)src[L.i])) lx_adv(&L);
            } else {
                while (is_digit((unsigned char)src[L.i])) lx_adv(&L);
            }
            t.offset = start;
            t.length = L.i - start;
            t.kind = CC_TOK_NUMBER;
        } else if (ch == '"' || ch == '\'') {
            unsigned char quote = ch;
            uint32_t start = L.i;
            lx_adv(&L);
            for (;;) {
                unsigned char c = (unsigned char)src[L.i];
                if (c == '\0' || c == '\n') {
                    diag_set(d, t.line, t.col, MSG_UNEXP, NULL);
                    return -1;
                }
                if (c == quote) {
                    lx_adv(&L);
                    break;
                }
                if (c == '\\' && src[L.i + 1u] != '\0') {
                    lx_adv(&L);
                }
                lx_adv(&L);
            }
            t.offset = start;
            t.length = L.i - start;
            t.kind = (quote == '"') ? CC_TOK_STRING : CC_TOK_CHAR;
        } else {
            unsigned char n2 = (nx != 0u) ? (unsigned char)src[L.i + 2u] : 0u;
            uint32_t len = 1u;
            switch (ch) {
                case '(': t.kind = CC_TOK_LPAREN; break;
                case ')': t.kind = CC_TOK_RPAREN; break;
                case '{': t.kind = CC_TOK_LBRACE; break;
                case '}': t.kind = CC_TOK_RBRACE; break;
                case '[': t.kind = CC_TOK_LBRACKET; break;
                case ']': t.kind = CC_TOK_RBRACKET; break;
                case ',': t.kind = CC_TOK_COMMA; break;
                case ';': t.kind = CC_TOK_SEMI; break;
                case '~': t.kind = CC_TOK_TILDE; break;
                case '+':
                    if (nx == '+') { t.kind = CC_TOK_INC; len = 2u; }
                    else if (nx == '=') { t.kind = CC_TOK_PLUS_ASSIGN; len = 2u; }
                    else t.kind = CC_TOK_PLUS;
                    break;
                case '-':
                    if (nx == '-') { t.kind = CC_TOK_DEC; len = 2u; }
                    else if (nx == '=') { t.kind = CC_TOK_MINUS_ASSIGN; len = 2u; }
                    else t.kind = CC_TOK_MINUS;
                    break;
                case '*':
                    if (nx == '=') { t.kind = CC_TOK_STAR_ASSIGN; len = 2u; }
                    else t.kind = CC_TOK_STAR;
                    break;
                case '/':
                    if (nx == '=') { t.kind = CC_TOK_SLASH_ASSIGN; len = 2u; }
                    else t.kind = CC_TOK_SLASH;
                    break;
                case '%':
                    if (nx == '=') { t.kind = CC_TOK_PERCENT_ASSIGN; len = 2u; }
                    else t.kind = CC_TOK_PERCENT;
                    break;
                case '=':
                    if (nx == '=') { t.kind = CC_TOK_EQ; len = 2u; }
                    else t.kind = CC_TOK_ASSIGN;
                    break;
                case '!':
                    if (nx == '=') { t.kind = CC_TOK_NE; len = 2u; }
                    else t.kind = CC_TOK_NOT;
                    break;
                case '<':
                    if (nx == '<' && n2 == '=') { t.kind = CC_TOK_SHL_ASSIGN; len = 3u; }
                    else if (nx == '<') { t.kind = CC_TOK_SHL; len = 2u; }
                    else if (nx == '=') { t.kind = CC_TOK_LE; len = 2u; }
                    else t.kind = CC_TOK_LT;
                    break;
                case '>':
                    if (nx == '>' && n2 == '=') { t.kind = CC_TOK_SHR_ASSIGN; len = 3u; }
                    else if (nx == '>') { t.kind = CC_TOK_SHR; len = 2u; }
                    else if (nx == '=') { t.kind = CC_TOK_GE; len = 2u; }
                    else t.kind = CC_TOK_GT;
                    break;
                case '&':
                    if (nx == '&') { t.kind = CC_TOK_AND_AND; len = 2u; }
                    else if (nx == '=') { t.kind = CC_TOK_AMP_ASSIGN; len = 2u; }
                    else t.kind = CC_TOK_AMP;
                    break;
                case '|':
                    if (nx == '|') { t.kind = CC_TOK_OR_OR; len = 2u; }
                    else if (nx == '=') { t.kind = CC_TOK_PIPE_ASSIGN; len = 2u; }
                    else t.kind = CC_TOK_PIPE;
                    break;
                case '^':
                    if (nx == '=') { t.kind = CC_TOK_CARET_ASSIGN; len = 2u; }
                    else t.kind = CC_TOK_CARET;
                    break;
                default:
                    diag_set(d, t.line, t.col, MSG_UNEXP, NULL);
                    return -1;
            }
            t.length = len;
            while (len > 0u) {
                lx_adv(&L);
                len--;
            }
        }

        if (count >= max_tokens - 1) {
            diag_set(d, t.line, t.col, MSG_LARGE, NULL);
            return -1;
        }
        tokens[count++] = t;
    }

    tokens[count].kind = CC_TOK_EOF;
    tokens[count].line = L.line;
    tokens[count].col = L.col;
    tokens[count].offset = L.i;
    tokens[count].length = 0u;
    count++;
    return count;
}

int cc_lex(const char *src, cc_token_t *tokens, int max_tokens) {
    cc_diag_t d;
    memset(&d, 0, sizeof(d));
    return lex_run(src, tokens, max_tokens, &d);
}

/* Decode a quoted char/string token body (without the quotes) into bytes. Returns the count. */
static int decode_escaped(const char *s, uint32_t len, uint8_t *out, int cap) {
    uint32_t i = 0u;
    int n = 0;
    while (i < len) {
        unsigned char c = (unsigned char)s[i++];
        if (c == '\\' && i < len) {
            unsigned char e = (unsigned char)s[i++];
            switch (e) {
                case 'n': c = 10u; break;
                case 't': c = 9u; break;
                case 'r': c = 13u; break;
                case '0': c = 0u; break;
                case 'a': c = 7u; break;
                case 'b': c = 8u; break;
                case 'f': c = 12u; break;
                case 'v': c = 11u; break;
                case 'e': c = 27u; break;
                case 'x': {
                    int v = 0;
                    int k = 0;
                    while (i < len && k < 2 && is_xdigit((unsigned char)s[i])) {
                        v = v * 16 + xval((unsigned char)s[i]);
                        i++;
                        k++;
                    }
                    c = (unsigned char)v;
                    break;
                }
                default: c = e; break;
            }
        }
        if (n < cap) out[n] = c;
        n++;
    }
    return n;
}

static int32_t parse_number_text(const char *s, uint32_t len) {
    uint32_t i = 0u;
    uint32_t v = 0u;
    if (len >= 2u && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        for (i = 2u; i < len; ++i) v = (v * 16u + (uint32_t)xval((unsigned char)s[i])) & 0xFFFFFFFFu;
        return (int32_t)(v & 0xFFFFFFFFu);
    }
    for (i = 0u; i < len; ++i) v = v * 10u + (uint32_t)(s[i] - '0');
    return (int32_t)v;
}

static int32_t char_token_value(const char *src, const cc_token_t *t) {
    uint8_t buf[8];
    int n;
    if (t->length < 2u) return 0;
    n = decode_escaped(src + t->offset + 1u, t->length - 2u, buf, 8);
    if (n <= 0) return 0;
    return (int32_t)buf[0];
}

/* ------------------------------------------------------------------------------------------ */
/* Parser                                                                                      */
/*                                                                                             */
/* AST conventions (flat node array, root = node 0, CC_AST_PROGRAM, left = first declaration,  */
/* declarations chained through `next`):                                                       */
/*   FUNC_DECL   op=type  left=IDENT  right=first param (VAR_DECL chain)  third=body block      */
/*   VAR_DECL    op=type  left=IDENT  right=init expr (-1 none)  third=__at address expr        */
/*   ARRAY_DECL  op=type  left=IDENT  value=element count  right=init (STRING literal or        */
/*               LITERAL chain)  third=__at address expr                                        */
/*   block       kind=PROGRAM  left=first stmt (chain through next)                             */
/*   IF          left=cond right=then third=else      WHILE/DO_WHILE left=cond right=body       */
/*   FOR         left=init right=cond third=step value=body                                     */
/*   RETURN      left=expr (-1 none)   BREAK/CONTINUE   UNKNOWN = empty statement               */
/*   ASSIGN      op=assignment token left=lvalue right=rhs                                      */
/*   BINOP/UNOP  op=token left[/right]   POSTFIX op=INC/DEC left=lvalue                         */
/*   CALL        left=IDENT right=first arg (chain)   INDEX left=IDENT right=index expr         */
/*   LITERAL     token_index = NUMBER/CHAR/STRING token; value = numeric value                  */
/* ------------------------------------------------------------------------------------------ */

typedef struct {
    const char *src;
    const cc_token_t *toks;
    int ntok;
    int pos;
    cc_ast_node_t *nodes;
    int nnodes;
    int max_nodes;
    cc_diag_t *diag;
    int failed;
    int depth;                /* nested sub-expressions in flight; the parser recurses per level */
} cc_par_t;

/* Recursive descent costs a C stack frame per nesting level, so the source has to be bounded
   somewhere. 96 levels is far past anything readable and far short of the smallest stack. */
#define CC_MAX_DEPTH 96

static int p_kind(const cc_par_t *p) {
    if (p->pos < 0 || p->pos >= p->ntok) return (int)CC_TOK_EOF;
    return (int)p->toks[p->pos].kind;
}

static int p_fail(cc_par_t *p, int tok_i, const char *msg) {
    if (!p->failed) {
        const cc_token_t *t;
        if (tok_i < 0) tok_i = 0;
        if (tok_i >= p->ntok) tok_i = p->ntok - 1;
        t = &p->toks[tok_i];
        diag_set(p->diag, t->line, t->col, msg, NULL);
        p->failed = 1;
    }
    return -1;
}

static int p_match(cc_par_t *p, cc_token_kind_t kind) {
    if (p_kind(p) == (int)kind) {
        p->pos++;
        return 1;
    }
    return 0;
}

static int p_expect(cc_par_t *p, cc_token_kind_t kind) {
    if (p_match(p, kind)) return 1;
    if (kind == CC_TOK_SEMI) p_fail(p, p->pos, MSG_SEMI);
    else if (kind == CC_TOK_RPAREN) p_fail(p, p->pos, MSG_RPAREN);
    else p_fail(p, p->pos, MSG_UNEXP);
    return 0;
}

static int p_new(cc_par_t *p, cc_ast_kind_t kind, int tok_i) {
    cc_ast_node_t *n;
    if (p->nnodes >= p->max_nodes) {
        p_fail(p, p->pos, MSG_LARGE);
        return -1;
    }
    n = &p->nodes[p->nnodes];
    n->kind = kind;
    n->value = 0;
    n->left = -1;
    n->right = -1;
    n->third = -1;
    n->next = -1;
    n->op = CC_TOK_EOF;
    n->token_index = (tok_i < 0) ? 0u : (uint32_t)tok_i;
    p->nnodes++;
    return p->nnodes - 1;
}

static int p_expr(cc_par_t *p);
static int p_stmt(cc_par_t *p);

/* kind of the token `ahead` positions past the current one (EOF past the end) */
static int p_peek(const cc_par_t *p, int ahead) {
    int i = p->pos + ahead;
    if (i < 0 || i >= p->ntok) return (int)CC_TOK_EOF;
    return (int)p->toks[i].kind;
}

/* 1 when token tok_i spells exactly `word` */
static int p_tok_is(const cc_par_t *p, int tok_i, const char *word) {
    const cc_token_t *t;
    uint32_t n = (uint32_t)strlen(word);
    if (tok_i < 0 || tok_i >= p->ntok) return 0;
    t = &p->toks[tok_i];
    return t->length == n && strncmp(&p->src[t->offset], word, (size_t)n) == 0;
}

static int p_is_assign_op(int k) {
    return k == (int)CC_TOK_ASSIGN || k == (int)CC_TOK_PLUS_ASSIGN || k == (int)CC_TOK_MINUS_ASSIGN ||
           k == (int)CC_TOK_STAR_ASSIGN || k == (int)CC_TOK_SLASH_ASSIGN || k == (int)CC_TOK_PERCENT_ASSIGN ||
           k == (int)CC_TOK_AMP_ASSIGN || k == (int)CC_TOK_PIPE_ASSIGN || k == (int)CC_TOK_CARET_ASSIGN ||
           k == (int)CC_TOK_SHL_ASSIGN || k == (int)CC_TOK_SHR_ASSIGN;
}

/* A sub-expression one level down: argument, index or parenthesised group. */
static int p_sub_expr(cc_par_t *p) {
    int e;
    if (p->depth >= CC_MAX_DEPTH) return p_fail(p, p->pos, MSG_NESTING);
    p->depth++;
    e = p_expr(p);
    p->depth--;
    return e;
}

static int p_primary(cc_par_t *p) {
    int tok_i = p->pos;
    int node;
    if (p_match(p, CC_TOK_NUMBER)) {
        const cc_token_t *t = &p->toks[tok_i];
        node = p_new(p, CC_AST_LITERAL, tok_i);
        if (node < 0) return -1;
        p->nodes[node].value = parse_number_text(&p->src[t->offset], t->length);
        return node;
    }
    if (p_match(p, CC_TOK_CHAR)) {
        node = p_new(p, CC_AST_LITERAL, tok_i);
        if (node < 0) return -1;
        p->nodes[node].value = char_token_value(p->src, &p->toks[tok_i]);
        return node;
    }
    if (p_match(p, CC_TOK_STRING)) {
        node = p_new(p, CC_AST_LITERAL, tok_i);
        return node;
    }
    if (p_kind(p) == (int)CC_TOK_IDENT) {
        int after = p_peek(p, 1);
        int ident;
        p->pos++;
        if (after == (int)CC_TOK_LPAREN) {
            int call = p_new(p, CC_AST_CALL, tok_i);        /* parent first, then its IDENT */
            int first = -1, last = -1;
            if (call < 0) return -1;
            ident = p_new(p, CC_AST_IDENT, tok_i);
            if (ident < 0) return -1;
            p->nodes[call].left = ident;
            p->pos++; /* '(' */
            if (!p_match(p, CC_TOK_RPAREN)) {
                for (;;) {
                    int arg = p_sub_expr(p);
                    if (arg < 0) return -1;
                    if (first < 0) first = arg;
                    else p->nodes[last].next = arg;
                    last = arg;
                    if (p_match(p, CC_TOK_COMMA)) continue;
                    if (!p_expect(p, CC_TOK_RPAREN)) return -1;
                    break;
                }
            }
            p->nodes[call].right = first;
            return call;
        }
        if (after == (int)CC_TOK_LBRACKET) {
            int index = p_new(p, CC_AST_INDEX, tok_i);
            int e;
            if (index < 0) return -1;
            ident = p_new(p, CC_AST_IDENT, tok_i);
            if (ident < 0) return -1;
            p->pos++; /* '[' */
            e = p_sub_expr(p);
            if (e < 0) return -1;
            if (!p_expect(p, CC_TOK_RBRACKET)) return -1;
            p->nodes[index].left = ident;
            p->nodes[index].right = e;
            return index;
        }
        ident = p_new(p, CC_AST_IDENT, tok_i);
        return ident;
    }
    if (p_match(p, CC_TOK_LPAREN)) {
        int e = p_sub_expr(p);
        if (e < 0) return -1;
        if (!p_expect(p, CC_TOK_RPAREN)) return -1;
        return e;
    }
    return p_fail(p, tok_i, MSG_UNEXP);
}

static int p_postfix(cc_par_t *p) {
    int e = p_primary(p);
    if (e < 0) return -1;
    if (p_kind(p) == (int)CC_TOK_INC || p_kind(p) == (int)CC_TOK_DEC) {
        int tok_i = p->pos;
        int node;
        cc_token_kind_t op = (cc_token_kind_t)p_kind(p);
        if (p->nodes[e].kind != CC_AST_IDENT && p->nodes[e].kind != CC_AST_INDEX) {
            return p_fail(p, tok_i, MSG_UNEXP);
        }
        p->pos++;
        node = p_new(p, CC_AST_POSTFIX, tok_i);
        if (node < 0) return -1;
        p->nodes[node].op = op;
        p->nodes[node].left = e;
        return node;
    }
    return e;
}

static int p_unary(cc_par_t *p) {
    if (p->depth >= CC_MAX_DEPTH) return p_fail(p, p->pos, MSG_NESTING);
    int tok_i = p->pos;
    int k = p_kind(p);
    if (k == (int)CC_TOK_MINUS || k == (int)CC_TOK_NOT || k == (int)CC_TOK_TILDE || k == (int)CC_TOK_PLUS ||
        k == (int)CC_TOK_INC || k == (int)CC_TOK_DEC) {
        int node, rhs;
        p->pos++;
        p->depth++;
        rhs = p_unary(p);
        p->depth--;
        if (rhs < 0) return -1;
        if ((k == (int)CC_TOK_INC || k == (int)CC_TOK_DEC) &&
            p->nodes[rhs].kind != CC_AST_IDENT && p->nodes[rhs].kind != CC_AST_INDEX) {
            return p_fail(p, tok_i, MSG_UNEXP);
        }
        node = p_new(p, CC_AST_UNOP, tok_i);
        if (node < 0) return -1;
        p->nodes[node].op = (cc_token_kind_t)k;
        p->nodes[node].left = rhs;
        return node;
    }
    return p_postfix(p);
}

static int p_bin_ltr(cc_par_t *p, int (*sub)(cc_par_t *), const cc_token_kind_t *ops, int nops) {
    int lhs = sub(p);
    if (lhs < 0) return -1;
    for (;;) {
        int k = p_kind(p);
        int i, hit = 0;
        for (i = 0; i < nops; ++i) {
            if (k == (int)ops[i]) { hit = 1; break; }
        }
        if (!hit) break;
        {
            int op_tok = p->pos;
            int rhs, node;
            p->pos++;
            rhs = sub(p);
            if (rhs < 0) return -1;
            node = p_new(p, CC_AST_BINOP, op_tok);
            if (node < 0) return -1;
            p->nodes[node].op = (cc_token_kind_t)k;
            p->nodes[node].left = lhs;
            p->nodes[node].right = rhs;
            lhs = node;
        }
    }
    return lhs;
}

static int p_term(cc_par_t *p) {
    static const cc_token_kind_t ops[] = {CC_TOK_STAR, CC_TOK_SLASH, CC_TOK_PERCENT};
    return p_bin_ltr(p, p_unary, ops, 3);
}
static int p_additive(cc_par_t *p) {
    static const cc_token_kind_t ops[] = {CC_TOK_PLUS, CC_TOK_MINUS};
    return p_bin_ltr(p, p_term, ops, 2);
}
static int p_shift(cc_par_t *p) {
    static const cc_token_kind_t ops[] = {CC_TOK_SHL, CC_TOK_SHR};
    return p_bin_ltr(p, p_additive, ops, 2);
}
static int p_relational(cc_par_t *p) {
    static const cc_token_kind_t ops[] = {CC_TOK_LT, CC_TOK_LE, CC_TOK_GT, CC_TOK_GE};
    return p_bin_ltr(p, p_shift, ops, 4);
}
static int p_equality(cc_par_t *p) {
    static const cc_token_kind_t ops[] = {CC_TOK_EQ, CC_TOK_NE};
    return p_bin_ltr(p, p_relational, ops, 2);
}
static int p_bitand(cc_par_t *p) {
    static const cc_token_kind_t ops[] = {CC_TOK_AMP};
    return p_bin_ltr(p, p_equality, ops, 1);
}
static int p_bitxor(cc_par_t *p) {
    static const cc_token_kind_t ops[] = {CC_TOK_CARET};
    return p_bin_ltr(p, p_bitand, ops, 1);
}
static int p_bitor(cc_par_t *p) {
    static const cc_token_kind_t ops[] = {CC_TOK_PIPE};
    return p_bin_ltr(p, p_bitxor, ops, 1);
}
static int p_logand(cc_par_t *p) {
    static const cc_token_kind_t ops[] = {CC_TOK_AND_AND};
    return p_bin_ltr(p, p_bitor, ops, 1);
}
static int p_logor(cc_par_t *p) {
    static const cc_token_kind_t ops[] = {CC_TOK_OR_OR};
    return p_bin_ltr(p, p_logand, ops, 1);
}

static int p_assign(cc_par_t *p) {
    int lhs = p_logor(p);
    if (lhs < 0) return -1;
    if (p_is_assign_op(p_kind(p))) {
        int op_tok = p->pos;
        cc_token_kind_t op = (cc_token_kind_t)p_kind(p);
        int rhs, node;
        if (p->nodes[lhs].kind != CC_AST_IDENT && p->nodes[lhs].kind != CC_AST_INDEX) {
            return p_fail(p, op_tok, MSG_UNEXP);
        }
        p->pos++;
        rhs = p_assign(p);
        if (rhs < 0) return -1;
        node = p_new(p, CC_AST_ASSIGN, op_tok);
        if (node < 0) return -1;
        p->nodes[node].op = op;
        p->nodes[node].left = lhs;
        p->nodes[node].right = rhs;
        return node;
    }
    return lhs;
}

static int p_expr(cc_par_t *p) { return p_assign(p); }

/* `type` already consumed; parses `ident [= expr] ;` (or, with want_semi=0, without the `;`). */
static int p_local_decl(cc_par_t *p, cc_token_kind_t type_tok, int want_semi) {
    int node, ident;
    int ident_tok = p->pos;
    if (p_kind(p) != (int)CC_TOK_IDENT) return p_fail(p, p->pos, MSG_UNEXP);
    p->pos++;
    node = p_new(p, CC_AST_VAR_DECL, ident_tok);
    if (node < 0) return -1;
    ident = p_new(p, CC_AST_IDENT, ident_tok);
    if (ident < 0) return -1;
    p->nodes[node].op = type_tok;
    p->nodes[node].left = ident;
    if (p_kind(p) == (int)CC_TOK_LBRACKET) return p_fail(p, p->pos, MSG_LOCARR);
    if (p_match(p, CC_TOK_ASSIGN)) {
        int init = p_expr(p);
        if (init < 0) return -1;
        p->nodes[node].right = init;
    }
    if (want_semi && !p_expect(p, CC_TOK_SEMI)) return -1;
    return node;
}

static int p_block(cc_par_t *p) {
    int first = -1, last = -1;
    int block = p_new(p, CC_AST_PROGRAM, p->pos);
    if (block < 0) return -1;
    if (!p_expect(p, CC_TOK_LBRACE)) return -1;
    while (p_kind(p) != (int)CC_TOK_RBRACE) {
        int st;
        if (p_kind(p) == (int)CC_TOK_EOF) return p_fail(p, p->pos, MSG_UNEXP);
        st = p_stmt(p);
        if (st < 0) return -1;
        if (first < 0) first = st;
        else p->nodes[last].next = st;
        last = st;
    }
    p->pos++; /* '}' */
    p->nodes[block].left = first;
    return block;
}

static int p_for(cc_par_t *p) {
    int node = p_new(p, CC_AST_FOR, p->pos);
    int init = -1, cond = -1, step = -1, body;
    if (node < 0) return -1;
    p->pos++; /* for */
    if (!p_expect(p, CC_TOK_LPAREN)) return -1;
    if (p_kind(p) == (int)CC_TOK_KW_INT || p_kind(p) == (int)CC_TOK_KW_CHAR) {
        cc_token_kind_t t = (cc_token_kind_t)p_kind(p);
        p->pos++;
        init = p_local_decl(p, t, 1);
        if (init < 0) return -1;
    } else if (!p_match(p, CC_TOK_SEMI)) {
        init = p_expr(p);
        if (init < 0) return -1;
        if (!p_expect(p, CC_TOK_SEMI)) return -1;
    }
    if (!p_match(p, CC_TOK_SEMI)) {
        cond = p_expr(p);
        if (cond < 0) return -1;
        if (!p_expect(p, CC_TOK_SEMI)) return -1;
    }
    if (!p_match(p, CC_TOK_RPAREN)) {
        step = p_expr(p);
        if (step < 0) return -1;
        if (!p_expect(p, CC_TOK_RPAREN)) return -1;
    }
    body = p_stmt(p);
    if (body < 0) return -1;
    p->nodes[node].left = init;
    p->nodes[node].right = cond;
    p->nodes[node].third = step;
    p->nodes[node].value = body;
    return node;
}

static int p_stmt(cc_par_t *p) {
    int k = p_kind(p);
    if (k == (int)CC_TOK_LBRACE) return p_block(p);
    if (k == (int)CC_TOK_KW_IF) {
        int node = p_new(p, CC_AST_IF, p->pos);
        int cond, then_n;
        if (node < 0) return -1;
        p->pos++;
        if (!p_expect(p, CC_TOK_LPAREN)) return -1;
        cond = p_expr(p);
        if (cond < 0) return -1;
        if (!p_expect(p, CC_TOK_RPAREN)) return -1;
        then_n = p_stmt(p);
        if (then_n < 0) return -1;
        p->nodes[node].left = cond;
        p->nodes[node].right = then_n;
        if (p_match(p, CC_TOK_KW_ELSE)) {
            int else_n = p_stmt(p);
            if (else_n < 0) return -1;
            p->nodes[node].third = else_n;
        }
        return node;
    }
    if (k == (int)CC_TOK_KW_WHILE) {
        int node = p_new(p, CC_AST_WHILE, p->pos);
        int cond, body;
        if (node < 0) return -1;
        p->pos++;
        if (!p_expect(p, CC_TOK_LPAREN)) return -1;
        cond = p_expr(p);
        if (cond < 0) return -1;
        if (!p_expect(p, CC_TOK_RPAREN)) return -1;
        body = p_stmt(p);
        if (body < 0) return -1;
        p->nodes[node].left = cond;
        p->nodes[node].right = body;
        return node;
    }
    if (k == (int)CC_TOK_KW_DO) {
        int node = p_new(p, CC_AST_DO_WHILE, p->pos);
        int cond, body;
        if (node < 0) return -1;
        p->pos++;
        body = p_stmt(p);
        if (body < 0) return -1;
        if (!p_expect(p, CC_TOK_KW_WHILE)) return -1;
        if (!p_expect(p, CC_TOK_LPAREN)) return -1;
        cond = p_expr(p);
        if (cond < 0) return -1;
        if (!p_expect(p, CC_TOK_RPAREN)) return -1;
        if (!p_expect(p, CC_TOK_SEMI)) return -1;
        p->nodes[node].left = cond;
        p->nodes[node].right = body;
        return node;
    }
    if (k == (int)CC_TOK_KW_FOR) return p_for(p);
    if (k == (int)CC_TOK_KW_BREAK || k == (int)CC_TOK_KW_CONTINUE) {
        int node = p_new(p, k == (int)CC_TOK_KW_BREAK ? CC_AST_BREAK : CC_AST_CONTINUE, p->pos);
        if (node < 0) return -1;
        p->pos++;
        if (!p_expect(p, CC_TOK_SEMI)) return -1;
        return node;
    }
    if (k == (int)CC_TOK_KW_RETURN) {
        int node = p_new(p, CC_AST_RETURN, p->pos);
        if (node < 0) return -1;
        p->pos++;
        if (!p_match(p, CC_TOK_SEMI)) {
            int e = p_expr(p);
            if (e < 0) return -1;
            if (!p_expect(p, CC_TOK_SEMI)) return -1;
            p->nodes[node].left = e;
        }
        return node;
    }
    if (k == (int)CC_TOK_KW_INT || k == (int)CC_TOK_KW_CHAR) {
        p->pos++;
        return p_local_decl(p, (cc_token_kind_t)k, 1);
    }
    if (k == (int)CC_TOK_SEMI) {
        int node = p_new(p, CC_AST_UNKNOWN, p->pos);
        p->pos++;
        return node;
    }
    {
        int e = p_expr(p);
        if (e < 0) return -1;
        if (!p_expect(p, CC_TOK_SEMI)) return -1;
        return e;
    }
}

/* Global declaration or function definition. */
static int p_top_decl(cc_par_t *p) {
    int at_expr = -1;
    cc_token_kind_t type_tok;
    int ident_tok, ident, decl;

    if (p_kind(p) == (int)CC_TOK_KW_AT) {
        p->pos++;
        if (!p_expect(p, CC_TOK_LPAREN)) return -1;
        at_expr = p_expr(p);
        if (at_expr < 0) return -1;
        if (!p_expect(p, CC_TOK_RPAREN)) return -1;
    }
    if (p_kind(p) != (int)CC_TOK_KW_INT && p_kind(p) != (int)CC_TOK_KW_CHAR) {
        return p_fail(p, p->pos, MSG_UNEXP);
    }
    type_tok = (cc_token_kind_t)p_kind(p);
    p->pos++;
    ident_tok = p->pos;
    if (p_kind(p) != (int)CC_TOK_IDENT) return p_fail(p, p->pos, MSG_UNEXP);
    p->pos++;
    /* The declaration node precedes its IDENT child (pre-order: root=0, first decl=1). Its kind is
     * refined below once we know whether this is a function, an array or a scalar. */
    decl = p_new(p, CC_AST_VAR_DECL, ident_tok);
    if (decl < 0) return -1;
    ident = p_new(p, CC_AST_IDENT, ident_tok);
    if (ident < 0) return -1;

    if (p_match(p, CC_TOK_LPAREN)) {
        int func = decl;
        int pfirst = -1, plast = -1, nparams = 0, body;
        p->nodes[func].kind = CC_AST_FUNC_DECL;
        if (at_expr >= 0) return p_fail(p, ident_tok, MSG_UNEXP);
        p->nodes[func].op = type_tok;
        p->nodes[func].left = ident;
        if (p_kind(p) == (int)CC_TOK_IDENT && p_tok_is(p, p->pos, "void") && p_peek(p, 1) == (int)CC_TOK_RPAREN) {
            p->pos += 2;                                  /* `(void)` = no parameters (host-C compatible) */
        } else if (!p_match(p, CC_TOK_RPAREN)) {
            for (;;) {
                cc_token_kind_t ptype;
                int pident, pdecl, ptok;
                if (p_kind(p) != (int)CC_TOK_KW_INT && p_kind(p) != (int)CC_TOK_KW_CHAR) {
                    return p_fail(p, p->pos, MSG_UNEXP);
                }
                ptype = (cc_token_kind_t)p_kind(p);
                p->pos++;
                ptok = p->pos;
                if (p_kind(p) != (int)CC_TOK_IDENT) return p_fail(p, p->pos, MSG_UNEXP);
                p->pos++;
                if (nparams >= CC_MAX_PARAMS) return p_fail(p, ptok, MSG_UNEXP);
                pdecl = p_new(p, CC_AST_VAR_DECL, ptok);
                if (pdecl < 0) return -1;
                pident = p_new(p, CC_AST_IDENT, ptok);
                if (pident < 0) return -1;
                p->nodes[pdecl].op = ptype;
                p->nodes[pdecl].left = pident;
                if (pfirst < 0) pfirst = pdecl;
                else p->nodes[plast].next = pdecl;
                plast = pdecl;
                nparams++;
                if (p_match(p, CC_TOK_COMMA)) continue;
                if (!p_expect(p, CC_TOK_RPAREN)) return -1;
                break;
            }
        }
        if (p_kind(p) != (int)CC_TOK_LBRACE) return p_fail(p, p->pos, MSG_UNEXP);
        body = p_block(p);
        if (body < 0) return -1;
        p->nodes[func].right = pfirst;
        p->nodes[func].third = body;
        p->nodes[func].value = nparams;
        return func;
    }

    if (p_match(p, CC_TOK_LBRACKET)) {
        int32_t count = -1;
        int init = -1;
        p->nodes[decl].kind = CC_AST_ARRAY_DECL;
        p->nodes[decl].op = type_tok;
        p->nodes[decl].left = ident;
        p->nodes[decl].third = at_expr;
        if (!p_match(p, CC_TOK_RBRACKET)) {
            int size_tok = p->pos;
            if (p_kind(p) != (int)CC_TOK_NUMBER) return p_fail(p, size_tok, MSG_UNEXP);
            count = parse_number_text(&p->src[p->toks[size_tok].offset], p->toks[size_tok].length);
            p->pos++;
            if (!p_expect(p, CC_TOK_RBRACKET)) return -1;
        }
        if (p_match(p, CC_TOK_ASSIGN)) {
            if (p_kind(p) == (int)CC_TOK_STRING) {
                init = p_new(p, CC_AST_LITERAL, p->pos);
                if (init < 0) return -1;
                p->pos++;
                if (count < 0) {
                    const cc_token_t *t = &p->toks[p->nodes[init].token_index];
                    uint8_t tmp[1];
                    int n = decode_escaped(&p->src[t->offset + 1u], t->length - 2u, tmp, 0);
                    count = n + 1;
                }
            } else if (p_match(p, CC_TOK_LBRACE)) {
                int first = -1, last = -1;
                int32_t n = 0;
                if (!p_match(p, CC_TOK_RBRACE)) {
                    for (;;) {
                        int e = p_expr(p);
                        if (e < 0) return -1;
                        if (first < 0) first = e;
                        else p->nodes[last].next = e;
                        last = e;
                        n++;
                        if (p_match(p, CC_TOK_COMMA)) {
                            if (p_match(p, CC_TOK_RBRACE)) break;
                            continue;
                        }
                        if (p_kind(p) != (int)CC_TOK_RBRACE) return p_fail(p, p->pos, MSG_UNEXP);
                        p->pos++;
                        break;
                    }
                }
                init = first;
                if (count < 0) count = n;
            } else {
                return p_fail(p, p->pos, MSG_UNEXP);
            }
        }
        if (count < 0) return p_fail(p, ident_tok, MSG_UNEXP);
        p->nodes[decl].value = count;
        p->nodes[decl].right = init;
        if (!p_expect(p, CC_TOK_SEMI)) return -1;
        return decl;
    }

    p->nodes[decl].kind = CC_AST_VAR_DECL;
    p->nodes[decl].op = type_tok;
    p->nodes[decl].left = ident;
    p->nodes[decl].third = at_expr;
    if (p_match(p, CC_TOK_ASSIGN)) {
        int init;
        if (p_kind(p) == (int)CC_TOK_STRING) return p_fail(p, p->pos, MSG_UNEXP);
        init = p_expr(p);
        if (init < 0) return -1;
        p->nodes[decl].right = init;
    }
    if (!p_expect(p, CC_TOK_SEMI)) return -1;
    return decl;
}

static int parse_run(const char *src, const cc_token_t *tokens, int token_count, cc_ast_node_t *nodes,
                     int max_nodes, cc_diag_t *d) {
    cc_par_t p;
    int root;
    int first = -1, last = -1;
    if (src == NULL || tokens == NULL || token_count <= 0 || nodes == NULL || max_nodes <= 0) {
        diag_set(d, 1u, 1u, MSG_UNEXP, NULL);
        return -1;
    }
    memset(&p, 0, sizeof(p));
    p.src = src;
    p.toks = tokens;
    p.ntok = token_count;
    p.nodes = nodes;
    p.max_nodes = max_nodes;
    p.diag = d;
    root = p_new(&p, CC_AST_PROGRAM, 0);
    if (root < 0) return -1;
    while (p_kind(&p) != (int)CC_TOK_EOF) {
        int decl = p_top_decl(&p);
        if (decl < 0) return -1;
        if (first < 0) first = decl;
        else p.nodes[last].next = decl;
        last = decl;
    }
    p.nodes[root].left = first;
    return p.nnodes;
}

int cc_parse(const char *src, const cc_token_t *tokens, int token_count, cc_ast_node_t *nodes, int max_nodes) {
    cc_diag_t d;
    memset(&d, 0, sizeof(d));
    return parse_run(src, tokens, token_count, nodes, max_nodes, &d);
}

/* ------------------------------------------------------------------------------------------ */
/* Code generator                                                                              */
/* ------------------------------------------------------------------------------------------ */

enum { FIX_FUNC = 0, FIX_GLOBAL = 1, FIX_RT = 2, FIX_STRING = 3 };

enum {
    RT_MUL = 0, RT_DIV, RT_MOD, RT_UDIV, RT_SHL, RT_SHR,
    RT_EQ, RT_NE, RT_LT, RT_GT, RT_LE, RT_GE,
    RT_NOT, RT_BOOL, RT_NEG, RT_COM, RT_PUTS, RT_INP, RT_OUTP,
    RT_COUNT
};

typedef struct {
    char name[CC_NAME_MAX];
    int is_char;
    int is_array;
    int is_at;
    int32_t at_addr;      /* absolute address when is_at */
    int32_t count;        /* element count (1 for scalars) */
    int32_t offset;       /* byte offset inside the data segment (when !is_at) */
    int node;
} cg_global_t;

typedef struct {
    char name[CC_NAME_MAX];
    int node;
    int addr;             /* output offset of the entry point */
    int nparams;
} cg_func_t;

typedef struct {
    int pos;              /* output offset of the 16-bit field */
    int kind;
    int idx;
    int32_t addend;
} cg_fix_t;

typedef struct {
    char name[CC_NAME_MAX];
    int offset;           /* frame offset (bytes above the post-prologue SP) */
    int is_char;
} cg_local_t;

typedef struct {
    int brk[CC_LOOP_PATCHES];
    int nbrk;
    int cont[CC_LOOP_PATCHES];
    int ncont;
} cg_loop_t;

/* lvalue descriptor */
enum { LV_LOCAL = 0, LV_GLOBAL = 1, LV_INDEX = 2 };
typedef struct {
    int kind;
    int is_char;
    int local_off;        /* LV_LOCAL */
    int gidx;             /* LV_GLOBAL / LV_INDEX */
    int index_node;       /* LV_INDEX: index expression (-1 when const_index is valid) */
    int32_t const_index;  /* LV_INDEX with a constant index */
    int has_const_index;
} cg_lv_t;

static struct {
    const char *src;
    const cc_token_t *toks;
    int ntok;
    const cc_ast_node_t *nodes;
    int nnodes;
    cc_diag_t *diag;
    int failed;

    uint8_t out[CC_OUT_CAP];
    int len;
    int overflow;

    cg_global_t globals[CC_MAX_GLOBALS];
    int nglobals;
    int32_t data_size;

    cg_func_t funcs[CC_MAX_FUNCS];
    int nfuncs;

    cg_fix_t fix[CC_MAX_FIXUPS];
    int nfix;
    int fix_overflow;

    cg_local_t locals[CC_MAX_LOCALS + CC_MAX_PARAMS + 1];
    int nlocals;
    int scope_marks[CC_SCOPE_DEPTH];
    int nscopes;
    int depth;            /* bytes pushed beyond the frame base */
    int cur_nslots;
    int slot_counter;
    int slot_overflow_tok;
    int32_t decl_slot[CC_MAX_NODES];

    cg_loop_t loops[CC_LOOP_DEPTH];
    int nloops;
    int last_was_return;

    uint8_t strpool[CC_STRPOOL];
    int32_t stroff[CC_MAX_STRINGS];
    int32_t strlen_[CC_MAX_STRINGS];
    int nstr;
    int32_t strpool_len;

    int rt_used[RT_COUNT];
    int rt_addr[RT_COUNT];
    int rt_emitted[RT_COUNT];
    int expr_depth;           /* gen_expr nesting; the AST can be as deep as the source allows */
} G;

static void cg_err_tok(uint32_t tok_i, const char *msg, const char *name) {
    if (G.failed) return;
    G.failed = 1;
    if ((int)tok_i >= G.ntok) tok_i = (uint32_t)(G.ntok - 1);
    diag_set(G.diag, G.toks[tok_i].line, G.toks[tok_i].col, msg, name);
}

static void cg_err_node(int idx, const char *msg, const char *name) {
    if (idx < 0 || idx >= G.nnodes) cg_err_tok((uint32_t)(G.ntok - 1), msg, name);
    else cg_err_tok(G.nodes[idx].token_index, msg, name);
}

static void tok_name(uint32_t tok_i, char out[CC_NAME_MAX]) {
    const cc_token_t *t;
    uint32_t i, n;
    out[0] = '\0';
    if ((int)tok_i >= G.ntok) return;
    t = &G.toks[tok_i];
    n = t->length;
    if (n > (uint32_t)(CC_NAME_MAX - 1)) n = (uint32_t)(CC_NAME_MAX - 1);
    for (i = 0u; i < n; ++i) out[i] = G.src[t->offset + i];
    out[n] = '\0';
}

static void node_name(int idx, char out[CC_NAME_MAX]) {
    out[0] = '\0';
    if (idx < 0 || idx >= G.nnodes) return;
    tok_name(G.nodes[idx].token_index, out);
}

/* ---- byte emission ---- */

static void e1(uint8_t b) {
    if (G.len < CC_OUT_CAP) G.out[G.len++] = b;
    else G.overflow = 1;
}
static void e2(uint8_t a, uint8_t b) { e1(a); e1(b); }
static void e3(uint8_t a, uint8_t b, uint8_t c) { e1(a); e1(b); e1(c); }
static void e_imm16(uint32_t v) {
    e1((uint8_t)(v & 0xFFu));
    e1((uint8_t)((v >> 8) & 0xFFu));
}
static void e_lxi_h(int32_t v) { e1(0x21u); e_imm16((uint32_t)v & 0xFFFFu); }
static void e_lxi_d(int32_t v) { e1(0x11u); e_imm16((uint32_t)v & 0xFFFFu); }

/* emits a jump/call with a zero target; returns the offset of the address field */
static int e_jump_ph(uint8_t op) {
    int at;
    e1(op);
    at = G.len;
    e_imm16(0u);
    return at;
}
static void patch_to(int at, int target_pos) {
    uint32_t addr = CC_CODE_BASE + (uint32_t)target_pos;
    if (at < 0 || at + 1 >= CC_OUT_CAP) return;
    G.out[at] = (uint8_t)(addr & 0xFFu);
    G.out[at + 1] = (uint8_t)((addr >> 8) & 0xFFu);
}
static void e_jump_to(uint8_t op, int target_pos) {
    e1(op);
    e_imm16(CC_CODE_BASE + (uint32_t)target_pos);
}

static void add_fix(int kind, int idx, int32_t addend) {
    if (G.nfix < CC_MAX_FIXUPS) {
        G.fix[G.nfix].pos = G.len;
        G.fix[G.nfix].kind = kind;
        G.fix[G.nfix].idx = idx;
        G.fix[G.nfix].addend = addend;
        G.nfix++;
    } else {
        G.fix_overflow = 1;
    }
    e_imm16(0u);
}

static void e_call_fn(int fidx) {
    e1(0xCDu);
    add_fix(FIX_FUNC, fidx, 0);
}
static void e_call_rt(int id) {
    G.rt_used[id] = 1;
    e1(0xCDu);
    add_fix(FIX_RT, id, 0);
}
static void e_jmp_rt(int id) {
    G.rt_used[id] = 1;
    e1(0xC3u);
    add_fix(FIX_RT, id, 0);
}
/* address of global element `elem` (elements, not bytes) */
static void e_global_addr(int gidx, int32_t elem) {
    const cg_global_t *g = &G.globals[gidx];
    int32_t bytes = g->is_char ? elem : elem * 2;
    if (g->is_at) e_imm16((uint32_t)(g->at_addr + bytes) & 0xFFFFu);
    else add_fix(FIX_GLOBAL, gidx, bytes);
}
static void e_lxi_h_global(int gidx, int32_t elem) { e1(0x21u); e_global_addr(gidx, elem); }
static void e_lxi_d_global(int gidx, int32_t elem) { e1(0x11u); e_global_addr(gidx, elem); }

/* MVI A,fn ; OUT 1 */
static void e_bios(uint8_t fn) {
    e2(0x3Eu, fn);
    e2(0xD3u, 0x01u);
}
/* MOV L,A ; MVI H,0 */
static void e_a_to_hl(void) {
    e1(0x6Fu);
    e2(0x26u, 0x00u);
}
static void e_push_h(void) { e1(0xE5u); G.depth += 2; }
static void e_push_d(void) { e1(0xD5u); G.depth += 2; }
static void e_pop_d(void) { e1(0xD1u); G.depth -= 2; }
/* MOV A,H ; ORA L ; Jcc placeholder */
static int e_test_hl_jump(uint8_t op) {
    e2(0x7Cu, 0xB5u);
    return e_jump_ph(op);
}

/* ---- constant folding ---- */

static int32_t wrap16(int32_t v) {
    v &= 0xFFFF;
    if (v >= 0x8000) v -= 0x10000;
    return v;
}

static int const_eval(int idx, int32_t *out) {
    const cc_ast_node_t *n;
    int32_t a, b;
    if (idx < 0 || idx >= G.nnodes) return 0;
    n = &G.nodes[idx];
    switch (n->kind) {
        case CC_AST_LITERAL: {
            const cc_token_t *t = &G.toks[n->token_index];
            if (t->kind == CC_TOK_STRING) return 0;
            *out = wrap16(n->value);
            return 1;
        }
        case CC_AST_UNOP:
            if (n->op == CC_TOK_INC || n->op == CC_TOK_DEC) return 0;
            if (!const_eval(n->left, &a)) return 0;
            if (n->op == CC_TOK_MINUS) *out = wrap16(-a);
            else if (n->op == CC_TOK_TILDE) *out = wrap16(~a);
            else if (n->op == CC_TOK_NOT) *out = (a == 0) ? 1 : 0;
            else *out = a;
            return 1;
        case CC_AST_BINOP:
            if (!const_eval(n->left, &a)) return 0;
            if (!const_eval(n->right, &b)) return 0;
            switch (n->op) {
                case CC_TOK_PLUS: *out = wrap16(a + b); return 1;
                case CC_TOK_MINUS: *out = wrap16(a - b); return 1;
                case CC_TOK_STAR: *out = wrap16(a * b); return 1;
                case CC_TOK_SLASH: *out = (b == 0) ? 0 : wrap16(a / b); return 1;
                case CC_TOK_PERCENT: *out = (b == 0) ? 0 : wrap16(a % b); return 1;
                case CC_TOK_AMP: *out = wrap16(a & b); return 1;
                case CC_TOK_PIPE: *out = wrap16(a | b); return 1;
                case CC_TOK_CARET: *out = wrap16(a ^ b); return 1;
                case CC_TOK_SHL: *out = (b < 0 || b >= 16) ? 0 : wrap16((int32_t)(((uint32_t)a & 0xFFFFu) << b)); return 1;
                case CC_TOK_SHR: *out = (b < 0 || b >= 16) ? 0 : (int32_t)(((uint32_t)a & 0xFFFFu) >> b); return 1;
                case CC_TOK_EQ: *out = (a == b); return 1;
                case CC_TOK_NE: *out = (a != b); return 1;
                case CC_TOK_LT: *out = (a < b); return 1;
                case CC_TOK_LE: *out = (a <= b); return 1;
                case CC_TOK_GT: *out = (a > b); return 1;
                case CC_TOK_GE: *out = (a >= b); return 1;
                case CC_TOK_AND_AND: *out = (a != 0 && b != 0); return 1;
                case CC_TOK_OR_OR: *out = (a != 0 || b != 0); return 1;
                default: return 0;
            }
            return 0;
        default:
            return 0;
    }
}

/* ---- symbol tables ---- */

static int find_global(const char *name) {
    int i;
    for (i = 0; i < G.nglobals; ++i) {
        if (strcmp(G.globals[i].name, name) == 0) return i;
    }
    return -1;
}

static int find_func(const char *name) {
    int i;
    for (i = 0; i < G.nfuncs; ++i) {
        if (strcmp(G.funcs[i].name, name) == 0) return i;
    }
    return -1;
}

static int find_local(const char *name) {
    int i;
    for (i = G.nlocals - 1; i >= 0; --i) {
        if (strcmp(G.locals[i].name, name) == 0) return i;
    }
    return -1;
}

static void scope_open(void) {
    if (G.nscopes < CC_SCOPE_DEPTH) G.scope_marks[G.nscopes] = G.nlocals;
    G.nscopes++;
}
static void scope_close(void) {
    if (G.nscopes > 0) {
        G.nscopes--;
        if (G.nscopes < CC_SCOPE_DEPTH) G.nlocals = G.scope_marks[G.nscopes];
    }
}

/* bounded name copy (always NUL-terminated) */
static void name_copy(char dst[CC_NAME_MAX], const char *src) {
    int i;
    for (i = 0; i < CC_NAME_MAX - 1 && src[i] != '\0'; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

static void add_local(const char *name, int offset, int is_char) {
    cg_local_t *l;
    if (G.nlocals >= (int)(sizeof(G.locals) / sizeof(G.locals[0]))) return;
    l = &G.locals[G.nlocals++];
    name_copy(l->name, name);
    l->offset = offset;
    l->is_char = is_char;
}

/* ---- string pool ---- */

static int add_string(uint32_t tok_i) {
    const cc_token_t *t = &G.toks[tok_i];
    uint8_t buf[CC_STRPOOL];
    int n, i;
    if (t->length < 2u) return -1;
    n = decode_escaped(G.src + t->offset + 1u, t->length - 2u, buf, CC_STRPOOL - 1);
    if (n >= CC_STRPOOL - 1) {
        cg_err_tok(tok_i, MSG_LARGE, NULL);
        return -1;
    }
    buf[n] = 0u;
    n++;
    for (i = 0; i < G.nstr; ++i) {
        if (G.strlen_[i] == n && memcmp(&G.strpool[G.stroff[i]], buf, (size_t)n) == 0) return i;
    }
    if (G.nstr >= CC_MAX_STRINGS || G.strpool_len + n > CC_STRPOOL) {
        cg_err_tok(tok_i, MSG_LARGE, NULL);
        return -1;
    }
    G.stroff[G.nstr] = G.strpool_len;
    G.strlen_[G.nstr] = n;
    memcpy(&G.strpool[G.strpool_len], buf, (size_t)n);
    G.strpool_len += n;
    G.nstr++;
    return G.nstr - 1;
}

/* ---- runtime routines (emitted after user code, only when referenced) ---- */

static int rt_lab[16];
static struct { int at; int lab; } rt_fx[32];
static int rt_nfx;

static void rt_begin(void) {
    int i;
    for (i = 0; i < 16; ++i) rt_lab[i] = -1;
    rt_nfx = 0;
}
static void rt_L(int l) { rt_lab[l] = G.len; }
static void rt_J(uint8_t op, int l) {
    e1(op);
    if (rt_nfx < 32) {
        rt_fx[rt_nfx].at = G.len;
        rt_fx[rt_nfx].lab = l;
        rt_nfx++;
    }
    e_imm16(0u);
}
static void rt_end(void) {
    int i;
    for (i = 0; i < rt_nfx; ++i) {
        if (rt_lab[rt_fx[i].lab] >= 0) patch_to(rt_fx[i].at, rt_lab[rt_fx[i].lab]);
    }
}

static void emit_rt(int id) {
    rt_begin();
    G.rt_addr[id] = G.len;
    G.rt_emitted[id] = 1;
    switch (id) {
        case RT_MUL: /* HL = DE * HL */
            e2(0x44u, 0x4Du);           /* MOV B,H ; MOV C,L */
            e_lxi_h(0);
            rt_L(0);
            e2(0x78u, 0xB1u);           /* MOV A,B ; ORA C */
            e1(0xC8u);                  /* RZ */
            e2(0x79u, 0x1Fu);           /* MOV A,C ; RAR */
            rt_J(0xD2u, 1);             /* JNC skip */
            e1(0x19u);                  /* DAD D */
            rt_L(1);
            e3(0xEBu, 0x29u, 0xEBu);    /* XCHG ; DAD H ; XCHG */
            e3(0x78u, 0xB7u, 0x1Fu);    /* MOV A,B ; ORA A ; RAR */
            e1(0x47u);                  /* MOV B,A */
            e2(0x79u, 0x1Fu);           /* MOV A,C ; RAR */
            e1(0x4Fu);                  /* MOV C,A */
            rt_J(0xC3u, 0);             /* JMP loop */
            break;
        case RT_UDIV: /* HL = DE / HL (unsigned), DE = remainder */
            e2(0x44u, 0x4Du);           /* MOV B,H ; MOV C,L */
            e2(0x78u, 0xB1u);           /* MOV A,B ; ORA C */
            rt_J(0xC2u, 0);             /* JNZ go */
            e_lxi_h(0);
            e_lxi_d(0);
            e1(0xC9u);                  /* RET */
            rt_L(0);
            e_lxi_h(0);
            e2(0x3Eu, 16u);             /* MVI A,16 */
            rt_L(1);                    /* loop */
            e1(0xF5u);                  /* PUSH PSW */
            e3(0xEBu, 0x29u, 0xEBu);    /* XCHG ; DAD H ; XCHG */
            e3(0x7Du, 0x17u, 0x6Fu);    /* MOV A,L ; RAL ; MOV L,A */
            e3(0x7Cu, 0x17u, 0x67u);    /* MOV A,H ; RAL ; MOV H,A */
            e3(0x7Du, 0x91u, 0x6Fu);    /* MOV A,L ; SUB C ; MOV L,A */
            e3(0x7Cu, 0x98u, 0x67u);    /* MOV A,H ; SBB B ; MOV H,A */
            rt_J(0xD2u, 2);             /* JNC ok */
            e1(0x09u);                  /* DAD B */
            rt_J(0xC3u, 3);             /* JMP next */
            rt_L(2);
            e1(0x1Cu);                  /* INR E */
            rt_L(3);
            e2(0xF1u, 0x3Du);           /* POP PSW ; DCR A */
            rt_J(0xC2u, 1);             /* JNZ loop */
            e2(0xEBu, 0xC9u);           /* XCHG ; RET */
            break;
        case RT_DIV: /* HL = DE / HL signed, truncating; /0 -> 0 */
            e3(0x7Au, 0xACu, 0xF5u);    /* MOV A,D ; XRA H ; PUSH PSW */
            e2(0x7Au, 0xB7u);           /* MOV A,D ; ORA A */
            rt_J(0xF2u, 0);             /* JP p1 */
            e1(0xEBu);                  /* XCHG */
            e_call_rt(RT_NEG);
            e1(0xEBu);                  /* XCHG */
            rt_L(0);
            e2(0x7Cu, 0xB7u);           /* MOV A,H ; ORA A */
            rt_J(0xF2u, 1);             /* JP p2 */
            e_call_rt(RT_NEG);
            rt_L(1);
            e_call_rt(RT_UDIV);
            e2(0xF1u, 0xF0u);           /* POP PSW ; RP */
            e_jmp_rt(RT_NEG);
            break;
        case RT_MOD: /* HL = DE % HL, sign of the dividend; %0 -> 0 */
            e3(0x7Au, 0xB7u, 0xF5u);    /* MOV A,D ; ORA A ; PUSH PSW */
            rt_J(0xF2u, 0);             /* JP p1 */
            e1(0xEBu);
            e_call_rt(RT_NEG);
            e1(0xEBu);
            rt_L(0);
            e2(0x7Cu, 0xB7u);           /* MOV A,H ; ORA A */
            rt_J(0xF2u, 1);
            e_call_rt(RT_NEG);
            rt_L(1);
            e_call_rt(RT_UDIV);
            e1(0xEBu);                  /* XCHG -> HL = remainder */
            e2(0xF1u, 0xF0u);           /* POP PSW ; RP */
            e_jmp_rt(RT_NEG);
            break;
        case RT_SHL: /* HL = DE << L */
        case RT_SHR: /* HL = DE >> L (logical) */
            e2(0x7Cu, 0xB7u);           /* MOV A,H ; ORA A */
            rt_J(0xC2u, 0);             /* JNZ zero */
            e1(0x7Du);                  /* MOV A,L */
            e2(0xFEu, 16u);             /* CPI 16 */
            rt_J(0xD2u, 0);             /* JNC zero */
            e1(0xEBu);                  /* XCHG */
            e2(0xB7u, 0xC8u);           /* ORA A ; RZ */
            rt_L(1);
            if (id == RT_SHL) {
                e1(0x29u);              /* DAD H */
                e1(0x3Du);              /* DCR A */
            } else {
                e1(0x47u);              /* MOV B,A */
                e3(0x7Cu, 0xB7u, 0x1Fu);/* MOV A,H ; ORA A ; RAR */
                e1(0x67u);              /* MOV H,A */
                e2(0x7Du, 0x1Fu);       /* MOV A,L ; RAR */
                e1(0x6Fu);              /* MOV L,A */
                e2(0x78u, 0x3Du);       /* MOV A,B ; DCR A */
            }
            rt_J(0xC2u, 1);             /* JNZ loop */
            e1(0xC9u);                  /* RET */
            rt_L(0);
            e_lxi_h(0);
            e1(0xC9u);
            break;
        case RT_EQ:
        case RT_NE:
            e3(0x7Bu, 0xADu, 0x47u);    /* MOV A,E ; XRA L ; MOV B,A */
            e3(0x7Au, 0xACu, 0xB0u);    /* MOV A,D ; XRA H ; ORA B */
            e_lxi_h(0);
            e1(id == RT_EQ ? 0xC0u : 0xC8u); /* RNZ / RZ */
            e2(0x23u, 0xC9u);           /* INX H ; RET */
            break;
        case RT_LT: /* HL = (DE < HL) signed */
            e2(0x7Au, 0xACu);           /* MOV A,D ; XRA H */
            rt_J(0xFAu, 0);             /* JM diff */
            e2(0x7Bu, 0x95u);           /* MOV A,E ; SUB L */
            e2(0x7Au, 0x9Cu);           /* MOV A,D ; SBB H */
            rt_J(0xDAu, 2);             /* JC yes */
            rt_L(1);                    /* no */
            e_lxi_h(0);
            e1(0xC9u);
            rt_L(0);                    /* diff */
            e2(0x7Au, 0xB7u);           /* MOV A,D ; ORA A */
            rt_J(0xFAu, 2);             /* JM yes */
            rt_J(0xC3u, 1);             /* JMP no */
            rt_L(2);                    /* yes */
            e_lxi_h(1);
            e1(0xC9u);
            break;
        case RT_GT: /* DE > HL  ==  HL < DE */
            e1(0xEBu);
            e_jmp_rt(RT_LT);
            break;
        case RT_GE: /* !(DE < HL) */
            e_call_rt(RT_LT);
            e3(0x7Du, 0xEEu, 0x01u);    /* MOV A,L ; XRI 1 */
            e2(0x6Fu, 0xC9u);           /* MOV L,A ; RET */
            break;
        case RT_LE: /* !(DE > HL) == !(HL < DE) */
            e1(0xEBu);
            e_jmp_rt(RT_GE);
            break;
        case RT_NOT:
        case RT_BOOL:
            e2(0x7Cu, 0xB5u);           /* MOV A,H ; ORA L */
            e_lxi_h(0);
            e1(id == RT_NOT ? 0xC0u : 0xC8u); /* RNZ / RZ */
            e2(0x23u, 0xC9u);           /* INX H ; RET */
            break;
        case RT_NEG:
        case RT_COM:
            e3(0x7Du, 0x2Fu, 0x6Fu);    /* MOV A,L ; CMA ; MOV L,A */
            e3(0x7Cu, 0x2Fu, 0x67u);    /* MOV A,H ; CMA ; MOV H,A */
            if (id == RT_NEG) e1(0x23u); /* INX H */
            e1(0xC9u);
            break;
        case RT_PUTS: /* print NUL-terminated string at HL, then '\n'; HL = 0 */
            rt_L(0);
            e2(0x7Eu, 0xB7u);           /* MOV A,M ; ORA A */
            rt_J(0xCAu, 1);             /* JZ nl */
            e1(0x4Fu);                  /* MOV C,A */
            e_bios(0x02u);
            e1(0x23u);                  /* INX H */
            rt_J(0xC3u, 0);
            rt_L(1);
            e2(0x0Eu, 0x0Au);           /* MVI C,10 */
            e_bios(0x02u);
            e_lxi_h(0);
            e1(0xC9u);
            break;
        case RT_INP: { /* HL = IN L (self-modifying port byte) */
            int p;
            e1(0x7Du);                  /* MOV A,L */
            p = G.len;
            e1(0x32u);                  /* STA port */
            e_imm16(CC_CODE_BASE + (uint32_t)p + 4u);
            e2(0xDBu, 0x00u);           /* IN xx */
            e_a_to_hl();
            e1(0xC9u);
            break;
        }
        case RT_OUTP: { /* OUT E, L ; HL = 0 */
            int p;
            e1(0x7Bu);                  /* MOV A,E */
            p = G.len;
            e1(0x32u);                  /* STA port */
            e_imm16(CC_CODE_BASE + (uint32_t)p + 5u);
            e1(0x7Du);                  /* MOV A,L */
            e2(0xD3u, 0x00u);           /* OUT xx */
            e_lxi_h(0);
            e1(0xC9u);
            break;
        }
        default:
            break;
    }
    rt_end();
}

static void emit_runtime(void) {
    int changed = 1;
    while (changed) {
        int i;
        changed = 0;
        for (i = 0; i < RT_COUNT; ++i) {
            if (G.rt_used[i] && !G.rt_emitted[i]) {
                emit_rt(i);
                changed = 1;
            }
        }
    }
}

/* ---- expressions ---- */

static void gen_expr(int idx);
static void gen_expr_inner(int idx);
static void gen_expr_discard(int idx);

/* Binary operator with DE = left operand, HL = right operand -> HL. */
static void gen_binop_de_hl(cc_token_kind_t op) {
    switch (op) {
        case CC_TOK_PLUS: e1(0x19u); break;                                   /* DAD D */
        case CC_TOK_MINUS:
            e3(0x7Bu, 0x95u, 0x6Fu);                                          /* MOV A,E ; SUB L ; MOV L,A */
            e3(0x7Au, 0x9Cu, 0x67u);                                          /* MOV A,D ; SBB H ; MOV H,A */
            break;
        case CC_TOK_STAR: e_call_rt(RT_MUL); break;
        case CC_TOK_SLASH: e_call_rt(RT_DIV); break;
        case CC_TOK_PERCENT: e_call_rt(RT_MOD); break;
        case CC_TOK_AMP:
            e3(0x7Bu, 0xA5u, 0x6Fu);                                          /* MOV A,E ; ANA L ; MOV L,A */
            e3(0x7Au, 0xA4u, 0x67u);                                          /* MOV A,D ; ANA H ; MOV H,A */
            break;
        case CC_TOK_PIPE:
            e3(0x7Bu, 0xB5u, 0x6Fu);                                          /* ORA L */
            e3(0x7Au, 0xB4u, 0x67u);                                          /* ORA H */
            break;
        case CC_TOK_CARET:
            e3(0x7Bu, 0xADu, 0x6Fu);                                          /* XRA L */
            e3(0x7Au, 0xACu, 0x67u);                                          /* XRA H */
            break;
        case CC_TOK_SHL: e_call_rt(RT_SHL); break;
        case CC_TOK_SHR: e_call_rt(RT_SHR); break;
        case CC_TOK_EQ: e_call_rt(RT_EQ); break;
        case CC_TOK_NE: e_call_rt(RT_NE); break;
        case CC_TOK_LT: e_call_rt(RT_LT); break;
        case CC_TOK_GT: e_call_rt(RT_GT); break;
        case CC_TOK_LE: e_call_rt(RT_LE); break;
        case CC_TOK_GE: e_call_rt(RT_GE); break;
        default: break;
    }
}

static cc_token_kind_t compound_base_op(cc_token_kind_t op) {
    switch (op) {
        case CC_TOK_PLUS_ASSIGN: return CC_TOK_PLUS;
        case CC_TOK_MINUS_ASSIGN: return CC_TOK_MINUS;
        case CC_TOK_STAR_ASSIGN: return CC_TOK_STAR;
        case CC_TOK_SLASH_ASSIGN: return CC_TOK_SLASH;
        case CC_TOK_PERCENT_ASSIGN: return CC_TOK_PERCENT;
        case CC_TOK_AMP_ASSIGN: return CC_TOK_AMP;
        case CC_TOK_PIPE_ASSIGN: return CC_TOK_PIPE;
        case CC_TOK_CARET_ASSIGN: return CC_TOK_CARET;
        case CC_TOK_SHL_ASSIGN: return CC_TOK_SHL;
        case CC_TOK_SHR_ASSIGN: return CC_TOK_SHR;
        default: return CC_TOK_EOF;
    }
}

/* Left operand already in HL; combine with `rhs` -> HL. */
static void gen_binop_rhs(cc_token_kind_t op, int rhs) {
    int32_t cv;
    if (const_eval(rhs, &cv)) {
        if (op == CC_TOK_PLUS || op == CC_TOK_AMP || op == CC_TOK_PIPE || op == CC_TOK_CARET) {
            e_lxi_d(cv);
            gen_binop_de_hl(op);
        } else if (op == CC_TOK_MINUS) {
            e_lxi_d(wrap16(-cv));
            e1(0x19u);                                                        /* DAD D */
        } else {
            e1(0xEBu);                                                        /* XCHG */
            e_lxi_h(cv);
            gen_binop_de_hl(op);
        }
        return;
    }
    e_push_h();
    gen_expr(rhs);
    e_pop_d();
    gen_binop_de_hl(op);
}

static int resolve_lvalue(int idx, cg_lv_t *lv) {
    const cc_ast_node_t *n;
    char name[CC_NAME_MAX];
    memset(lv, 0, sizeof(*lv));
    lv->index_node = -1;
    if (idx < 0 || idx >= G.nnodes) {
        cg_err_node(idx, MSG_UNEXP, NULL);
        return -1;
    }
    n = &G.nodes[idx];
    if (n->kind == CC_AST_IDENT) {
        int li, gi;
        node_name(idx, name);
        li = find_local(name);
        if (li >= 0) {
            lv->kind = LV_LOCAL;
            lv->local_off = G.locals[li].offset;
            lv->is_char = G.locals[li].is_char;
            return 0;
        }
        gi = find_global(name);
        if (gi < 0) {
            cg_err_node(idx, MSG_UNDEF_VAR, name);
            return -1;
        }
        if (G.globals[gi].is_array) {
            cg_err_node(idx, MSG_UNEXP, NULL);
            return -1;
        }
        lv->kind = LV_GLOBAL;
        lv->gidx = gi;
        lv->is_char = G.globals[gi].is_char;
        return 0;
    }
    if (n->kind == CC_AST_INDEX) {
        int gi;
        int32_t cv;
        node_name(n->left, name);
        if (find_local(name) >= 0) {
            cg_err_node(idx, MSG_UNEXP, NULL);
            return -1;
        }
        gi = find_global(name);
        if (gi < 0) {
            cg_err_node(n->left, MSG_UNDEF_VAR, name);
            return -1;
        }
        if (!G.globals[gi].is_array) {
            cg_err_node(idx, MSG_UNEXP, NULL);
            return -1;
        }
        lv->kind = LV_INDEX;
        lv->gidx = gi;
        lv->is_char = G.globals[gi].is_char;
        if (const_eval(n->right, &cv)) {
            lv->has_const_index = 1;
            lv->const_index = cv;
        } else {
            lv->index_node = n->right;
        }
        return 0;
    }
    cg_err_node(idx, MSG_UNEXP, NULL);
    return -1;
}

/* An lvalue whose address is static or SP-relative (no index expression to evaluate). */
static int lv_is_direct(const cg_lv_t *lv) {
    return lv->kind != LV_INDEX || lv->has_const_index;
}

/* HL = address of lvalue (uses the current push depth for locals). */
static void gen_lv_addr_hl(const cg_lv_t *lv) {
    if (lv->kind == LV_LOCAL) {
        e_lxi_h(lv->local_off + G.depth);
        e1(0x39u);                                                            /* DAD SP */
    } else if (lv->kind == LV_GLOBAL) {
        e_lxi_h_global(lv->gidx, 0);
    } else if (lv->has_const_index) {
        e_lxi_h_global(lv->gidx, lv->const_index);
    } else {
        gen_expr(lv->index_node);
        if (!lv->is_char) e1(0x29u);                                          /* DAD H */
        e_lxi_d_global(lv->gidx, 0);
        e1(0x19u);                                                            /* DAD D */
    }
}

/* HL = value at address DE (DE preserved). */
static void gen_load_via_de(int is_char) {
    if (is_char) {
        e2(0x1Au, 0x6Fu);                                                     /* LDAX D ; MOV L,A */
        e2(0x26u, 0x00u);                                                     /* MVI H,0 */
    } else {
        e2(0x1Au, 0x6Fu);                                                     /* LDAX D ; MOV L,A */
        e3(0x13u, 0x1Au, 0x67u);                                              /* INX D ; LDAX D ; MOV H,A */
        e1(0x1Bu);                                                            /* DCX D */
    }
}

/* store HL at address DE; HL keeps the (truncated) stored value. */
static void gen_store_via_de(int is_char) {
    if (is_char) {
        e2(0x7Du, 0x12u);                                                     /* MOV A,L ; STAX D */
        e2(0x26u, 0x00u);                                                     /* MVI H,0 */
    } else {
        e2(0x7Du, 0x12u);                                                     /* MOV A,L ; STAX D */
        e3(0x13u, 0x7Cu, 0x12u);                                              /* INX D ; MOV A,H ; STAX D */
    }
}

/* HL = value of a direct lvalue. */
static void gen_load_direct(const cg_lv_t *lv) {
    if (lv->kind == LV_LOCAL) {
        e_lxi_h(lv->local_off + G.depth);
        e1(0x39u);                                                            /* DAD SP */
        if (lv->is_char) {
            e1(0x6Eu);                                                        /* MOV L,M */
            e2(0x26u, 0x00u);                                                 /* MVI H,0 */
        } else {
            e3(0x5Eu, 0x23u, 0x56u);                                          /* MOV E,M ; INX H ; MOV D,M */
            e1(0xEBu);                                                        /* XCHG */
        }
        return;
    }
    {
        int32_t elem = (lv->kind == LV_INDEX) ? lv->const_index : 0;
        if (lv->is_char) {
            e1(0x3Au);                                                        /* LDA addr */
            e_global_addr(lv->gidx, elem);
            e_a_to_hl();
        } else {
            e1(0x2Au);                                                        /* LHLD addr */
            e_global_addr(lv->gidx, elem);
        }
    }
}

/* Store HL into a direct lvalue; HL keeps the (truncated) stored value. */
static void gen_store_direct(const cg_lv_t *lv) {
    if (lv->kind == LV_LOCAL) {
        e1(0xEBu);                                                            /* XCHG */
        e_lxi_h(lv->local_off + G.depth);
        e1(0x39u);                                                            /* DAD SP */
        if (lv->is_char) {
            e1(0x73u);                                                        /* MOV M,E */
            e2(0x16u, 0x00u);                                                 /* MVI D,0 */
        } else {
            e3(0x73u, 0x23u, 0x72u);                                          /* MOV M,E ; INX H ; MOV M,D */
        }
        e1(0xEBu);                                                            /* XCHG */
        return;
    }
    {
        int32_t elem = (lv->kind == LV_INDEX) ? lv->const_index : 0;
        if (lv->is_char) {
            e2(0x7Du, 0x32u);                                                 /* MOV A,L ; STA addr */
            e_global_addr(lv->gidx, elem);
            e2(0x26u, 0x00u);                                                 /* MVI H,0 */
        } else {
            e1(0x22u);                                                        /* SHLD addr */
            e_global_addr(lv->gidx, elem);
        }
    }
}

static void gen_load_lvalue(const cg_lv_t *lv) {
    if (lv_is_direct(lv)) {
        gen_load_direct(lv);
    } else {
        gen_lv_addr_hl(lv);
        if (lv->is_char) {
            e1(0x6Eu);                                                        /* MOV L,M */
            e2(0x26u, 0x00u);
        } else {
            e3(0x5Eu, 0x23u, 0x56u);                                          /* MOV E,M ; INX H ; MOV D,M */
            e1(0xEBu);
        }
    }
}

static void gen_assign(int idx) {
    const cc_ast_node_t *n = &G.nodes[idx];
    cg_lv_t lv;
    if (resolve_lvalue(n->left, &lv) != 0) return;
    if (n->op == CC_TOK_ASSIGN) {
        if (lv_is_direct(&lv)) {
            gen_expr(n->right);
            gen_store_direct(&lv);
        } else {
            gen_lv_addr_hl(&lv);
            e1(0xEBu);                                                        /* XCHG */
            e_push_d();
            gen_expr(n->right);
            e_pop_d();
            gen_store_via_de(lv.is_char);
        }
        return;
    }
    {
        cc_token_kind_t op = compound_base_op(n->op);
        if (lv_is_direct(&lv)) {
            gen_load_direct(&lv);
            gen_binop_rhs(op, n->right);
            gen_store_direct(&lv);
        } else {
            gen_lv_addr_hl(&lv);
            e1(0xEBu);                                                        /* XCHG */
            e_push_d();
            gen_load_via_de(lv.is_char);
            gen_binop_rhs(op, n->right);
            e_pop_d();
            gen_store_via_de(lv.is_char);
        }
    }
}

static void gen_incdec(int lvnode, int is_inc, int prefix) {
    cg_lv_t lv;
    if (resolve_lvalue(lvnode, &lv) != 0) return;
    if (lv_is_direct(&lv)) {
        gen_load_direct(&lv);
        if (!prefix) e2(0x44u, 0x4Du);                                        /* MOV B,H ; MOV C,L */
        e1(is_inc ? 0x23u : 0x2Bu);                                           /* INX H / DCX H */
        gen_store_direct(&lv);
        if (!prefix) e2(0x60u, 0x69u);                                        /* MOV H,B ; MOV L,C */
    } else {
        gen_lv_addr_hl(&lv);
        e1(0xEBu);                                                            /* XCHG */
        gen_load_via_de(lv.is_char);
        if (!prefix) e2(0x44u, 0x4Du);
        e1(is_inc ? 0x23u : 0x2Bu);
        gen_store_via_de(lv.is_char);
        if (!prefix) e2(0x60u, 0x69u);
    }
}

static void gen_load_ident(int idx) {
    char name[CC_NAME_MAX];
    int li, gi;
    node_name(idx, name);
    li = find_local(name);
    if (li >= 0) {
        cg_lv_t lv;
        memset(&lv, 0, sizeof(lv));
        lv.kind = LV_LOCAL;
        lv.local_off = G.locals[li].offset;
        lv.is_char = G.locals[li].is_char;
        gen_load_direct(&lv);
        return;
    }
    gi = find_global(name);
    if (gi < 0) {
        cg_err_node(idx, MSG_UNDEF_VAR, name);
        return;
    }
    if (G.globals[gi].is_array) {
        e_lxi_h_global(gi, 0);                                                /* array name = its address */
        return;
    }
    {
        cg_lv_t lv;
        memset(&lv, 0, sizeof(lv));
        lv.kind = LV_GLOBAL;
        lv.gidx = gi;
        lv.is_char = G.globals[gi].is_char;
        gen_load_direct(&lv);
    }
}

/* ---- intrinsics ---- */

enum {
    IN_PUTCHAR = 0, IN_GETCHAR, IN_PUTS, IN_PEEK, IN_POKE, IN_PEEKW, IN_POKEW, IN_INP, IN_OUTP, IN_BIOS,
    IN_KBHIT, IN_VSYNC, IN_RAND, IN_TICKS, IN_KEYS, IN_TAPE, IN_SELDISK, IN_LISTDIR, IN_NAMECH, IN_NAMEND,
    IN_RUNEND, IN_DELEND, IN_CCEND, IN_ASMEND, IN_TMEND, IN_BFEND, IN_READLINE, IN_LINEGET, IN_LINELEN,
    IN_COUNT
};

static const char *const intrinsic_names[IN_COUNT] = {
    "putchar", "getchar", "puts", "peek", "poke", "peekw", "pokew", "inp", "outp", "bios",
    "kbhit", "vsync", "rand", "ticks", "keys", "tape", "seldisk", "listdir", "namech", "namend",
    "runend", "delend", "ccend", "asmend", "tmend", "bfend", "readline", "lineget", "linelen"
};

static int find_intrinsic(const char *name) {
    int i;
    for (i = 0; i < IN_COUNT; ++i) {
        if (strcmp(intrinsic_names[i], name) == 0) return i;
    }
    return -1;
}

static void gen_arg(int a) {
    if (a >= 0) gen_expr(a);
    else e_lxi_h(0);
}

/* evaluate arg into C (constant -> MVI C,k) */
static void gen_arg_to_c(int a) {
    int32_t cv;
    if (a >= 0 && const_eval(a, &cv)) {
        e2(0x0Eu, (uint8_t)(cv & 0xFF));                                      /* MVI C,k */
    } else {
        gen_arg(a);
        e1(0x4Du);                                                            /* MOV C,L */
    }
}

static void gen_zero_if(int want) {
    if (want) e_lxi_h(0);
}

static void gen_puts(int a0, int want) {
    const cc_ast_node_t *an;
    (void)want;
    if (a0 < 0) {
        e_lxi_h(0);
        e_call_rt(RT_PUTS);
        return;
    }
    an = &G.nodes[a0];
    if (an->kind == CC_AST_LITERAL && G.toks[an->token_index].kind == CC_TOK_STRING) {
        int si = add_string(an->token_index);
        if (si < 0) return;
        e1(0x21u);                                                            /* LXI H,str */
        add_fix(FIX_STRING, si, 0);
        e_call_rt(RT_PUTS);
        return;
    }
    gen_expr(a0);                                                             /* array name -> address */
    e_call_rt(RT_PUTS);
}

static void gen_intrinsic(int id, int idx, int want) {
    const cc_ast_node_t *n = &G.nodes[idx];
    int a0 = n->right;
    int a1 = (a0 >= 0) ? G.nodes[a0].next : -1;
    int32_t cv;
    switch (id) {
        case IN_PUTCHAR:
            gen_arg_to_c(a0);
            e_bios(0x02u);
            gen_zero_if(want);
            break;
        case IN_GETCHAR:
            e_bios(0x01u);
            e_a_to_hl();
            break;
        case IN_PUTS:
            gen_puts(a0, want);
            break;
        case IN_PEEK:
            if (a0 >= 0 && const_eval(a0, &cv)) {
                e1(0x3Au);                                                    /* LDA addr */
                e_imm16((uint32_t)cv & 0xFFFFu);
                e_a_to_hl();
            } else {
                gen_arg(a0);
                e1(0x6Eu);                                                    /* MOV L,M */
                e2(0x26u, 0x00u);                                             /* MVI H,0 */
            }
            break;
        case IN_POKE:
            if (a0 >= 0 && const_eval(a0, &cv)) {
                gen_arg(a1);
                e2(0x7Du, 0x32u);                                             /* MOV A,L ; STA addr */
                e_imm16((uint32_t)cv & 0xFFFFu);
            } else {
                gen_arg(a0);
                e_push_h();
                gen_arg(a1);
                e_pop_d();
                e2(0x7Du, 0x12u);                                             /* MOV A,L ; STAX D */
            }
            gen_zero_if(want);
            break;
        case IN_PEEKW:
            if (a0 >= 0 && const_eval(a0, &cv)) {
                e1(0x2Au);                                                    /* LHLD addr */
                e_imm16((uint32_t)cv & 0xFFFFu);
            } else {
                gen_arg(a0);
                e3(0x5Eu, 0x23u, 0x56u);                                      /* MOV E,M ; INX H ; MOV D,M */
                e1(0xEBu);
            }
            break;
        case IN_POKEW:
            if (a0 >= 0 && const_eval(a0, &cv)) {
                gen_arg(a1);
                e1(0x22u);                                                    /* SHLD addr */
                e_imm16((uint32_t)cv & 0xFFFFu);
            } else {
                gen_arg(a0);
                e_push_h();
                gen_arg(a1);
                e_pop_d();
                e2(0x7Du, 0x12u);                                             /* MOV A,L ; STAX D */
                e3(0x13u, 0x7Cu, 0x12u);                                      /* INX D ; MOV A,H ; STAX D */
            }
            gen_zero_if(want);
            break;
        case IN_INP:
            if (a0 >= 0 && const_eval(a0, &cv)) {
                e2(0xDBu, (uint8_t)(cv & 0xFF));                              /* IN port */
                e_a_to_hl();
            } else {
                gen_arg(a0);
                e_call_rt(RT_INP);
            }
            break;
        case IN_OUTP:
            if (a0 >= 0 && const_eval(a0, &cv)) {
                gen_arg(a1);
                e1(0x7Du);                                                    /* MOV A,L */
                e2(0xD3u, (uint8_t)(cv & 0xFF));                              /* OUT port */
                gen_zero_if(want);
            } else {
                gen_arg(a0);
                e_push_h();
                gen_arg(a1);
                e_pop_d();
                e_call_rt(RT_OUTP);
            }
            break;
        case IN_BIOS:
            if (a0 >= 0 && const_eval(a0, &cv)) {
                gen_arg_to_c(a1);
                e_bios((uint8_t)(cv & 0xFF));
            } else {
                gen_arg(a0);
                e_push_h();
                gen_arg(a1);
                e_pop_d();
                e2(0x4Du, 0x7Bu);                                             /* MOV C,L ; MOV A,E */
                e2(0xD3u, 0x01u);                                             /* OUT 1 */
            }
            e_a_to_hl();
            break;
        case IN_KBHIT:
            e_bios(0x05u);
            e_a_to_hl();
            break;
        case IN_VSYNC:
            e_bios(0x06u);
            gen_zero_if(want);
            break;
        case IN_RAND:
            e_bios(0x07u);
            e_a_to_hl();
            break;
        case IN_TICKS:
            e_bios(0x08u);
            e_a_to_hl();
            break;
        case IN_KEYS:
            e2(0xDBu, 0x03u);                                                 /* IN 3 */
            e_a_to_hl();
            break;
        case IN_TAPE:
            gen_arg(a0);
            e1(0x7Du);                                                        /* MOV A,L */
            e2(0xD3u, 0x02u);                                                 /* OUT 2 */
            gen_zero_if(want);
            break;
        case IN_SELDISK:
            gen_arg_to_c(a0);
            e_bios(0x09u);
            e_a_to_hl();                /* 0 = selected, 1 = no such disk */
            break;
        case IN_LISTDIR:
            e_bios(0x0Fu);
            gen_zero_if(want);
            break;
        case IN_NAMECH:
            gen_arg_to_c(a0);
            e_bios(0x12u);
            gen_zero_if(want);
            break;
        case IN_NAMEND: e_bios(0x13u); gen_zero_if(want); break;
        case IN_RUNEND: e_bios(0x14u); gen_zero_if(want); break;
        case IN_DELEND: e_bios(0x15u); gen_zero_if(want); break;
        case IN_CCEND: e_bios(0x16u); gen_zero_if(want); break;
        case IN_ASMEND: e_bios(0x1Au); gen_zero_if(want); break;
        case IN_TMEND: e_bios(0x1Bu); gen_zero_if(want); break;
        case IN_BFEND: e_bios(0x1Cu); gen_zero_if(want); break;
        case IN_READLINE:
            e_bios(0x17u);
            gen_zero_if(want);
            break;
        case IN_LINEGET:
            gen_arg_to_c(a0);
            e_bios(0x18u);
            e_a_to_hl();
            break;
        case IN_LINELEN:
            e_bios(0x19u);
            e_a_to_hl();
            break;
        default:
            break;
    }
}

static void gen_call(int idx, int want) {
    const cc_ast_node_t *n = &G.nodes[idx];
    char name[CC_NAME_MAX];
    int id, f, a, nargs = 0;
    node_name(n->left, name);
    id = find_intrinsic(name);
    if (id >= 0) {
        gen_intrinsic(id, idx, want);
        return;
    }
    f = find_func(name);
    if (f < 0) {
        cg_err_node(n->left, MSG_UNDEF_FN, name);
        return;
    }
    for (a = n->right; a >= 0; a = G.nodes[a].next) {
        gen_expr(a);
        e_push_h();
        nargs++;
    }
    if (nargs != G.funcs[f].nparams) {
        cg_err_node(n->left, MSG_ARGCOUNT, name);
        return;
    }
    e_call_fn(f);
    while (nargs-- > 0) e_pop_d();
}

static void gen_logical(int idx) {
    const cc_ast_node_t *n = &G.nodes[idx];
    int is_and = (n->op == CC_TOK_AND_AND);
    uint8_t jop = is_and ? 0xCAu : 0xC2u;                                     /* JZ / JNZ */
    int p1, p2, pend;
    gen_expr(n->left);
    p1 = e_test_hl_jump(jop);
    gen_expr(n->right);
    p2 = e_test_hl_jump(jop);
    e_lxi_h(is_and ? 1 : 0);
    pend = e_jump_ph(0xC3u);
    patch_to(p1, G.len);
    patch_to(p2, G.len);
    e_lxi_h(is_and ? 0 : 1);
    patch_to(pend, G.len);
}

static void gen_expr(int idx) {
    if (G.failed) return;
    if (G.expr_depth >= CC_MAX_DEPTH) {
        cg_err_node(idx, MSG_NESTING, NULL);
        return;
    }
    G.expr_depth++;
    gen_expr_inner(idx);
    G.expr_depth--;
}

static void gen_expr_inner(int idx) {
    const cc_ast_node_t *n;
    int32_t cv;
    if (G.failed) return;
    if (idx < 0 || idx >= G.nnodes) {
        e_lxi_h(0);
        return;
    }
    if (const_eval(idx, &cv)) {
        e_lxi_h(cv);
        return;
    }
    n = &G.nodes[idx];
    switch (n->kind) {
        case CC_AST_LITERAL:
            cg_err_node(idx, MSG_UNEXP, NULL);
            break;
        case CC_AST_IDENT:
            gen_load_ident(idx);
            break;
        case CC_AST_INDEX: {
            cg_lv_t lv;
            if (resolve_lvalue(idx, &lv) != 0) return;
            gen_load_lvalue(&lv);
            break;
        }
        case CC_AST_CALL:
            gen_call(idx, 1);
            break;
        case CC_AST_UNOP:
            if (n->op == CC_TOK_INC || n->op == CC_TOK_DEC) {
                gen_incdec(n->left, n->op == CC_TOK_INC, 1);
                break;
            }
            gen_expr(n->left);
            if (n->op == CC_TOK_MINUS) e_call_rt(RT_NEG);
            else if (n->op == CC_TOK_NOT) e_call_rt(RT_NOT);
            else if (n->op == CC_TOK_TILDE) e_call_rt(RT_COM);
            break;
        case CC_AST_POSTFIX:
            gen_incdec(n->left, n->op == CC_TOK_INC, 0);
            break;
        case CC_AST_BINOP:
            if (n->op == CC_TOK_AND_AND || n->op == CC_TOK_OR_OR) {
                gen_logical(idx);
                break;
            }
            gen_expr(n->left);
            gen_binop_rhs(n->op, n->right);
            break;
        case CC_AST_ASSIGN:
            gen_assign(idx);
            break;
        default:
            cg_err_node(idx, MSG_UNEXP, NULL);
            break;
    }
}

/* Expression whose value is not needed. */
static void gen_expr_discard(int idx) {
    const cc_ast_node_t *n;
    if (G.failed || idx < 0 || idx >= G.nnodes) return;
    n = &G.nodes[idx];
    if (n->kind == CC_AST_CALL) {
        gen_call(idx, 0);
        return;
    }
    if (n->kind == CC_AST_POSTFIX) {
        gen_incdec(n->left, n->op == CC_TOK_INC, 1);
        return;
    }
    if (n->kind == CC_AST_LITERAL || n->kind == CC_AST_IDENT) return;         /* no side effects */
    gen_expr(idx);
}

/* ---- statements / functions ---- */

static void gen_stmt(int idx);

static void gen_epilogue(void) {
    int n = G.cur_nslots;
    if (n <= 6) {
        while (n-- > 0) e1(0xD1u);                                            /* POP D */
    } else {
        e1(0xEBu);                                                            /* XCHG */
        e_lxi_h(2 * n);
        e2(0x39u, 0xF9u);                                                     /* DAD SP ; SPHL */
        e1(0xEBu);                                                            /* XCHG */
    }
    e1(0xC9u);                                                                /* RET */
}

static void loop_push(int idx) {
    if (G.nloops >= CC_LOOP_DEPTH) {
        cg_err_node(idx, MSG_LARGE, NULL);
        return;
    }
    G.loops[G.nloops].nbrk = 0;
    G.loops[G.nloops].ncont = 0;
    G.nloops++;
}

static void loop_add_brk(int at, int idx) {
    cg_loop_t *l;
    if (G.nloops <= 0 || G.nloops > CC_LOOP_DEPTH) return;
    l = &G.loops[G.nloops - 1];
    if (l->nbrk >= CC_LOOP_PATCHES) {
        cg_err_node(idx, MSG_LARGE, NULL);
        return;
    }
    l->brk[l->nbrk++] = at;
}

static void loop_add_cont(int at, int idx) {
    cg_loop_t *l;
    if (G.nloops <= 0 || G.nloops > CC_LOOP_DEPTH) return;
    l = &G.loops[G.nloops - 1];
    if (l->ncont >= CC_LOOP_PATCHES) {
        cg_err_node(idx, MSG_LARGE, NULL);
        return;
    }
    l->cont[l->ncont++] = at;
}

static void loop_pop(int cont_target, int brk_target) {
    cg_loop_t *l;
    int i;
    if (G.nloops <= 0) return;
    if (G.nloops <= CC_LOOP_DEPTH) {
        l = &G.loops[G.nloops - 1];
        for (i = 0; i < l->nbrk; ++i) patch_to(l->brk[i], brk_target);
        for (i = 0; i < l->ncont; ++i) patch_to(l->cont[i], cont_target);
    }
    G.nloops--;
}

/* Pre-pass: give every local declaration in a function body a stack slot. */
static void assign_slots(int idx) {
    const cc_ast_node_t *n;
    if (idx < 0 || idx >= G.nnodes) return;
    n = &G.nodes[idx];
    switch (n->kind) {
        case CC_AST_VAR_DECL:
            G.decl_slot[idx] = G.slot_counter++;
            if (G.slot_counter > CC_MAX_LOCALS && G.slot_overflow_tok < 0) {
                G.slot_overflow_tok = (int)n->token_index;
            }
            break;
        case CC_AST_PROGRAM: {
            int it;
            for (it = n->left; it >= 0; it = G.nodes[it].next) assign_slots(it);
            break;
        }
        case CC_AST_IF:
            assign_slots(n->right);
            assign_slots(n->third);
            break;
        case CC_AST_WHILE:
        case CC_AST_DO_WHILE:
            assign_slots(n->right);
            break;
        case CC_AST_FOR:
            assign_slots(n->left);
            assign_slots(n->value);
            break;
        default:
            break;
    }
}

static void gen_stmt(int idx) {
    const cc_ast_node_t *n;
    if (G.failed || idx < 0 || idx >= G.nnodes) return;
    n = &G.nodes[idx];
    G.last_was_return = 0;
    switch (n->kind) {
        case CC_AST_UNKNOWN:
            break;
        case CC_AST_PROGRAM: {
            int it;
            scope_open();
            for (it = n->left; it >= 0; it = G.nodes[it].next) gen_stmt(it);
            scope_close();
            break;
        }
        case CC_AST_VAR_DECL: {
            char name[CC_NAME_MAX];
            int slot = (int)G.decl_slot[idx];
            int is_char = (n->op == CC_TOK_KW_CHAR);
            node_name(n->left, name);
            add_local(name, 2 * slot, is_char);
            if (n->right >= 0) {
                cg_lv_t lv;
                memset(&lv, 0, sizeof(lv));
                lv.kind = LV_LOCAL;
                lv.is_char = is_char;
                lv.local_off = 2 * slot;
                lv.index_node = -1;
                gen_expr(n->right);
                gen_store_direct(&lv);
            }
            break;
        }
        case CC_AST_IF: {
            int32_t cv;
            int pelse;
            if (const_eval(n->left, &cv)) {
                if (cv != 0) gen_stmt(n->right);
                else if (n->third >= 0) gen_stmt(n->third);
                G.last_was_return = 0;
                break;
            }
            gen_expr(n->left);
            pelse = e_test_hl_jump(0xCAu);                                    /* JZ else */
            gen_stmt(n->right);
            if (n->third >= 0) {
                int pend = e_jump_ph(0xC3u);                                  /* JMP end */
                patch_to(pelse, G.len);
                gen_stmt(n->third);
                patch_to(pend, G.len);
            } else {
                patch_to(pelse, G.len);
            }
            G.last_was_return = 0;
            break;
        }
        case CC_AST_WHILE: {
            int top = G.len;
            int32_t cv;
            int always = const_eval(n->left, &cv) && cv != 0;
            loop_push(idx);
            if (!always) {
                gen_expr(n->left);
                loop_add_brk(e_test_hl_jump(0xCAu), idx);
            }
            gen_stmt(n->right);
            e_jump_to(0xC3u, top);
            loop_pop(top, G.len);
            G.last_was_return = 0;
            break;
        }
        case CC_AST_DO_WHILE: {
            int top = G.len;
            int cont_at;
            loop_push(idx);
            gen_stmt(n->right);
            cont_at = G.len;
            gen_expr(n->left);
            e2(0x7Cu, 0xB5u);                                                 /* MOV A,H ; ORA L */
            e_jump_to(0xC2u, top);                                            /* JNZ top */
            loop_pop(cont_at, G.len);
            G.last_was_return = 0;
            break;
        }
        case CC_AST_FOR: {
            int top, cont_at;
            int body = n->value;
            scope_open();
            if (n->left >= 0) {
                if (G.nodes[n->left].kind == CC_AST_VAR_DECL) gen_stmt(n->left);
                else gen_expr_discard(n->left);
            }
            top = G.len;
            loop_push(idx);
            if (n->right >= 0) {
                int32_t cv;
                if (!(const_eval(n->right, &cv) && cv != 0)) {
                    gen_expr(n->right);
                    loop_add_brk(e_test_hl_jump(0xCAu), idx);
                }
            }
            gen_stmt(body);
            cont_at = G.len;
            if (n->third >= 0) gen_expr_discard(n->third);
            e_jump_to(0xC3u, top);
            loop_pop(cont_at, G.len);
            scope_close();
            G.last_was_return = 0;
            break;
        }
        case CC_AST_BREAK:
            if (G.nloops == 0) {
                cg_err_node(idx, MSG_UNEXP, NULL);
                break;
            }
            loop_add_brk(e_jump_ph(0xC3u), idx);
            break;
        case CC_AST_CONTINUE:
            if (G.nloops == 0) {
                cg_err_node(idx, MSG_UNEXP, NULL);
                break;
            }
            loop_add_cont(e_jump_ph(0xC3u), idx);
            break;
        case CC_AST_RETURN:
            if (n->left >= 0) gen_expr(n->left);
            else e_lxi_h(0);
            gen_epilogue();
            G.last_was_return = 1;
            break;
        default:
            gen_expr_discard(idx);
            break;
    }
}

static void gen_function(int f) {
    const cc_ast_node_t *fn = &G.nodes[G.funcs[f].node];
    int np = 0, p, nslots, j;
    G.nlocals = 0;
    G.nscopes = 0;
    G.depth = 0;
    G.nloops = 0;
    G.last_was_return = 0;
    G.slot_counter = 0;
    G.slot_overflow_tok = -1;
    assign_slots(fn->third);
    nslots = G.slot_counter;
    if (nslots > CC_MAX_LOCALS) {
        cg_err_tok((uint32_t)G.slot_overflow_tok, MSG_LOCALS, NULL);
        return;
    }
    G.cur_nslots = nslots;
    for (p = fn->right; p >= 0; p = G.nodes[p].next) np++;
    G.funcs[f].addr = G.len;
    G.funcs[f].nparams = np;
    j = 0;
    for (p = fn->right; p >= 0; p = G.nodes[p].next) {
        char name[CC_NAME_MAX];
        node_name(G.nodes[p].left, name);
        add_local(name, 2 * nslots + 2 + 2 * (np - 1 - j), G.nodes[p].op == CC_TOK_KW_CHAR);
        j++;
    }
    if (nslots <= 6) {
        int k;
        for (k = 0; k < nslots; ++k) e1(0xE5u);                              /* PUSH H */
    } else {
        e_lxi_h(wrap16(-2 * nslots));
        e2(0x39u, 0xF9u);                                                     /* DAD SP ; SPHL */
    }
    gen_stmt(fn->third);
    if (!G.last_was_return) {
        e_lxi_h(0);
        gen_epilogue();
    }
}

/* ---- program ---- */

static int collect_decls(void) {
    int it;
    for (it = G.nodes[0].left; it >= 0; it = G.nodes[it].next) {
        const cc_ast_node_t *n = &G.nodes[it];
        char name[CC_NAME_MAX];
        node_name(n->left, name);
        if (n->kind == CC_AST_FUNC_DECL) {
            cg_func_t *fdef;
            if (find_func(name) >= 0) {
                cg_err_node(n->left, MSG_UNEXP, NULL);
                return -1;
            }
            if (G.nfuncs >= CC_MAX_FUNCS) {
                cg_err_node(n->left, MSG_LARGE, NULL);
                return -1;
            }
            fdef = &G.funcs[G.nfuncs++];
            name_copy(fdef->name, name);
            fdef->node = it;
            fdef->addr = -1;
            fdef->nparams = n->value;
        } else if (n->kind == CC_AST_VAR_DECL || n->kind == CC_AST_ARRAY_DECL) {
            cg_global_t *g;
            if (find_global(name) >= 0) {
                cg_err_node(n->left, MSG_UNEXP, NULL);
                return -1;
            }
            if (G.nglobals >= CC_MAX_GLOBALS) {
                cg_err_node(n->left, MSG_LARGE, NULL);
                return -1;
            }
            g = &G.globals[G.nglobals++];
            name_copy(g->name, name);
            g->is_char = (n->op == CC_TOK_KW_CHAR);
            g->is_array = (n->kind == CC_AST_ARRAY_DECL);
            g->count = g->is_array ? n->value : 1;
            g->node = it;
            if (g->count <= 0 || g->count > 65535) {
                cg_err_node(n->left, MSG_UNEXP, NULL);
                return -1;
            }
            if (n->third >= 0) {
                int32_t av;
                if (!const_eval(n->third, &av)) {
                    cg_err_node(n->third, MSG_UNEXP, NULL);
                    return -1;
                }
                g->is_at = 1;
                g->at_addr = av & 0xFFFF;
                /* An __at variable names memory the program does not own an image of, so there is
                   nowhere to put an initialiser. Say so instead of dropping it. */
                if (n->right >= 0) {
                    cg_err_node(it, MSG_AT_INIT, NULL);
                    return -1;
                }
            } else {
                g->offset = G.data_size;
                G.data_size += g->is_char ? g->count : g->count * 2;
                if (G.data_size > CC_OUT_CAP) {
                    cg_err_node(n->left, MSG_LARGE, NULL);
                    return -1;
                }
            }
        } else {
            cg_err_node(it, MSG_UNEXP, NULL);
            return -1;
        }
    }
    return 0;
}

static void put_elem(int start, int is_char, int32_t k, int32_t v) {
    if (is_char) {
        int at = start + (int)k;
        if (at >= 0 && at < CC_OUT_CAP) G.out[at] = (uint8_t)(v & 0xFF);
    } else {
        int at = start + (int)k * 2;
        if (at >= 0 && at + 1 < CC_OUT_CAP) {
            G.out[at] = (uint8_t)(v & 0xFF);
            G.out[at + 1] = (uint8_t)((v >> 8) & 0xFF);
        }
    }
}

/* Emits globals (declaration order) then the string pool. Returns the string pool offset. */
static int emit_data(void) {
    int i, str_base;
    for (i = 0; i < G.nglobals; ++i) {
        cg_global_t *g = &G.globals[i];
        const cc_ast_node_t *n = &G.nodes[g->node];
        int start, k;
        int32_t bytes;
        if (g->is_at) continue;
        start = G.len;
        bytes = g->is_char ? g->count : g->count * 2;
        for (k = 0; k < bytes; ++k) e1(0u);
        if (n->right < 0) continue;
        if (!g->is_array) {
            int32_t v;
            if (!const_eval(n->right, &v)) {
                cg_err_node(n->right, MSG_UNEXP, NULL);
                return -1;
            }
            put_elem(start, g->is_char, 0, v);
        } else {
            const cc_ast_node_t *init = &G.nodes[n->right];
            if (init->kind == CC_AST_LITERAL && G.toks[init->token_index].kind == CC_TOK_STRING) {
                static uint8_t buf[CC_STRPOOL];
                const cc_token_t *t = &G.toks[init->token_index];
                int nb = decode_escaped(G.src + t->offset + 1u, t->length - 2u, buf, CC_STRPOOL);
                int32_t e;
                if (nb > (int)sizeof buf) nb = (int)sizeof buf;   /* decode_escaped reports the
                    full decoded length, but it only ever wrote sizeof buf bytes */
                for (e = 0; e < (int32_t)nb && e < g->count; ++e) put_elem(start, g->is_char, e, (int32_t)buf[e]);
            } else {
                int e;
                int32_t idx = 0;
                for (e = n->right; e >= 0 && idx < g->count; e = G.nodes[e].next, ++idx) {
                    int32_t v;
                    if (!const_eval(e, &v)) {
                        cg_err_node(e, MSG_UNEXP, NULL);
                        return -1;
                    }
                    put_elem(start, g->is_char, idx, v);
                }
            }
        }
    }
    str_base = G.len;
    for (i = 0; i < (int)G.strpool_len; ++i) e1(G.strpool[i]);
    return str_base;
}

static void resolve_fixups(int data_base, int str_base) {
    int i;
    for (i = 0; i < G.nfix; ++i) {
        const cg_fix_t *f = &G.fix[i];
        int32_t target = 0;
        switch (f->kind) {
            case FIX_FUNC: target = (int32_t)CC_CODE_BASE + G.funcs[f->idx].addr; break;
            case FIX_GLOBAL: target = (int32_t)CC_CODE_BASE + data_base + G.globals[f->idx].offset + f->addend; break;
            case FIX_RT: target = (int32_t)CC_CODE_BASE + G.rt_addr[f->idx]; break;
            case FIX_STRING: target = (int32_t)CC_CODE_BASE + str_base + G.stroff[f->idx] + f->addend; break;
            default: break;
        }
        if (f->pos >= 0 && f->pos + 1 < CC_OUT_CAP) {
            G.out[f->pos] = (uint8_t)(target & 0xFF);
            G.out[f->pos + 1] = (uint8_t)((target >> 8) & 0xFF);
        }
    }
}

static int codegen_run(const char *src, const cc_token_t *toks, int ntok, const cc_ast_node_t *nodes, int nnodes,
                       cc_diag_t *d) {
    int f, main_f, data_base, str_base;
    memset(&G, 0, sizeof(G));
    G.src = src;
    G.toks = toks;
    G.ntok = ntok;
    G.nodes = nodes;
    G.nnodes = nnodes;
    G.diag = d;
    if (nnodes <= 0 || nodes[0].kind != CC_AST_PROGRAM) {
        diag_set(d, 1u, 1u, MSG_UNEXP, NULL);
        return -1;
    }
    if (collect_decls() != 0) return -1;
    main_f = find_func("main");
    if (main_f < 0) {
        cg_err_tok((uint32_t)(ntok - 1), MSG_UNDEF_FN, "main");
        return -1;
    }
    e1(0xCDu);                                                                /* CALL main */
    add_fix(FIX_FUNC, main_f, 0);
    e1(0x76u);                                                                /* HLT */
    for (f = 0; f < G.nfuncs; ++f) {
        gen_function(f);
        if (G.failed) return -1;
        if (G.len > CC_TPA_SIZE) break;                                       /* already too large */
    }
    if (G.len > CC_TPA_SIZE) {
        cg_err_tok((uint32_t)(ntok - 1), MSG_LARGE, NULL);
        return -1;
    }
    emit_runtime();
    data_base = G.len;
    str_base = emit_data();
    if (str_base < 0 || G.failed) return -1;
    resolve_fixups(data_base, str_base);
    if (G.overflow || G.fix_overflow || G.len > CC_TPA_SIZE) {
        cg_err_tok((uint32_t)(ntok - 1), MSG_LARGE, NULL);
        return -1;
    }
    return G.len;
}

/* ------------------------------------------------------------------------------------------ */
/* Public entry points                                                                         */
/* ------------------------------------------------------------------------------------------ */

static char g_src[CC_MAX_SRC + 1u];
static cc_token_t g_toks[CC_MAX_TOKENS];
static cc_ast_node_t g_nodes[CC_MAX_NODES];

int cc_compile_buf(const char *src, uint32_t len, uint8_t *out, uint32_t cap, char *err, uint32_t errcap) {
    cc_diag_t d;
    int ntok, nnodes, n;
    memset(&d, 0, sizeof(d));
    if (err != NULL && errcap > 0u) err[0] = '\0';
    if (src == NULL || out == NULL) {
        diag_set(&d, 1u, 1u, MSG_UNEXP, NULL);
        diag_format(&d, err, errcap);
        return -1;
    }
    if (len > CC_MAX_SRC) {
        diag_set(&d, 1u, 1u, MSG_LARGE, NULL);
        diag_format(&d, err, errcap);
        return -1;
    }
    memcpy(g_src, src, len);
    g_src[len] = '\0';
    ntok = lex_run(g_src, g_toks, CC_MAX_TOKENS, &d);
    if (ntok < 0) {
        diag_format(&d, err, errcap);
        return -1;
    }
    nnodes = parse_run(g_src, g_toks, ntok, g_nodes, CC_MAX_NODES, &d);
    if (nnodes < 0) {
        diag_format(&d, err, errcap);
        return -1;
    }
    n = codegen_run(g_src, g_toks, ntok, g_nodes, nnodes, &d);
    if (n < 0) {
        diag_format(&d, err, errcap);
        return -1;
    }
    if ((uint32_t)n > cap) {
        diag_set(&d, g_toks[ntok - 1].line, g_toks[ntok - 1].col, MSG_LARGE, NULL);
        diag_format(&d, err, errcap);
        return -1;
    }
    memcpy(out, G.out, (size_t)n);
    return n;
}

/* Host CLI convenience: reads src_path, writes out_path; prints "src.c:L:C: message" on stderr. */
int cc_compile(const char *src_path, const char *out_path) {
    static char file_buf[CC_MAX_SRC + 2u];
    static uint8_t image[CC_TPA_SIZE];
    char err[256];
    FILE *fp;
    size_t nread;
    int n;

    if (src_path == NULL || out_path == NULL) {
        fprintf(stderr, "src.c:1:1: unexpected token\n");
        return -1;
    }
    fp = fopen(src_path, "rb");
    if (fp == NULL) {
        fprintf(stderr, "%s: cannot open\n", src_path);
        return -1;
    }
    nread = fread(file_buf, 1u, sizeof(file_buf) - 1u, fp);
    fclose(fp);
    if (nread > (size_t)CC_MAX_SRC) {
        fprintf(stderr, "src.c:1:1: program too large\n");
        return -1;
    }
    file_buf[nread] = '\0';
    n = cc_compile_buf(file_buf, (uint32_t)nread, image, (uint32_t)sizeof(image), err, (uint32_t)sizeof(err));
    if (n < 0) {
        if (strncmp(err, "src.c:", 6u) == 0) {
            const char *base = strrchr(src_path, '/');
            fprintf(stderr, "%s%s\n", base ? base + 1 : src_path, err + 5);   /* real file name */
        } else {
            fprintf(stderr, "%s\n", err);
        }
        return -1;
    }
    fp = fopen(out_path, "wb");
    if (fp == NULL) {
        fprintf(stderr, "%s: cannot write\n", out_path);
        return -1;
    }
    if (fwrite(image, 1u, (size_t)n, fp) != (size_t)n) {
        fclose(fp);
        fprintf(stderr, "%s: write error\n", out_path);
        return -1;
    }
    if (fclose(fp) != 0) {
        fprintf(stderr, "%s: write error\n", out_path);
        return -1;
    }
    return 0;
}
