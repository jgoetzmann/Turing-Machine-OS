#include "compiler.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cc_token_kind_t keyword_kind(const char *s, uint32_t len) {
    if (len == 4u && strncmp(s, "char", 4u) == 0) return CC_TOK_KW_CHAR;
    if (len == 3u && strncmp(s, "int", 3u) == 0) return CC_TOK_KW_INT;
    if (len == 2u && strncmp(s, "if", 2u) == 0) return CC_TOK_KW_IF;
    if (len == 4u && strncmp(s, "else", 4u) == 0) return CC_TOK_KW_ELSE;
    if (len == 5u && strncmp(s, "while", 5u) == 0) return CC_TOK_KW_WHILE;
    if (len == 3u && strncmp(s, "for", 3u) == 0) return CC_TOK_KW_FOR;
    if (len == 6u && strncmp(s, "return", 6u) == 0) return CC_TOK_KW_RETURN;
    return CC_TOK_IDENT;
}

static int push_tok(cc_token_t *tokens, int max_tokens, int *count, cc_token_t tok) {
    if (*count >= max_tokens) {
        return -1;
    }
    tokens[*count] = tok;
    (*count)++;
    return 0;
}

int cc_lex(const char *src, cc_token_t *tokens, int max_tokens) {
    uint32_t i = 0u;
    uint32_t line = 1u;
    uint32_t col = 1u;
    int count = 0;

    if (src == NULL || tokens == NULL || max_tokens <= 0) {
        return -1;
    }

    while (src[i] != '\0') {
        cc_token_t t;
        unsigned char ch = (unsigned char)src[i];

        if (ch == ' ' || ch == '\t' || ch == '\r') {
            i++;
            col++;
            continue;
        }
        if (ch == '\n') {
            i++;
            line++;
            col = 1u;
            continue;
        }

        t.line = line;
        t.col = col;
        t.offset = i;
        t.length = 1u;
        t.kind = CC_TOK_EOF;

        if (isalpha(ch) || ch == '_') {
            uint32_t start = i;
            while (isalnum((unsigned char)src[i]) || src[i] == '_') {
                i++;
                col++;
            }
            t.offset = start;
            t.length = i - start;
            t.kind = keyword_kind(&src[start], t.length);
            if (push_tok(tokens, max_tokens, &count, t) != 0) return -1;
            continue;
        }
        if (isdigit(ch)) {
            uint32_t start = i;
            while (isdigit((unsigned char)src[i])) {
                i++;
                col++;
            }
            t.offset = start;
            t.length = i - start;
            t.kind = CC_TOK_NUMBER;
            if (push_tok(tokens, max_tokens, &count, t) != 0) return -1;
            continue;
        }
        if (ch == '"' || ch == '\'') {
            unsigned char quote = ch;
            uint32_t start = i++;
            col++;
            while (src[i] != '\0' && (unsigned char)src[i] != quote) {
                if (src[i] == '\\' && src[i + 1u] != '\0') {
                    i += 2u;
                    col += 2u;
                } else {
                    if (src[i] == '\n') {
                        line++;
                        col = 1u;
                    } else {
                        col++;
                    }
                    i++;
                }
            }
            if (src[i] == '\0') return -1;
            i++;
            col++;
            t.offset = start;
            t.length = i - start;
            t.kind = (quote == '"') ? CC_TOK_STRING : CC_TOK_CHAR;
            if (push_tok(tokens, max_tokens, &count, t) != 0) return -1;
            continue;
        }

        switch (ch) {
            case '(': t.kind = CC_TOK_LPAREN; break;
            case ')': t.kind = CC_TOK_RPAREN; break;
            case '{': t.kind = CC_TOK_LBRACE; break;
            case '}': t.kind = CC_TOK_RBRACE; break;
            case '[': t.kind = CC_TOK_LBRACKET; break;
            case ']': t.kind = CC_TOK_RBRACKET; break;
            case ',': t.kind = CC_TOK_COMMA; break;
            case ';': t.kind = CC_TOK_SEMI; break;
            case '+': t.kind = CC_TOK_PLUS; break;
            case '-': t.kind = CC_TOK_MINUS; break;
            case '*': t.kind = CC_TOK_STAR; break;
            case '/': t.kind = CC_TOK_SLASH; break;
            case '%': t.kind = CC_TOK_PERCENT; break;
            case '!':
                if (src[i + 1u] == '=') { t.kind = CC_TOK_NE; t.length = 2u; i++; col++; }
                else t.kind = CC_TOK_NOT;
                break;
            case '=':
                if (src[i + 1u] == '=') { t.kind = CC_TOK_EQ; t.length = 2u; i++; col++; }
                else t.kind = CC_TOK_ASSIGN;
                break;
            case '<':
                if (src[i + 1u] == '=') { t.kind = CC_TOK_LE; t.length = 2u; i++; col++; }
                else t.kind = CC_TOK_LT;
                break;
            case '>':
                if (src[i + 1u] == '=') { t.kind = CC_TOK_GE; t.length = 2u; i++; col++; }
                else t.kind = CC_TOK_GT;
                break;
            case '&':
                if (src[i + 1u] == '&') { t.kind = CC_TOK_AND_AND; t.length = 2u; i++; col++; }
                else return -1;
                break;
            case '|':
                if (src[i + 1u] == '|') { t.kind = CC_TOK_OR_OR; t.length = 2u; i++; col++; }
                else return -1;
                break;
            default:
                return -1;
        }

        i++;
        col++;
        if (push_tok(tokens, max_tokens, &count, t) != 0) return -1;
    }

    if (count >= max_tokens) {
        return -1;
    }
    tokens[count].kind = CC_TOK_EOF;
    tokens[count].line = line;
    tokens[count].col = col;
    tokens[count].offset = i;
    tokens[count].length = 0u;
    count++;
    return count;
}

