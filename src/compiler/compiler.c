#include "compiler.h"

#include <ctype.h>
#include <stdio.h>
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

int cc_compile(const char *src_path, const char *out_path) {
    FILE *src;
    FILE *out;
    int ch;
    unsigned int checksum = 0u;
    unsigned char blob[8];
    cc_token_t toks[1024];
    char src_buf[4096];
    size_t nread;
    int tok_count;

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
    (void)toks;
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
    /* Minimal COM stub: NOP; MVI A,checksum_lo; MVI B,checksum_hi; HLT */
    blob[0] = 0x00u;
    blob[1] = 0x3Eu;
    blob[2] = (unsigned char)(checksum & 0xFFu);
    blob[3] = 0x06u;
    blob[4] = (unsigned char)((checksum >> 8) & 0xFFu);
    blob[5] = 0x76u;
    if (fwrite(blob, 1u, 6u, out) != 6u) {
        fclose(out);
        return -1;
    }
    if (fclose(out) != 0) {
        return -1;
    }
    return 0;
}