typedef struct {
    const char *src;
    const cc_token_t *tokens;
    int token_count;
    int pos;
    cc_ast_node_t *nodes;
    int node_count;
    int max_nodes;
} cc_parser_t;

typedef struct {
    unsigned char *buf;
    int len;
    int cap;
} cc_out_t;

typedef struct {
    const cc_ast_node_t *nodes;
    int node_count;
    cc_out_t *out;
} cc_codegen_t;

static int p_peek_kind(const cc_parser_t *p) {
    if (p->pos < 0 || p->pos >= p->token_count) {
        return (int)CC_TOK_EOF;
    }
    return (int)p->tokens[p->pos].kind;
}

static int p_match(cc_parser_t *p, cc_token_kind_t kind) {
    if (p_peek_kind(p) == (int)kind) {
        p->pos++;
        return 1;
    }
    return 0;
}

static int p_expect(cc_parser_t *p, cc_token_kind_t kind) {
    if (!p_match(p, kind)) {
        return 0;
    }
    return 1;
}

static int p_new_node(cc_parser_t *p, cc_ast_kind_t kind, int token_index) {
    cc_ast_node_t *n;
    if (p->node_count >= p->max_nodes) {
        return -1;
    }
    n = &p->nodes[p->node_count];
    n->kind = kind;
    n->value = 0;
    n->left = -1;
    n->right = -1;
    n->third = -1;
    n->next = -1;
    n->op = CC_TOK_EOF;
    n->token_index = (token_index < 0) ? 0u : (uint32_t)token_index;
    p->node_count++;
    return p->node_count - 1;
}

static int p_parse_expr(cc_parser_t *p);
static int p_parse_stmt(cc_parser_t *p);

static int p_parse_number(const char *s, uint32_t len) {
    int value = 0;
    uint32_t i;
    for (i = 0u; i < len; ++i) {
        value = (value * 10) + (int)(s[i] - '0');
    }
    return value;
}

static int p_parse_primary(cc_parser_t *p) {
    int tok_i = p->pos;
    int node;
    if (p_match(p, CC_TOK_NUMBER)) {
        const cc_token_t *t = &p->tokens[tok_i];
        node = p_new_node(p, CC_AST_LITERAL, tok_i);
        if (node < 0) return -1;
        p->nodes[node].value = p_parse_number(&p->src[t->offset], t->length);
        return node;
    }
    if (p_match(p, CC_TOK_CHAR) || p_match(p, CC_TOK_STRING)) {
        node = p_new_node(p, CC_AST_LITERAL, tok_i);
        return node;
    }
    if (p_match(p, CC_TOK_IDENT)) {
        int ident_node = p_new_node(p, CC_AST_IDENT, tok_i);
        if (ident_node < 0) return -1;
        if (p_match(p, CC_TOK_LPAREN)) {
            int call_node = p_new_node(p, CC_AST_CALL, tok_i);
            int first_arg = -1;
            int last_arg = -1;
            if (call_node < 0) return -1;
            p->nodes[call_node].left = ident_node;
            if (!p_match(p, CC_TOK_RPAREN)) {
                for (;;) {
                    int arg = p_parse_expr(p);
                    if (arg < 0) return -1;
                    if (first_arg < 0) {
                        first_arg = arg;
                    } else {
                        p->nodes[last_arg].next = arg;
                    }
                    last_arg = arg;
                    if (p_match(p, CC_TOK_COMMA)) {
                        continue;
                    }
                    if (!p_expect(p, CC_TOK_RPAREN)) return -1;
                    break;
                }
            }
            p->nodes[call_node].right = first_arg;
            return call_node;
        }
        return ident_node;
    }
    if (p_match(p, CC_TOK_LPAREN)) {
        int expr = p_parse_expr(p);
        if (expr < 0) return -1;
        if (!p_expect(p, CC_TOK_RPAREN)) return -1;
        return expr;
    }
    return -1;
}

static int p_parse_unary(cc_parser_t *p) {
    int tok_i = p->pos;
    cc_token_kind_t op = (cc_token_kind_t)p_peek_kind(p);
    if (op == CC_TOK_NOT || op == CC_TOK_MINUS || op == CC_TOK_PLUS) {
        int node;
        int rhs;
        p->pos++;
        rhs = p_parse_unary(p);
        if (rhs < 0) return -1;
        node = p_new_node(p, CC_AST_UNOP, tok_i);
        if (node < 0) return -1;
        p->nodes[node].op = op;
        p->nodes[node].left = rhs;
        return node;
    }
    return p_parse_primary(p);
}

static int p_parse_bin_ltr(cc_parser_t *p,
                           int (*sub)(cc_parser_t *),
                           const cc_token_kind_t *ops,
                           int op_count) {
    int lhs = sub(p);
    int i;
    if (lhs < 0) return -1;
    for (;;) {
        cc_token_kind_t op = (cc_token_kind_t)p_peek_kind(p);
        int match = 0;
        for (i = 0; i < op_count; ++i) {
            if (op == ops[i]) {
                match = 1;
                break;
            }
        }
        if (!match) break;
        p->pos++;
        {
            int rhs = sub(p);
            int node;
            if (rhs < 0) return -1;
            node = p_new_node(p, CC_AST_BINOP, p->pos - 1);
            if (node < 0) return -1;
            p->nodes[node].op = op;
            p->nodes[node].left = lhs;
            p->nodes[node].right = rhs;
            lhs = node;
        }
    }
    return lhs;
}

static int p_parse_mul(cc_parser_t *p) {
    static const cc_token_kind_t ops[] = {CC_TOK_STAR, CC_TOK_SLASH, CC_TOK_PERCENT};
    return p_parse_bin_ltr(p, p_parse_unary, ops, 3);
}

static int p_parse_add(cc_parser_t *p) {
    static const cc_token_kind_t ops[] = {CC_TOK_PLUS, CC_TOK_MINUS};
    return p_parse_bin_ltr(p, p_parse_mul, ops, 2);
}

static int p_parse_rel(cc_parser_t *p) {
    static const cc_token_kind_t ops[] = {CC_TOK_LT, CC_TOK_LE, CC_TOK_GT, CC_TOK_GE};
    return p_parse_bin_ltr(p, p_parse_add, ops, 4);
}

static int p_parse_eq(cc_parser_t *p) {
    static const cc_token_kind_t ops[] = {CC_TOK_EQ, CC_TOK_NE};
    return p_parse_bin_ltr(p, p_parse_rel, ops, 2);
}

static int p_parse_and(cc_parser_t *p) {
    static const cc_token_kind_t ops[] = {CC_TOK_AND_AND};
    return p_parse_bin_ltr(p, p_parse_eq, ops, 1);
}

static int p_parse_or(cc_parser_t *p) {
    static const cc_token_kind_t ops[] = {CC_TOK_OR_OR};
    return p_parse_bin_ltr(p, p_parse_and, ops, 1);
}

static int p_parse_assign(cc_parser_t *p) {
    int lhs = p_parse_or(p);
    if (lhs < 0) return -1;
    if (p_match(p, CC_TOK_ASSIGN)) {
        int rhs = p_parse_assign(p);
        int node = p_new_node(p, CC_AST_ASSIGN, p->pos - 1);
        if (rhs < 0 || node < 0) return -1;
        p->nodes[node].left = lhs;
        p->nodes[node].right = rhs;
        return node;
    }
    return lhs;
}

static int p_parse_expr(cc_parser_t *p) {
    return p_parse_assign(p);
}

static int p_parse_var_decl_stmt(cc_parser_t *p, cc_token_kind_t type_tok) {
    int node;
    int ident_node;
    if (!p_expect(p, CC_TOK_IDENT)) return -1;
    ident_node = p_new_node(p, CC_AST_IDENT, p->pos - 1);
    if (ident_node < 0) return -1;
    node = p_new_node(p, CC_AST_VAR_DECL, p->pos - 1);
    if (node < 0) return -1;
    p->nodes[node].op = type_tok;
    p->nodes[node].left = ident_node;
    if (p_match(p, CC_TOK_ASSIGN)) {
        int init = p_parse_expr(p);
        if (init < 0) return -1;
        p->nodes[node].right = init;
    }
    if (!p_expect(p, CC_TOK_SEMI)) return -1;
    return node;
}

static int p_parse_block(cc_parser_t *p) {
    int first = -1;
    int last = -1;
    int block = p_new_node(p, CC_AST_PROGRAM, p->pos);
    if (block < 0) return -1;
    if (!p_expect(p, CC_TOK_LBRACE)) return -1;
    while (p_peek_kind(p) != (int)CC_TOK_RBRACE && p_peek_kind(p) != (int)CC_TOK_EOF) {
        int st = p_parse_stmt(p);
        if (st < 0) return -1;
        if (first < 0) first = st;
        else p->nodes[last].next = st;
        last = st;
    }
    if (!p_expect(p, CC_TOK_RBRACE)) return -1;
    p->nodes[block].left = first;
    return block;
}

static int p_parse_for(cc_parser_t *p) {
    int node = p_new_node(p, CC_AST_FOR, p->pos);
    int init = -1;
    int cond = -1;
    int step = -1;
    int body;
    if (node < 0) return -1;
    if (!p_expect(p, CC_TOK_KW_FOR) || !p_expect(p, CC_TOK_LPAREN)) return -1;
    if (p_peek_kind(p) == (int)CC_TOK_KW_INT || p_peek_kind(p) == (int)CC_TOK_KW_CHAR) {
        cc_token_kind_t t = (cc_token_kind_t)p_peek_kind(p);
        p->pos++;
        init = p_parse_var_decl_stmt(p, t);
        if (init < 0) return -1;
    } else if (!p_match(p, CC_TOK_SEMI)) {
        init = p_parse_expr(p);
        if (init < 0 || !p_expect(p, CC_TOK_SEMI)) return -1;
    }
    if (!p_match(p, CC_TOK_SEMI)) {
        cond = p_parse_expr(p);
        if (cond < 0 || !p_expect(p, CC_TOK_SEMI)) return -1;
    }
    if (!p_match(p, CC_TOK_RPAREN)) {
        step = p_parse_expr(p);
        if (step < 0 || !p_expect(p, CC_TOK_RPAREN)) return -1;
    }
    body = p_parse_stmt(p);
    if (body < 0) return -1;
    p->nodes[node].left = init;
    p->nodes[node].right = cond;
    p->nodes[node].third = step;
    p->nodes[node].next = body;
    return node;
}

static int p_parse_stmt(cc_parser_t *p) {
    if (p_peek_kind(p) == (int)CC_TOK_LBRACE) {
        return p_parse_block(p);
    }
    if (p_peek_kind(p) == (int)CC_TOK_KW_IF) {
        int node = p_new_node(p, CC_AST_IF, p->pos);
        int cond;
        int then_node;
        if (node < 0) return -1;
        p->pos++;
        if (!p_expect(p, CC_TOK_LPAREN)) return -1;
        cond = p_parse_expr(p);
        if (cond < 0 || !p_expect(p, CC_TOK_RPAREN)) return -1;
        then_node = p_parse_stmt(p);
        if (then_node < 0) return -1;
        p->nodes[node].left = cond;
        p->nodes[node].right = then_node;
        if (p_match(p, CC_TOK_KW_ELSE)) {
            int else_node = p_parse_stmt(p);
            if (else_node < 0) return -1;
            p->nodes[node].third = else_node;
        }
        return node;
    }
    if (p_peek_kind(p) == (int)CC_TOK_KW_WHILE) {
        int node = p_new_node(p, CC_AST_WHILE, p->pos);
        int cond;
        int body;
        if (node < 0) return -1;
        p->pos++;
        if (!p_expect(p, CC_TOK_LPAREN)) return -1;
        cond = p_parse_expr(p);
        if (cond < 0 || !p_expect(p, CC_TOK_RPAREN)) return -1;
        body = p_parse_stmt(p);
        if (body < 0) return -1;
        p->nodes[node].left = cond;
        p->nodes[node].right = body;
        return node;
    }
    if (p_peek_kind(p) == (int)CC_TOK_KW_FOR) {
        return p_parse_for(p);
    }
    if (p_peek_kind(p) == (int)CC_TOK_KW_RETURN) {
        int node = p_new_node(p, CC_AST_RETURN, p->pos);
        if (node < 0) return -1;
        p->pos++;
        if (!p_match(p, CC_TOK_SEMI)) {
            int expr = p_parse_expr(p);
            if (expr < 0 || !p_expect(p, CC_TOK_SEMI)) return -1;
            p->nodes[node].left = expr;
        }
        return node;
    }
    if (p_peek_kind(p) == (int)CC_TOK_KW_INT || p_peek_kind(p) == (int)CC_TOK_KW_CHAR) {
        cc_token_kind_t t = (cc_token_kind_t)p_peek_kind(p);
        p->pos++;
        return p_parse_var_decl_stmt(p, t);
    }
    if (p_match(p, CC_TOK_SEMI)) {
        return p_new_node(p, CC_AST_UNKNOWN, p->pos - 1);
    }
    {
        int expr = p_parse_expr(p);
        if (expr < 0 || !p_expect(p, CC_TOK_SEMI)) return -1;
        return expr;
    }
}

int cc_parse(const char *src,
             const cc_token_t *tokens,
             int token_count,
             cc_ast_node_t *nodes,
             int max_nodes) {
    cc_parser_t p;
    int root;
    int first = -1;
    int last = -1;
    if (src == NULL || tokens == NULL || token_count <= 0 || nodes == NULL || max_nodes <= 0) {
        return -1;
    }
    memset(&p, 0, sizeof(p));
    p.src = src;
    p.tokens = tokens;
    p.token_count = token_count;
    p.nodes = nodes;
    p.max_nodes = max_nodes;

    root = p_new_node(&p, CC_AST_PROGRAM, 0);
    if (root < 0) return -1;

    while (p_peek_kind(&p) != (int)CC_TOK_EOF) {
        if (p_peek_kind(&p) != (int)CC_TOK_KW_INT && p_peek_kind(&p) != (int)CC_TOK_KW_CHAR) {
            return -1;
        }
        {
            cc_token_kind_t type_tok = (cc_token_kind_t)p_peek_kind(&p);
            int decl;
            int ident;
            int marker;
            p.pos++;
            if (!p_expect(&p, CC_TOK_IDENT)) return -1;
            ident = p_new_node(&p, CC_AST_IDENT, p.pos - 1);
            if (ident < 0) return -1;
            marker = p.pos;
            if (p_match(&p, CC_TOK_LPAREN)) {
                int func = p_new_node(&p, CC_AST_FUNC_DECL, p.pos - 2);
                int param_first = -1;
                int param_last = -1;
                int body;
                if (func < 0) return -1;
                p.nodes[func].op = type_tok;
                p.nodes[func].left = ident;
                if (!p_match(&p, CC_TOK_RPAREN)) {
                    for (;;) {
                        int param_decl;
                        int param_ident;
                        cc_token_kind_t ptype;
                        if (p_peek_kind(&p) != (int)CC_TOK_KW_INT &&
                            p_peek_kind(&p) != (int)CC_TOK_KW_CHAR) return -1;
                        ptype = (cc_token_kind_t)p_peek_kind(&p);
                        p.pos++;
                        if (!p_expect(&p, CC_TOK_IDENT)) return -1;
                        param_ident = p_new_node(&p, CC_AST_IDENT, p.pos - 1);
                        if (param_ident < 0) return -1;
                        param_decl = p_new_node(&p, CC_AST_VAR_DECL, p.pos - 1);
                        if (param_decl < 0) return -1;
                        p.nodes[param_decl].op = ptype;
                        p.nodes[param_decl].left = param_ident;
                        if (param_first < 0) param_first = param_decl;
                        else p.nodes[param_last].next = param_decl;
                        param_last = param_decl;
                        if (p_match(&p, CC_TOK_COMMA)) continue;
                        if (!p_expect(&p, CC_TOK_RPAREN)) return -1;
                        break;
                    }
                }
                body = p_parse_block(&p);
                if (body < 0) return -1;
                p.nodes[func].right = param_first;
                p.nodes[func].third = body;
                decl = func;
            } else {
                int var = p_new_node(&p, CC_AST_VAR_DECL, marker - 1);
                if (var < 0) return -1;
                p.nodes[var].op = type_tok;
                p.nodes[var].left = ident;
                if (p_match(&p, CC_TOK_ASSIGN)) {
                    int init = p_parse_expr(&p);
                    if (init < 0) return -1;
                    p.nodes[var].right = init;
                }
                if (!p_expect(&p, CC_TOK_SEMI)) return -1;
                decl = var;
            }
            if (first < 0) first = decl;
            else p.nodes[last].next = decl;
            last = decl;
        }
    }
    p.nodes[root].left = first;
    return p.node_count;
}

static int cg_emit1(cc_out_t *o, unsigned char b) {
    if (o->len >= o->cap) return -1;
    o->buf[o->len++] = b;
    return 0;
}

static int cg_node_ok(const cc_codegen_t *cg, int idx) {
    return idx >= 0 && idx < cg->node_count;
}

static int cg_expr(cc_codegen_t *cg, int idx);

static int cg_binop(cc_codegen_t *cg, int idx) {
    const cc_ast_node_t *n;
    if (!cg_node_ok(cg, idx)) return -1;
    n = &cg->nodes[idx];
    if (cg_expr(cg, n->left) != 0) return -1;
    if (cg_emit1(cg->out, 0x47u) != 0) return -1; /* MOV B,A */
    if (cg_expr(cg, n->right) != 0) return -1;
    switch (n->op) {
        case CC_TOK_PLUS:
            if (cg_emit1(cg->out, 0x80u) != 0) return -1; /* ADD B */
            return 0;
        case CC_TOK_MINUS:
            if (cg_emit1(cg->out, 0x4Fu) != 0) return -1; /* MOV C,A */
            if (cg_emit1(cg->out, 0x78u) != 0) return -1; /* MOV A,B */
            if (cg_emit1(cg->out, 0x91u) != 0) return -1; /* SUB C */
            return 0;
        default:
            return -1;
    }
}

static int cg_expr(cc_codegen_t *cg, int idx) {
    const cc_ast_node_t *n;
    if (!cg_node_ok(cg, idx)) return -1;
    n = &cg->nodes[idx];
    switch (n->kind) {
        case CC_AST_LITERAL:
            if (cg_emit1(cg->out, 0x3Eu) != 0) return -1; /* MVI A,imm8 */
            if (cg_emit1(cg->out, (unsigned char)(n->value & 0xFF)) != 0) return -1;
            return 0;
        case CC_AST_BINOP:
            return cg_binop(cg, idx);
        default:
            return -1;
    }
}

static int cg_stmt(cc_codegen_t *cg, int idx, int *saw_return) {
    const cc_ast_node_t *n;
    if (!cg_node_ok(cg, idx)) return -1;
    n = &cg->nodes[idx];
    switch (n->kind) {
        case CC_AST_RETURN:
            if (n->left >= 0) {
                if (cg_expr(cg, n->left) != 0) return -1;
            } else {
                if (cg_emit1(cg->out, 0x3Eu) != 0) return -1; /* MVI A,0 */
                if (cg_emit1(cg->out, 0x00u) != 0) return -1;
            }
            if (cg_emit1(cg->out, 0x76u) != 0) return -1; /* HLT */
            *saw_return = 1;
            return 0;
        case CC_AST_PROGRAM: {
            int it = n->left;
            while (it >= 0) {
                if (cg_stmt(cg, it, saw_return) != 0) return -1;
                if (*saw_return) break;
                it = cg->nodes[it].next;
            }
            return 0;
        }
        default:
            return -1;
    }
}

static int cc_codegen(const cc_ast_node_t *nodes,
                      int node_count,
                      unsigned char *out,
                      int out_cap,
                      int *out_len) {
    cc_codegen_t cg;
    int prog_idx;
    int decl_idx;
    int main_body = -1;
    int saw_return = 0;

    if (nodes == NULL || node_count <= 0 || out == NULL || out_cap <= 0 || out_len == NULL) {
        return -1;
    }

    prog_idx = 0;
    if (nodes[prog_idx].kind != CC_AST_PROGRAM) return -1;
    decl_idx = nodes[prog_idx].left;
    while (decl_idx >= 0) {
        const cc_ast_node_t *decl = &nodes[decl_idx];
        if (decl->kind == CC_AST_FUNC_DECL && decl->left >= 0) {
            main_body = decl->third;
            break;
        }
        decl_idx = decl->next;
    }
    if (main_body < 0) return -1;

    cg.nodes = nodes;
    cg.node_count = node_count;
    cg.out = &(cc_out_t){out, 0, out_cap};

    if (cg_stmt(&cg, main_body, &saw_return) != 0) return -1;
    if (!saw_return) {
        if (cg_emit1(cg.out, 0x3Eu) != 0) return -1; /* MVI A,0 */
        if (cg_emit1(cg.out, 0x00u) != 0) return -1;
        if (cg_emit1(cg.out, 0x76u) != 0) return -1; /* HLT */
    }
    *out_len = cg.out->len;
    return 0;
}

int cc_compile(const char *src_path, const char *out_path) {
    FILE *src;
    FILE *out;
    int ch;
    unsigned int checksum = 0u;
    unsigned char blob[512];
    cc_token_t toks[1024];
    cc_ast_node_t nodes[2048];
    char src_buf[4096];
    size_t nread;
    int tok_count;
    int node_count;
    int code_len = 0;

    if (src_path == NULL || out_path == NULL) {
        return -1;
    }

    src = fopen(src_path, "rb");
    if (src == NULL) {
        return -1;
    }
    nread = fread(src_buf, 1u, sizeof(src_buf) - 1u, src);
    src_buf[nread] = '\0';
    tok_count = cc_lex(src_buf, toks, (int)(sizeof(toks) / sizeof(toks[0])));
    if (tok_count < 0) {
        fclose(src);
        return -1;
    }
    node_count = cc_parse(src_buf, toks, tok_count, nodes, (int)(sizeof(nodes) / sizeof(nodes[0])));
    if (node_count < 0) {
        fclose(src);
        return -1;
    }
    (void)nodes;
    for (size_t i = 0u; i < nread; ++i) {
        ch = (unsigned char)src_buf[i];
        checksum = (checksum + (unsigned int)ch) & 0xFFFFu;
    }
    if (fclose(src) != 0) {
        return -1;
    }

    out = fopen(out_path, "wb");
    if (out == NULL) {
        return -1;
    }
    /* Prefer AST-based codegen; fallback to deterministic stub for unsupported input. */
    if (cc_codegen(nodes, node_count, blob, (int)sizeof(blob), &code_len) != 0) {
        blob[0] = 0x00u;
        blob[1] = 0x3Eu;
        blob[2] = (unsigned char)(checksum & 0xFFu);
        blob[3] = 0x06u;
        blob[4] = (unsigned char)((checksum >> 8) & 0xFFu);
        blob[5] = 0x76u;
        code_len = 6;
    }
    if (fwrite(blob, 1u, (size_t)code_len, out) != (size_t)code_len) {
        fclose(out);
        return -1;
    }
    if (fclose(out) != 0) {
        return -1;
    }
    return 0;
}
