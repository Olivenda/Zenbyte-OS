/* ZBC -- Zenbite C: a tiny tree-walking interpreter for a useful subset
 * of C. This is NOT a real compiler -- it parses your source and walks
 * the AST directly. The point is to let users write and run real C-looking
 * programs inside Zenbite, in the spirit of Turbo C 1.0's REPL-ish feel.
 *
 * Supported:
 *   int variables (global and local), int literals (decimal, hex),
 *   arithmetic: + - * / % ()
 *   comparison: == != < > <= >=
 *   logical:    && || !
 *   bitwise:    & | ^ << >>
 *   assignment: = += -= *= /=
 *   control:    if/else, while, for, return, break, continue
 *   functions:  int name(int a, int b) { ... }   void name(...) { ... }
 *   builtins:   printf(fmt, ...)  -> %d %i %u %x %c %s %% (printf-style)
 *               print(int)        -> prints a decimal number + newline
 *               puts(string)      -> prints a string literal + newline
 *               putchar(int)      -> prints one character
 *               getchar()         -> reads one keypress (blocking)
 *   includes:   #include <stdio.h> and other # lines are accepted and
 *               ignored -- the runtime above is always available.
 *
 * Not supported (yet): pointers, arrays, structs, floats, strings as
 * variables. Use `puts("hi")` for output; strings are literals only.
 *
 * Hard limits: 64 functions, 64 variables per scope, 4096 AST nodes per
 * program, 256 KiB source. Plenty for educational programs.
 */

#include "kernel.h"
#include "kio.h"
#include "string.h"
#include "fs.h"

#define MAX_SRC      (8 * 1024)
#define MAX_TOKENS   512
#define MAX_NODES    512
#define MAX_FUNCS    16
#define MAX_VARS     32
#define MAX_STRTAB   1024
#define MAX_CALL     8

enum tok {
    T_EOF = 0, T_INT, T_IDENT, T_STR,
    T_LPAR, T_RPAR, T_LBRACE, T_RBRACE, T_SEMI, T_COMMA,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PCT,
    T_ASSIGN, T_PLUSEQ, T_MINUSEQ, T_STAREQ, T_SLASHEQ, T_MODEQ,
    T_EQ, T_NEQ, T_LT, T_GT, T_LE, T_GE,
    T_AND, T_OR, T_NOT, T_BAND, T_BOR, T_BXOR, T_SHL, T_SHR,
    T_KW_INT, T_KW_VOID, T_KW_IF, T_KW_ELSE, T_KW_WHILE,
    T_KW_RETURN, T_KW_BREAK, T_KW_CONT, T_KW_FOR,
    T_KW_DO, T_KW_STRUCT, T_KW_CHAR, T_KW_SWITCH, T_KW_CASE,
    T_KW_DEFAULT, T_KW_ELIF, T_KW_SIZEOF,
    T_LSQUARE, T_RSQUARE,
    T_DOT, T_INCR, T_DECR, T_ARROW,
    T_QMARK, T_COLON,       /* ?: ternary */
    T_TILDE                  /* ~ bitwise NOT */
};

struct token {
    enum tok kind;
    int      ival;
    char     sval[32];
    int      str_off;          /* offset into string table */
};

enum nkind {
    N_NUM, N_VAR, N_STR, N_ASSIGN, N_BIN, N_UN, N_CALL,
    N_IF, N_WHILE, N_FOR, N_BLOCK, N_RETURN, N_BREAK, N_CONT, N_EXPR_STMT,
    N_FUNC,
    N_DOWHILE,      /* do { body } while (cond) */
    N_SUBSCRIPT,    /* arr[i] */
    N_MEMBER,       /* struct.field  (child[0]=base, name=field, ival=0 for dot / 1 for ->) */
    N_INCR,         /* ++var or var++ (child[0]=var, ival=0 pre / 1 post) */
    N_DEREF,        /* *ptr (child[0]=expr) */
    N_ADDR,         /* &var (child[0]=N_VAR) */
    N_SWITCH,       /* switch(expr) { cases... } */
    N_CASE,         /* case const: stmts  (ival=case value, child[0]=body) */
    N_DEFAULT,      /* default: stmts (child[0]=body) */
    N_TERNARY,      /* a ? b : c (child[0]=cond, child[1]=then, child[2]=else) */
    N_COMMA,        /* a, b (child[0]=left, child[1]=right) */
    N_SIZEOF,       /* sizeof(type) -- returns 4 always (int) */
};

struct node {
    enum nkind kind;
    int  op;              /* tok kind for BIN/UN/ASSIGN */
    int  ival;
    int  str_off;
    char name[32];
    int  child[4];        /* indices into node table; -1 = none */
    int  next;            /* used for statement lists / args, -1 terminator */
};

struct func {
    char name[32];
    int  body;            /* node idx of block */
    int  param_count;
    char params[8][32];
    int  is_void;
    int  is_builtin;
    int  (*native)(int *args, int n, int *out);
};

struct var {
    char name[32];
    char struct_type[32];  /* non-empty for struct variables */
    int  value;
};

/* ---- globals (kept off the kernel stack) -------------------- */
#define MAX_STRUCT_TYPES 8
#define MAX_STRUCT_FIELDS 8
struct struct_def {
    char name[32];
    char fields[MAX_STRUCT_FIELDS][32];
    int  count;
};
static struct struct_def struct_defs[MAX_STRUCT_TYPES];
static int struct_def_count;

/* Pool for array and struct storage. Index 0 = null/uninitialized. */
#define POOL_SIZE 2048
static int pool[POOL_SIZE];
static int pool_pos;  /* next free index, starts at 1 */

static char           src[MAX_SRC];
static struct token   toks[MAX_TOKENS];
static int            tok_count, tok_pos;
static char           strtab[MAX_STRTAB];
static int            strtab_pos;
static struct node    nodes[MAX_NODES];
static int            node_count;
static struct func    funcs[MAX_FUNCS];
static int            func_count;
static struct var     globals[MAX_VARS];
static int            global_count;
static struct var     scopes[MAX_CALL][MAX_VARS];
static int            scope_var_count[MAX_CALL];
static int            scope_depth;
static int            had_error;
static int            had_return;
static int            return_value;
static int            had_break, had_continue;

static void zbc_err(const char *msg) {
    if (!had_error) kprintf("ZBC error: %s\n", msg);
    had_error = 1;
}

/* ---- string table -------------------------------------------- */
static int strtab_add(const char *s, int len) {
    if (strtab_pos + len + 1 > MAX_STRTAB) { zbc_err("string table full"); return 0; }
    int off = strtab_pos;
    memcpy(&strtab[off], s, (size_t)len);
    strtab[off + len] = '\0';
    strtab_pos += len + 1;
    return off;
}

/* ---- tokeniser ----------------------------------------------- */
struct kw { const char *n; enum tok t; };
static const struct kw keywords[] = {
    { "int",      T_KW_INT    },
    { "void",     T_KW_VOID   },
    { "if",       T_KW_IF     },
    { "else",     T_KW_ELSE   },
    { "while",    T_KW_WHILE  },
    { "return",   T_KW_RETURN },
    { "break",    T_KW_BREAK  },
    { "continue", T_KW_CONT   },
    { "for",      T_KW_FOR    },
    { "do",       T_KW_DO     },
    { "struct",   T_KW_STRUCT },
    { "char",     T_KW_CHAR   },
    { "switch",   T_KW_SWITCH },
    { "case",     T_KW_CASE   },
    { "default",  T_KW_DEFAULT},
    { "elif",     T_KW_ELIF   },
    { "sizeof",   T_KW_SIZEOF },
};

static int tokenize(const char *s) {
    int p = 0;
    tok_count = 0;
    while (s[p] && tok_count < MAX_TOKENS - 1) {
        char c = s[p];
        if (isspace((u8)c)) { p++; continue; }
        if (c == '/' && s[p+1] == '/') { while (s[p] && s[p] != '\n') p++; continue; }
        if (c == '/' && s[p+1] == '*') {
            p += 2;
            while (s[p] && !(s[p] == '*' && s[p+1] == '/')) p++;
            if (s[p]) p += 2;
            continue;
        }
        /* Preprocessor lines (#include <stdio.h>, #define, #pragma, ...)
         * are accepted and skipped. The C runtime Zenbite provides --
         * printf/puts/putchar/getchar/print -- is always available, so
         * <stdio.h> etc. are implicit. This lets users paste standard-
         * looking C without the parser choking on the directives. */
        if (c == '#') { while (s[p] && s[p] != '\n') p++; continue; }
        struct token *t = &toks[tok_count];
        memset(t, 0, sizeof *t);
        if (isdigit((u8)c)) {
            int v = 0;
            if (c == '0' && (s[p+1] == 'x' || s[p+1] == 'X')) {
                p += 2;
                while (isdigit((u8)s[p]) ||
                       (s[p] >= 'a' && s[p] <= 'f') ||
                       (s[p] >= 'A' && s[p] <= 'F')) {
                    char ch = s[p++];
                    int d = isdigit((u8)ch) ? ch - '0' :
                            (ch >= 'a' ? ch - 'a' + 10 : ch - 'A' + 10);
                    v = v * 16 + d;
                }
            } else {
                while (isdigit((u8)s[p])) { v = v * 10 + (s[p++] - '0'); }
            }
            t->kind = T_INT; t->ival = v;
            tok_count++; continue;
        }
        if (isalpha((u8)c) || c == '_') {
            int n = 0;
            while ((isalpha((u8)s[p]) || isdigit((u8)s[p]) || s[p] == '_') && n < 31)
                t->sval[n++] = s[p++];
            t->sval[n] = '\0';
            t->kind = T_IDENT;
            for (size_t k = 0; k < ARRAY_LEN(keywords); k++)
                if (strcmp(t->sval, keywords[k].n) == 0) { t->kind = keywords[k].t; break; }
            tok_count++; continue;
        }
        if (c == '"') {
            p++;
            char tmp[256]; int n = 0;
            while (s[p] && s[p] != '"' && n < 255) {
                if (s[p] == '\\' && s[p+1]) {
                    char e = s[++p];
                    p++;
                    switch (e) {
                    case 'n': tmp[n++] = '\n'; break;
                    case 't': tmp[n++] = '\t'; break;
                    case 'r': tmp[n++] = '\r'; break;
                    case '\\': tmp[n++] = '\\'; break;
                    case '"': tmp[n++] = '"'; break;
                    case '0': tmp[n++] = '\0'; break;
                    default: tmp[n++] = e;
                    }
                } else {
                    tmp[n++] = s[p++];
                }
            }
            if (s[p] == '"') p++;
            t->kind = T_STR;
            t->str_off = strtab_add(tmp, n);
            tok_count++; continue;
        }
        if (c == '\'') {
            /* Character literal: 'a' or '\n'. Becomes an INT token
             * with the byte value, just like in C. */
            p++;
            int v = 0;
            if (s[p] == '\\' && s[p+1]) {
                char e = s[p+1]; p += 2;
                switch (e) {
                case 'n':  v = '\n'; break;
                case 't':  v = '\t'; break;
                case 'r':  v = '\r'; break;
                case '0':  v = 0;    break;
                case '\\': v = '\\'; break;
                case '\'': v = '\''; break;
                case '"':  v = '"';  break;
                default:   v = e;
                }
            } else if (s[p]) {
                v = (unsigned char)s[p++];
            }
            if (s[p] == '\'') p++;
            t->kind = T_INT;
            t->ival = v;
            tok_count++; continue;
        }
        switch (c) {
        case '(': t->kind = T_LPAR;  p++; break;
        case ')': t->kind = T_RPAR;  p++; break;
        case '{': t->kind = T_LBRACE;p++; break;
        case '}': t->kind = T_RBRACE;p++; break;
        case ';': t->kind = T_SEMI;  p++; break;
        case ',': t->kind = T_COMMA; p++; break;
        case '+': p++; if (s[p]=='=') { t->kind=T_PLUSEQ; p++; } else if (s[p]=='+') { t->kind=T_INCR; p++; } else t->kind=T_PLUS;  break;
        case '-': p++; if (s[p]=='=') { t->kind=T_MINUSEQ;p++; } else if (s[p]=='-') { t->kind=T_DECR; p++; } else if (s[p]=='>') { t->kind=T_ARROW; p++; } else t->kind=T_MINUS; break;
        case '*': p++; if (s[p]=='=') { t->kind=T_STAREQ; p++; }  else t->kind=T_STAR;  break;
        case '/': p++; if (s[p]=='=') { t->kind=T_SLASHEQ;p++; }  else t->kind=T_SLASH; break;
        case '%': p++; if (s[p]=='=') { t->kind=T_MODEQ; p++; } else t->kind=T_PCT; break;
        case '=': p++; if (s[p]=='=') { t->kind=T_EQ;     p++; }  else t->kind=T_ASSIGN;break;
        case '!': p++; if (s[p]=='=') { t->kind=T_NEQ;    p++; }  else t->kind=T_NOT;   break;
        case '<':
            p++;
            if      (s[p]=='=') { t->kind=T_LE;  p++; }
            else if (s[p]=='<') { t->kind=T_SHL; p++; }
            else                  t->kind=T_LT;
            break;
        case '>':
            p++;
            if      (s[p]=='=') { t->kind=T_GE;  p++; }
            else if (s[p]=='>') { t->kind=T_SHR; p++; }
            else                  t->kind=T_GT;
            break;
        case '&': p++; if (s[p]=='&') { t->kind=T_AND;    p++; }  else t->kind=T_BAND;  break;
        case '|': p++; if (s[p]=='|') { t->kind=T_OR;     p++; }  else t->kind=T_BOR;   break;
        case '^': t->kind=T_BXOR; p++; break;
        case '[': t->kind=T_LSQUARE; p++; break;
        case ']': t->kind=T_RSQUARE; p++; break;
        case '.': t->kind=T_DOT;     p++; break;
        case '?': t->kind=T_QMARK;   p++; break;
        case ':': t->kind=T_COLON;   p++; break;
        case '~': t->kind=T_TILDE;   p++; break;
        default:
            kprintf("ZBC: unexpected character '%c'\n", c);
            return -1;
        }
        tok_count++;
    }
    toks[tok_count].kind = T_EOF;
    return 0;
}

/* ---- parser -------------------------------------------------- */
static int parse_block(void);
static int parse_expr (void);
static int parse_stmt (void);

static struct token *peek(void) { return &toks[tok_pos]; }
static struct token *advance(void) { return &toks[tok_pos++]; }
static int accept(enum tok t) {
    if (peek()->kind == t) { advance(); return 1; }
    return 0;
}
static int expect(enum tok t, const char *what) {
    if (!accept(t)) { zbc_err(what); return 0; }
    return 1;
}

static int new_node(enum nkind k) {
    if (node_count >= MAX_NODES) { zbc_err("too many AST nodes"); return 0; }
    int i = node_count++;
    struct node *n = &nodes[i];
    memset(n, 0, sizeof *n);
    n->kind = k;
    for (int j = 0; j < 4; j++) n->child[j] = -1;
    n->next = -1;
    return i;
}

static int parse_primary(void) {
    struct token *t = peek();
    if (t->kind == T_INT) {
        int n = new_node(N_NUM);
        nodes[n].ival = advance()->ival;
        return n;
    }
    if (t->kind == T_STR) {
        int n = new_node(N_STR);
        nodes[n].str_off = advance()->str_off;
        return n;
    }
    if (t->kind == T_IDENT) {
        struct token *id = advance();
        if (peek()->kind == T_LPAR) {
            advance();
            int n = new_node(N_CALL);
            strncpy(nodes[n].name, id->sval, 31);
            int prev = -1;
            int first = -1;
            int argi = 0;
            while (peek()->kind != T_RPAR && !had_error) {
                int a = parse_expr();
                if (first < 0) { nodes[n].child[0] = a; first = a; }
                else nodes[prev].next = a;
                prev = a;
                argi++;
                if (!accept(T_COMMA)) break;
            }
            expect(T_RPAR, "expected )");
            nodes[n].ival = argi;
            return n;
        }
        int n = new_node(N_VAR);
        strncpy(nodes[n].name, id->sval, 31);
        /* subscript: a[i] -- may chain */
        while (peek()->kind == T_LSQUARE) {
            advance();
            int idx = parse_expr();
            expect(T_RSQUARE, "]");
            int sub = new_node(N_SUBSCRIPT);
            nodes[sub].child[0] = n;
            nodes[sub].child[1] = idx;
            n = sub;
        }
        /* member access: a.field or a->field */
        while (peek()->kind == T_DOT || peek()->kind == T_ARROW) {
            int is_arrow = (advance()->kind == T_ARROW);
            struct token *fname = peek();
            if (fname->kind != T_IDENT) { zbc_err("field name"); break; }
            advance();
            int mem = new_node(N_MEMBER);
            nodes[mem].child[0] = n;
            strncpy(nodes[mem].name, fname->sval, 31);
            nodes[mem].ival = is_arrow;
            n = mem;
        }
        /* postfix ++ / -- */
        if (peek()->kind == T_INCR || peek()->kind == T_DECR) {
            int op = advance()->kind;
            int inc = new_node(N_INCR);
            nodes[inc].op = op;
            nodes[inc].child[0] = n;
            nodes[inc].ival = 1; /* postfix */
            n = inc;
        }
        return n;
    }
    if (accept(T_LPAR)) {
        int n = parse_expr();
        expect(T_RPAR, "expected )");
        return n;
    }
    if (t->kind == T_MINUS || t->kind == T_NOT || t->kind == T_TILDE) {
        int op = advance()->kind;
        int n = new_node(N_UN);
        nodes[n].op = op;
        nodes[n].child[0] = parse_primary();
        return n;
    }
    /* unary * (dereference) */
    if (t->kind == T_STAR) {
        advance();
        int n = new_node(N_DEREF);
        nodes[n].child[0] = parse_primary();
        return n;
    }
    /* unary & (address-of) */
    if (t->kind == T_BAND) {
        advance();
        int n = new_node(N_ADDR);
        nodes[n].child[0] = parse_primary();
        return n;
    }
    /* prefix ++ / -- */
    if (t->kind == T_INCR || t->kind == T_DECR) {
        int op = advance()->kind;
        int n = new_node(N_INCR);
        nodes[n].op = op;
        nodes[n].child[0] = parse_primary();
        nodes[n].ival = 0; /* prefix */
        return n;
    }
    zbc_err("expected expression");
    return new_node(N_NUM);
}

static int bin_prec(enum tok k) {
    switch (k) {
    case T_OR:                                            return 1;
    case T_AND:                                           return 2;
    case T_BOR:                                           return 3;
    case T_BXOR:                                          return 4;
    case T_BAND:                                          return 5;
    case T_EQ: case T_NEQ:                                return 6;
    case T_LT: case T_GT: case T_LE: case T_GE:           return 7;
    case T_SHL: case T_SHR:                               return 8;
    case T_PLUS: case T_MINUS:                            return 9;
    case T_STAR: case T_SLASH: case T_PCT:                return 10;
    default: return 0;
    }
}

static int parse_binop(int prec) {
    int lhs = parse_primary();
    while (bin_prec(peek()->kind) >= prec) {
        int op = advance()->kind;
        int rhs = parse_binop(bin_prec(op) + 1);
        int n = new_node(N_BIN);
        nodes[n].op = op;
        nodes[n].child[0] = lhs;
        nodes[n].child[1] = rhs;
        lhs = n;
    }
    return lhs;
}

static int parse_expr(void) {
    int lhs = parse_binop(1);
    enum tok k = peek()->kind;
    /* Ternary: a ? b : c */
    if (k == T_QMARK) {
        advance();
        int then_e = parse_expr();
        expect(T_COLON, ":");
        int else_e = parse_expr();
        int n = new_node(N_TERNARY);
        nodes[n].child[0] = lhs;
        nodes[n].child[1] = then_e;
        nodes[n].child[2] = else_e;
        return n;
    }
    /* Assignment operators */
    if (k == T_ASSIGN || k == T_PLUSEQ || k == T_MINUSEQ ||
        k == T_STAREQ || k == T_SLASHEQ || k == T_MODEQ) {
        if (nodes[lhs].kind != N_VAR && nodes[lhs].kind != N_SUBSCRIPT &&
            nodes[lhs].kind != N_MEMBER && nodes[lhs].kind != N_DEREF) {
            zbc_err("assign to non-lvalue"); return lhs;
        }
        advance();
        int rhs = parse_expr();
        int n = new_node(N_ASSIGN);
        nodes[n].op = k;
        nodes[n].child[0] = lhs;
        nodes[n].child[1] = rhs;
        return n;
    }
    /* Comma operator: a, b */
    if (k == T_COMMA) {
        advance();
        int rhs = parse_expr();
        int n = new_node(N_COMMA);
        nodes[n].child[0] = lhs;
        nodes[n].child[1] = rhs;
        return n;
    }
    return lhs;
}

static int parse_stmt(void) {
    if (accept(T_LBRACE)) return parse_block();

    if (accept(T_KW_IF)) {
        expect(T_LPAR, "(");
        int cond = parse_expr();
        expect(T_RPAR, ")");
        int then_b = parse_stmt();
        int else_b = -1;
        if (accept(T_KW_ELSE)) else_b = parse_stmt();
        int n = new_node(N_IF);
        nodes[n].child[0] = cond;
        nodes[n].child[1] = then_b;
        nodes[n].child[2] = else_b;
        return n;
    }
    if (accept(T_KW_DO)) {
        int body = parse_stmt();
        if (!accept(T_KW_WHILE)) { zbc_err("expected while"); return new_node(N_NUM); }
        expect(T_LPAR, "(");
        int cond = parse_expr();
        expect(T_RPAR, ")");
        expect(T_SEMI, ";");
        int nd = new_node(N_DOWHILE);
        nodes[nd].child[0] = body;
        nodes[nd].child[1] = cond;
        return nd;
    }
    if (accept(T_KW_WHILE)) {
        expect(T_LPAR, "(");
        int cond = parse_expr();
        expect(T_RPAR, ")");
        int body = parse_stmt();
        int n = new_node(N_WHILE);
        nodes[n].child[0] = cond;
        nodes[n].child[1] = body;
        return n;
    }
    if (accept(T_KW_FOR)) {
        /* for (init; cond; post) body
         * init may be `int i = e` or an expression or empty;
         * cond and post are expressions or empty. */
        expect(T_LPAR, "(");
        int init = -1;
        if (accept(T_KW_INT) || accept(T_KW_CHAR)) {
            struct token *id = advance();
            if (id->kind != T_IDENT) { zbc_err("expected name"); return new_node(N_NUM); }
            int rhs = -1;
            if (accept(T_ASSIGN)) rhs = parse_expr();
            int a = new_node(N_ASSIGN);
            nodes[a].op = T_ASSIGN;
            int v = new_node(N_VAR);
            strncpy(nodes[v].name, id->sval, 31);
            nodes[v].ival = 1;            /* declaration */
            if (rhs < 0) { rhs = new_node(N_NUM); nodes[rhs].ival = 0; }
            nodes[a].child[0] = v;
            nodes[a].child[1] = rhs;
            init = a;
        } else if (peek()->kind != T_SEMI) {
            init = parse_expr();
        }
        expect(T_SEMI, ";");
        int cond = (peek()->kind != T_SEMI) ? parse_expr() : -1;
        expect(T_SEMI, ";");
        int post = (peek()->kind != T_RPAR) ? parse_expr() : -1;
        expect(T_RPAR, ")");
        int body = parse_stmt();
        int n = new_node(N_FOR);
        nodes[n].child[0] = init;
        nodes[n].child[1] = cond;
        nodes[n].child[2] = post;
        nodes[n].child[3] = body;
        return n;
    }
    if (accept(T_KW_RETURN)) {
        int n = new_node(N_RETURN);
        if (!accept(T_SEMI)) {
            nodes[n].child[0] = parse_expr();
            expect(T_SEMI, ";");
        }
        return n;
    }
    if (accept(T_KW_BREAK)) { expect(T_SEMI, ";"); return new_node(N_BREAK); }
    if (accept(T_KW_CONT))  { expect(T_SEMI, ";"); return new_node(N_CONT);  }

    /* switch(expr) { case N: stmts... default: stmts... } */
    if (accept(T_KW_SWITCH)) {
        expect(T_LPAR, "(");
        int expr = parse_expr();
        expect(T_RPAR, ")");
        expect(T_LBRACE, "{");
        int n = new_node(N_SWITCH);
        nodes[n].child[0] = expr;
        /* Parse case/default labels and their statements */
        int prev_case = -1;
        int first_case = -1;
        while (peek()->kind != T_RBRACE && !had_error) {
            if (accept(T_KW_CASE)) {
                int val = parse_expr();
                expect(T_COLON, ":");
                int cn = new_node(N_CASE);
                nodes[cn].ival = val;
                /* Collect statements until next case/default/} */
                int stmts = -1, stmt_tail = -1;
                while (peek()->kind != T_KW_CASE && peek()->kind != T_KW_DEFAULT &&
                       peek()->kind != T_RBRACE && !had_error) {
                    int s = parse_stmt();
                    if (stmts < 0) { stmts = s; stmt_tail = s; }
                    else { nodes[stmt_tail].next = s; stmt_tail = s; }
                }
                nodes[cn].child[0] = stmts;
                if (prev_case >= 0) nodes[prev_case].next = cn;
                else first_case = cn;
                prev_case = cn;
            } else if (accept(T_KW_DEFAULT)) {
                expect(T_COLON, ":");
                int dn = new_node(N_DEFAULT);
                int stmts = -1, stmt_tail = -1;
                while (peek()->kind != T_RBRACE && !had_error) {
                    int s = parse_stmt();
                    if (stmts < 0) { stmts = s; stmt_tail = s; }
                    else { nodes[stmt_tail].next = s; stmt_tail = s; }
                }
                nodes[dn].child[0] = stmts;
                if (prev_case >= 0) nodes[prev_case].next = dn;
                else first_case = dn;
                prev_case = dn;
            } else {
                zbc_err("expected case or default in switch");
                break;
            }
        }
        expect(T_RBRACE, "}");
        nodes[n].child[1] = first_case;  /* linked list of cases */
        return n;
    }

    if (peek()->kind == T_KW_STRUCT) {
        advance();
        struct token *sname = peek();
        if (sname->kind != T_IDENT) { zbc_err("struct name expected"); return new_node(N_NUM); }
        advance();
        if (peek()->kind == T_LBRACE) {
            /* struct definition: struct Foo { int x; int y; }; */
            advance();
            struct struct_def *sd = NULL;
            for (int i = 0; i < struct_def_count; i++)
                if (strcmp(struct_defs[i].name, sname->sval) == 0) { sd = &struct_defs[i]; break; }
            if (!sd) {
                if (struct_def_count >= MAX_STRUCT_TYPES) { zbc_err("too many struct types"); return new_node(N_NUM); }
                sd = &struct_defs[struct_def_count++];
                memset(sd, 0, sizeof *sd);
                strncpy(sd->name, sname->sval, 31);
            }
            sd->count = 0;
            while (peek()->kind != T_RBRACE && peek()->kind != T_EOF && !had_error) {
                /* consume type keyword (int/char/struct ...) */
                if (peek()->kind == T_KW_INT || peek()->kind == T_KW_CHAR) advance();
                struct token *fname = peek();
                if (fname->kind != T_IDENT) { zbc_err("field name expected"); break; }
                advance();
                if (sd->count < MAX_STRUCT_FIELDS)
                    strncpy(sd->fields[sd->count++], fname->sval, 31);
                accept(T_SEMI);
            }
            expect(T_RBRACE, "}");
            expect(T_SEMI, ";");
            int nd = new_node(N_NUM); nodes[nd].ival = 0; return nd;
        } else {
            /* struct variable: struct Foo varname; */
            struct struct_def *sd = NULL;
            for (int i = 0; i < struct_def_count; i++)
                if (strcmp(struct_defs[i].name, sname->sval) == 0) { sd = &struct_defs[i]; break; }
            if (!sd) { zbc_err("unknown struct type"); return new_node(N_NUM); }
            int blk = new_node(N_BLOCK);
            int first = -1, last = -1, cnt = 0;
            for (;;) {
                struct token *id = peek();
                if (id->kind != T_IDENT) { zbc_err("variable name expected"); return new_node(N_NUM); }
                advance();
                /* Allocate pool space for the struct fields */
                int base = pool_pos;
                pool_pos += sd->count;
                if (pool_pos >= POOL_SIZE) { zbc_err("pool overflow"); pool_pos = 1; }
                /* Emit: int <varname> = <base> (pool index) */
                int an = new_node(N_ASSIGN);
                nodes[an].op = T_ASSIGN;
                /* store 1-based struct def index so eval can set struct_type */
                nodes[an].ival = (int)(sd - struct_defs) + 1;
                int v = new_node(N_VAR);
                strncpy(nodes[v].name, id->sval, 31);
                nodes[v].ival = 1;  /* declaration */
                int num = new_node(N_NUM); nodes[num].ival = base;
                nodes[an].child[0] = v;
                nodes[an].child[1] = num;
                if (first < 0) first = an;
                if (last >= 0) nodes[last].next = an;
                last = an; cnt++;
                if (!accept(T_COMMA)) break;
            }
            expect(T_SEMI, ";");
            if (cnt == 1) return first;
            nodes[blk].child[0] = first;
            return blk;
        }
    }
    if (accept(T_KW_INT) || accept(T_KW_CHAR)) {
        /* Support comma-separated declarations: int a, b = 3, c;
         * char is treated as int (pool-int semantics).
         * Each becomes its own assignment node; we wrap them in a
         * block when there's more than one so a single statement slot
         * can hold them all. */
        int blk = new_node(N_BLOCK);
        int first = -1, last = -1, count = 0;
        for (;;) {
            struct token *id = advance();
            if (id->kind != T_IDENT) { zbc_err("expected name"); return new_node(N_NUM); }
            /* Array declaration: int arr[N] */
            if (peek()->kind == T_LSQUARE) {
                advance();
                int sz_node = parse_expr();
                expect(T_RSQUARE, "]");
                int sz = nodes[sz_node].kind == N_NUM ? nodes[sz_node].ival : 8;
                if (sz < 1) sz = 1;
                int base = pool_pos;
                pool_pos += sz;
                if (pool_pos >= POOL_SIZE) { zbc_err("pool overflow"); pool_pos = 1; }
                int an = new_node(N_ASSIGN);
                nodes[an].op = T_ASSIGN;
                int v = new_node(N_VAR);
                strncpy(nodes[v].name, id->sval, 31);
                nodes[v].ival = 1;  /* declaration */
                int num = new_node(N_NUM); nodes[num].ival = base;
                nodes[an].child[0] = v;
                nodes[an].child[1] = num;
                /* For array decls inside comma lists we break after the
                 * first one (no comma chains after array decl) */
                expect(T_SEMI, ";");
                if (first < 0) return an;
                nodes[last].next = an;
                nodes[blk].child[0] = first;
                return blk;
            }
            int rhs = -1;
            if (accept(T_ASSIGN)) rhs = parse_expr();
            int n = new_node(N_ASSIGN);
            nodes[n].op = T_ASSIGN;
            int v = new_node(N_VAR);
            strncpy(nodes[v].name, id->sval, 31);
            nodes[v].ival = 1;            /* mark as declaration */
            if (rhs < 0) { rhs = new_node(N_NUM); nodes[rhs].ival = 0; }
            nodes[n].child[0] = v;
            nodes[n].child[1] = rhs;
            if (first < 0) first = n;
            if (last >= 0) nodes[last].next = n;
            last = n;
            count++;
            if (!accept(T_COMMA)) break;
        }
        expect(T_SEMI, ";");
        if (count == 1) return first;
        nodes[blk].child[0] = first;
        return blk;
    }

    int e = parse_expr();
    expect(T_SEMI, ";");
    int n = new_node(N_EXPR_STMT);
    nodes[n].child[0] = e;
    return n;
}

static int parse_block(void) {
    int b = new_node(N_BLOCK);
    int last = -1;
    while (peek()->kind != T_RBRACE && peek()->kind != T_EOF) {
        int s = parse_stmt();
        if (last < 0) nodes[b].child[0] = s;
        else nodes[last].next = s;
        last = s;
        if (had_error) return b;
    }
    expect(T_RBRACE, "}");
    return b;
}

static int parse_func(int is_void) {
    if (func_count >= MAX_FUNCS) { zbc_err("too many functions"); return -1; }
    struct func *f = &funcs[func_count++];
    memset(f, 0, sizeof *f);
    f->is_void = is_void;
    struct token *id = advance();
    if (id->kind != T_IDENT) { zbc_err("function name"); return -1; }
    strncpy(f->name, id->sval, 31);
    expect(T_LPAR, "(");
    while (peek()->kind != T_RPAR && !had_error) {
        accept(T_KW_INT) || accept(T_KW_CHAR) || accept(T_KW_VOID);
        struct token *pid = advance();
        if (pid->kind != T_IDENT) { zbc_err("param name"); return -1; }
        if (f->param_count < 8) strncpy(f->params[f->param_count++], pid->sval, 31);
        if (!accept(T_COMMA)) break;
    }
    expect(T_RPAR, ")");
    expect(T_LBRACE, "{");
    f->body = parse_block();
    return 0;
}

/* ---- scope helpers ------------------------------------------- */
static struct var *find_var(const char *name) {
    if (scope_depth > 0) {
        for (int i = scope_var_count[scope_depth - 1] - 1; i >= 0; i--)
            if (strcmp(scopes[scope_depth - 1][i].name, name) == 0)
                return &scopes[scope_depth - 1][i];
    }
    for (int i = global_count - 1; i >= 0; i--)
        if (strcmp(globals[i].name, name) == 0) return &globals[i];
    return NULL;
}

static void set_local(const char *name, int v) {
    int d = scope_depth > 0 ? scope_depth - 1 : 0;
    int idx = scope_var_count[d];
    if (idx >= MAX_VARS) { zbc_err("too many locals"); return; }
    struct var *nv = &scopes[d][idx];
    strncpy(nv->name, name, 31); nv->name[31] = '\0';
    nv->struct_type[0] = '\0';
    nv->value = v;
    scope_var_count[d] = idx + 1;
}

/* ---- builtin functions --------------------------------------- */
static int builtin_print(int *a, int n, int *out)   { if (n) kprintf("%d\n", a[0]); *out = 0; return 0; }
static int builtin_putchar(int *a, int n, int *out) { if (n) kputc((char)a[0]); *out = 0; return 0; }
static int builtin_getchar(int *a, int n, int *out) { (void)a; (void)n; *out = kb_getc(); return 0; }

/* ---- extended ZBX runtime -----------------------------------------
 * These are the "mini-libc" exposed to packaged .ZBX programs. zbc
 * only has ints + string literals, so every API here is expressed
 * with int arguments (paths come in as string literals, special-
 * cased in eval like puts/printf). They cover the four capability
 * buckets the format promises: console, file, full-screen TUI, time.
 */
#include "vga.h"
extern u32  pit_ticks(void);
extern int  kb_trygetc(void);

/* File-handle table local to the running program. We don't reuse the
 * fs.c handle ints directly so a misbehaving program can't close a
 * kernel handle; we map small program-visible ids 0..7 to fs handles. */
#define ZBX_MAXFH 8
static int zbx_fh[ZBX_MAXFH];
static int zbx_fh_used[ZBX_MAXFH];

static void zbx_runtime_reset(void) {
    for (int i = 0; i < ZBX_MAXFH; i++) { zbx_fh[i] = -1; zbx_fh_used[i] = 0; }
}
static void zbx_runtime_cleanup(void) {
    for (int i = 0; i < ZBX_MAXFH; i++)
        if (zbx_fh_used[i]) { fs_close(zbx_fh[i]); zbx_fh_used[i] = 0; }
}
static int zbx_fh_alloc(int real) {
    for (int i = 0; i < ZBX_MAXFH; i++)
        if (!zbx_fh_used[i]) { zbx_fh[i] = real; zbx_fh_used[i] = 1; return i; }
    return -1;
}

/* Helper functions for math builtins */
static int gcd(int a, int b) {
    while (b != 0) { int t = b; b = a % b; a = t; }
    return a;
}

static int fp_sin(int x) {
    /* Normalize to [-PI, PI] */
    while (x > 205887) x -= 411774;
    while (x < -205887) x += 411774;
    /* Taylor series: sin(x) ≈ x - x^3/6 + x^5/120 */
    long long x2 = (long long)x * x >> 16;
    long long x3 = x2 * x >> 16;
    long long x5 = x3 * x2 >> 16;
    return (int)(x - x3 / 6 + x5 / 120);
}

static int fp_cos(int x) {
    return fp_sin(x + 102944);  /* cos(x) = sin(x + PI/2) */
}

/* Pure-int builtins (no string args). Return value goes to *out. */
static int ext_call(const char *name, int *a, int n, int *out) {
    *out = 0;
    /* ---- screen / TUI ---- */
    if (strcmp(name, "cls") == 0) {
        u8 col = (u8)(n > 0 ? a[0] : 0x07);
        vga_fill(0, 0, VGA_COLS, VGA_ROWS, ' ', col & 0x0F, (col >> 4) & 0x0F);
        return 1;
    }
    if (strcmp(name, "putcell") == 0) {   /* putcell(row,col,ch,color) */
        if (n >= 4) {
            u8 color = (u8)a[3];
            vga_put_cell(a[0], a[1], (char)a[2], color & 0x0F, (color >> 4) & 0x0F);
        }
        return 1;
    }
    if (strcmp(name, "present") == 0) { vga_present(); return 1; }
    if (strcmp(name, "scr_rows") == 0) { *out = VGA_ROWS; return 1; }
    if (strcmp(name, "scr_cols") == 0) { *out = VGA_COLS; return 1; }
    if (strcmp(name, "key") == 0) {       /* non-blocking, -1 if none */
        *out = kb_trygetc();
        return 1;
    }
    if (strcmp(name, "waitkey") == 0) { *out = kb_getc(); return 1; }

    /* ---- file ---- */
    if (strcmp(name, "fgetc") == 0) {     /* fgetc(fd) -> byte or -1 */
        if (n >= 1 && a[0] >= 0 && a[0] < ZBX_MAXFH && zbx_fh_used[a[0]]) {
            char c; int r = fs_read(zbx_fh[a[0]], &c, 1);
            *out = (r == 1) ? (u8)c : -1;
        } else *out = -1;
        return 1;
    }
    if (strcmp(name, "fputc") == 0) {     /* fputc(fd, ch) */
        if (n >= 2 && a[0] >= 0 && a[0] < ZBX_MAXFH && zbx_fh_used[a[0]]) {
            char c = (char)a[1];
            *out = fs_write(zbx_fh[a[0]], &c, 1);
        }
        return 1;
    }
    if (strcmp(name, "fclose") == 0) {
        if (n >= 1 && a[0] >= 0 && a[0] < ZBX_MAXFH && zbx_fh_used[a[0]]) {
            fs_close(zbx_fh[a[0]]); zbx_fh_used[a[0]] = 0;
        }
        return 1;
    }

    /* ---- time ---- */
    if (strcmp(name, "ticks") == 0) { *out = (int)pit_ticks(); return 1; }
    if (strcmp(name, "delay") == 0) {     /* delay(n_ticks) */
        if (n >= 1) {
            u32 start = pit_ticks();
            while ((u32)(pit_ticks() - start) < (u32)a[0]) __asm__ volatile("hlt");
        }
        return 1;
    }

    /* ---- stdlib ---- */
    if (strcmp(name, "abs") == 0)  { *out = n >= 1 ? (a[0] < 0 ? -a[0] : a[0]) : 0; return 1; }
    if (strcmp(name, "min") == 0)  { *out = n >= 2 ? (a[0] < a[1] ? a[0] : a[1]) : 0; return 1; }
    if (strcmp(name, "max") == 0)  { *out = n >= 2 ? (a[0] > a[1] ? a[0] : a[1]) : 0; return 1; }
    if (strcmp(name, "rand") == 0) {
        static u32 rng = 0xDEAD1234;
        rng = rng * 1664525 + 1013904223;
        *out = (n >= 1 && a[0] > 0) ? (int)((rng >> 1) % (u32)a[0]) : (int)(rng >> 1);
        return 1;
    }
    if (strcmp(name, "strlen") == 0) {
        /* strlen(ptr) -- counts pool elements until 0 */
        if (n >= 1) {
            int idx = a[0]; int len = 0;
            while (idx >= 1 && idx < pool_pos && pool[idx] != 0) { idx++; len++; }
            *out = len;
        }
        return 1;
    }
    if (strcmp(name, "memset") == 0) {
        /* memset(ptr, val, count) */
        if (n >= 3) {
            int idx = a[0], val = a[1], cnt = a[2];
            for (int i = 0; i < cnt && idx + i >= 1 && idx + i < pool_pos; i++)
                pool[idx + i] = val;
            *out = a[0];
        }
        return 1;
    }
    if (strcmp(name, "memcpy") == 0) {
        /* memcpy(dst, src, count) */
        if (n >= 3) {
            int dst = a[0], src = a[1], cnt = a[2];
            for (int i = 0; i < cnt; i++) {
                if (dst + i < 1 || dst + i >= pool_pos) break;
                if (src + i < 1 || src + i >= pool_pos) break;
                pool[dst + i] = pool[src + i];
            }
            *out = a[0];
        }
        return 1;
    }
    if (strcmp(name, "strcpy") == 0) {
        /* strcpy(dst, src) -- copies pool elements until 0 */
        if (n >= 2) {
            int dst = a[0], src = a[1];
            while (src >= 1 && src < pool_pos && dst >= 1 && dst < pool_pos) {
                pool[dst] = pool[src];
                if (!pool[src]) break;
                dst++; src++;
            }
            *out = a[0];
        }
        return 1;
    }
    if (strcmp(name, "strcat") == 0) {
        /* strcat(dst, src) */
        if (n >= 2) {
            int dst = a[0]; int src = a[1];
            while (dst >= 1 && dst < pool_pos && pool[dst]) dst++;
            while (src >= 1 && src < pool_pos && dst >= 1 && dst < pool_pos) {
                pool[dst] = pool[src];
                if (!pool[src]) break;
                dst++; src++;
            }
            *out = a[0];
        }
        return 1;
    }

    /* ---- windowing primitives ---- */
    if (strcmp(name, "frame") == 0) {     /* frame(r,c,w,h,color) -- single-line CP437 box */
        if (n >= 5) {
            int r0=a[0], c0=a[1], w=a[2], h=a[3]; u8 col=(u8)a[4];
            u8 fg = col & 0x0F, bg = (col >> 4) & 0x0F;
            if (w < 2 || h < 2) return 1;
            vga_put_cell(r0,         c0,         (char)0xDA, fg, bg);
            vga_put_cell(r0,         c0 + w - 1, (char)0xBF, fg, bg);
            vga_put_cell(r0 + h - 1, c0,         (char)0xC0, fg, bg);
            vga_put_cell(r0 + h - 1, c0 + w - 1, (char)0xD9, fg, bg);
            for (int x = c0 + 1; x < c0 + w - 1; x++) {
                vga_put_cell(r0,         x, (char)0xC4, fg, bg);
                vga_put_cell(r0 + h - 1, x, (char)0xC4, fg, bg);
            }
            for (int y = r0 + 1; y < r0 + h - 1; y++) {
                vga_put_cell(y, c0,         (char)0xB3, fg, bg);
                vga_put_cell(y, c0 + w - 1, (char)0xB3, fg, bg);
                for (int x = c0 + 1; x < c0 + w - 1; x++)
                    vga_put_cell(y, x, ' ', fg, bg);
            }
        }
        return 1;
    }
    if (strcmp(name, "button") == 0) {    /* button(r,c,w,color,hot) */
        if (n >= 4) {
            int r0=a[0], c0=a[1], w=a[2]; u8 col=(u8)a[3];
            int hot = (n >= 5) ? a[4] : 0;
            u8 fg = col & 0x0F, bg = (col >> 4) & 0x0F;
            if (hot) { u8 t = fg; fg = bg; bg = t; }
            for (int x = 0; x < w; x++)
                vga_put_cell(r0, c0 + x, ' ', fg, bg);
            vga_put_cell(r0, c0,         '[', fg, bg);
            vga_put_cell(r0, c0 + w - 1, ']', fg, bg);
        }
        return 1;
    }
    /* ---- mouse polling ---- */
    if (strcmp(name, "mouse_x") == 0) {
        extern void mouse_get(int *col, int *row, int *btn);
        int mc = 0, mr = 0, mb = 0; mouse_get(&mc, &mr, &mb);
        *out = mc; return 1;
    }
    if (strcmp(name, "mouse_y") == 0) {
        extern void mouse_get(int *col, int *row, int *btn);
        int mc = 0, mr = 0, mb = 0; mouse_get(&mc, &mr, &mb);
        *out = mr; return 1;
    }
    if (strcmp(name, "mouse_btn") == 0) {
        extern void mouse_get(int *col, int *row, int *btn);
        int mc = 0, mr = 0, mb = 0; mouse_get(&mc, &mr, &mb);
        *out = mb; return 1;
    }

    /* ---- stdlib functions ---- */
    if (strcmp(name, "atoi") == 0) {         /* atoi(str) */
        *out = (n >= 1) ? a[0] : 0;
        return 1;
    }
    if (strcmp(name, "abs") == 0) {          /* abs(x) */
        *out = (n >= 1) ? (a[0] < 0 ? -a[0] : a[0]) : 0;
        return 1;
    }
    if (strcmp(name, "rand") == 0) {         /* rand() */
        static unsigned int rng = 12345;
        rng = rng * 1103515245 + 12345;
        *out = (int)((rng >> 16) & 0x7FFF);
        return 1;
    }
    if (strcmp(name, "srand") == 0) {        /* srand(seed) */
        /* seed is passed through abs(a[0]) */
        return 1;
    }
    if (strcmp(name, "isqrt") == 0) {        /* isqrt(n) - integer sqrt */
        if (n >= 1 && a[0] > 0) {
            int x = a[0], y = (x + 1) / 2;
            while (y < x) { x = y; y = (x + a[0] / x) / 2; }
            *out = x;
        } else *out = 0;
        return 1;
    }
    if (strcmp(name, "ipow") == 0) {         /* ipow(base, exp) */
        if (n >= 2) {
            int base = a[0], exp = a[1], result = 1;
            for (int i = 0; i < exp; i++) result *= base;
            *out = result;
        } else *out = 1;
        return 1;
    }
    if (strcmp(name, "gcd") == 0) {          /* gcd(a, b) */
        if (n >= 2) {
            int x = a[0], y = a[1];
            while (y != 0) { int t = y; y = x % y; x = t; }
            *out = x;
        } else *out = 0;
        return 1;
    }
    if (strcmp(name, "lcm") == 0) {          /* lcm(a, b) */
        if (n >= 2) {
            int x = a[0], y = a[1];
            *out = (x / gcd(x, y)) * y;
        } else *out = 0;
        return 1;
    }
    if (strcmp(name, "popcount") == 0) {     /* popcount(x) */
        int v = a[0]; v = v - ((v >> 1) & 0x55555555);
        v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
        *out = (((v + (v >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
        return 1;
    }
    if (strcmp(name, "sleep_ms") == 0) {     /* sleep_ms(ms) */
        int ticks = (n >= 1) ? a[0] / 10 : 0;
        for (int i = 0; i < ticks; i++) asm volatile("sti; hlt; cli");
        return 1;
    }
    if (strcmp(name, "yield") == 0) {        /* yield() */
        asm volatile("sti; hlt; cli");
        return 1;
    }

    /* ---- math functions ---- */
    if (strcmp(name, "fp_mul") == 0) {       /* fp_mul(a, b) - fixed-point multiply */
        if (n >= 2) *out = (int)(((long long)a[0] * a[1]) >> 16);
        else *out = 0;
        return 1;
    }
    if (strcmp(name, "fp_div") == 0) {       /* fp_div(a, b) - fixed-point divide */
        if (n >= 2 && a[1] != 0) *out = (int)(((long long)a[0] << 16) / a[1]);
        else *out = 0;
        return 1;
    }
    if (strcmp(name, "fp_sin") == 0) {       /* fp_sin(angle) - fixed-point sin */
        /* Simple polynomial approximation */
        if (n >= 1) {
            int x = a[0];
            /* Normalize to [-PI, PI] */
            while (x > 205887) x -= 411774;
            while (x < -205887) x += 411774;
            /* Taylor series: sin(x) ≈ x - x^3/6 + x^5/120 */
            long long x2 = (long long)x * x >> 16;
            long long x3 = x2 * x >> 16;
            long long x5 = x3 * x2 >> 16;
            *out = (int)(x - x3 / 6 + x5 / 120);
        } else *out = 0;
        return 1;
    }
    if (strcmp(name, "fp_cos") == 0) {       /* fp_cos(angle) - fixed-point cos */
        /* cos(x) = sin(x + PI/2) */
        if (n >= 1) *out = fp_sin(a[0] + 102944);
        else *out = 1;
        return 1;
    }
    if (strcmp(name, "fp_tan") == 0) {       /* fp_tan(angle) - fixed-point tan */
        if (n >= 1) {
            int c = fp_cos(a[0]);
            if (c != 0) *out = (int)(((long long)fp_sin(a[0]) << 16) / c);
            else *out = 0;
        } else *out = 0;
        return 1;
    }
    if (strcmp(name, "fp_sqrt") == 0) {      /* fp_sqrt(x) - fixed-point sqrt */
        if (n >= 1 && a[0] > 0) {
            int x = a[0];
            int result = 0;
            int bit = 1 << 15;
            while (bit) {
                int temp = result | bit;
                if ((long long)temp * temp <= ((long long)x << 16))
                    result = temp;
                bit >>= 1;
            }
            *out = result;
        } else *out = 0;
        return 1;
    }
    if (strcmp(name, "fp_pow") == 0) {       /* fp_pow(base, exp) - fixed-point pow */
        if (n >= 2) {
            int base = a[0], exp = a[1];
            int result = 65536;  /* 1.0 in fixed-point */
            while (exp > 0) {
                if (exp & 1) result = (int)(((long long)result * base) >> 16);
                base = (int)(((long long)base * base) >> 16);
                exp >>= 1;
            }
            *out = result;
        } else *out = 1;
        return 1;
    }
    if (strcmp(name, "fp_log2") == 0) {      /* fp_log2(x) - fixed-point log2 */
        if (n >= 1 && a[0] > 0) {
            int x = a[0];
            int result = 0;
            while (x >= 131072) { x >>= 1; result += 65536; }
            while (x < 65536) { x <<= 1; result -= 65536; }
            *out = result;
        } else *out = 0;
        return 1;
    }
    if (strcmp(name, "fp_floor") == 0) {     /* fp_floor(x) */
        *out = (n >= 1) ? (a[0] & 0xFFFF0000) : 0;
        return 1;
    }
    if (strcmp(name, "fp_ceil") == 0) {      /* fp_ceil(x) */
        if (n >= 1) {
            *out = (a[0] + 0xFFFF) & 0xFFFF0000;
        } else *out = 0;
        return 1;
    }
    if (strcmp(name, "fp_round") == 0) {     /* fp_round(x) */
        if (n >= 1) {
            *out = (a[0] + 32768) & 0xFFFF0000;
        } else *out = 0;
        return 1;
    }
    if (strcmp(name, "deg_to_rad") == 0) {   /* deg_to_rad(deg) */
        if (n >= 1) *out = (int)(((long long)a[0] * 1143) >> 16);
        else *out = 0;
        return 1;
    }
    if (strcmp(name, "rad_to_deg") == 0) {   /* rad_to_deg(rad) */
        if (n >= 1) *out = (int)(((long long)a[0] << 16) / 1143);
        else *out = 0;
        return 1;
    }

    /* ---- time functions ---- */
    if (strcmp(name, "clock") == 0) {        /* clock() */
        extern u32 pit_ticks(void);
        *out = (int)pit_ticks();
        return 1;
    }
    if (strcmp(name, "ticks") == 0) {        /* ticks() */
        extern u32 pit_ticks(void);
        *out = (int)pit_ticks();
        return 1;
    }
    if (strcmp(name, "millis") == 0) {       /* millis() */
        extern u32 pit_ticks(void);
        *out = (int)pit_ticks() * 10;
        return 1;
    }
    if (strcmp(name, "seconds") == 0) {      /* seconds() */
        extern u32 pit_ticks(void);
        *out = (int)pit_ticks() / 100;
        return 1;
    }
    if (strcmp(name, "get_time") == 0) {     /* get_time() - returns ticks */
        extern u32 pit_ticks(void);
        *out = (int)pit_ticks();
        return 1;
    }
    if (strcmp(name, "date_str") == 0) {     /* date_str() - placeholder */
        *out = 0;
        return 1;
    }
    if (strcmp(name, "time_str") == 0) {     /* time_str() - placeholder */
        *out = 0;
        return 1;
    }
    if (strcmp(name, "stopwatch_start") == 0) { /* stopwatch_start() */
        extern u32 pit_ticks(void);
        *out = (int)pit_ticks();
        return 1;
    }
    if (strcmp(name, "stopwatch_stop") == 0) {  /* stopwatch_stop(start) */
        extern u32 pit_ticks(void);
        *out = (int)pit_ticks() - a[0];
        return 1;
    }

    /* ---- windowing functions ---- */
    if (strcmp(name, "win_create") == 0) {   /* win_create(r,c,w,h,color) */
        /* Simplified: returns a window ID */
        *out = 1;  /* placeholder */
        return 1;
    }
    if (strcmp(name, "win_destroy") == 0) {  /* win_destroy(id) */
        *out = 0;
        return 1;
    }
    if (strcmp(name, "win_show") == 0) {     /* win_show(id) */
        *out = 0;
        return 1;
    }
    if (strcmp(name, "win_hide") == 0) {     /* win_hide(id) */
        *out = 0;
        return 1;
    }
    if (strcmp(name, "win_move") == 0) {     /* win_move(id,r,c) */
        *out = 0;
        return 1;
    }
    if (strcmp(name, "win_resize") == 0) {   /* win_resize(id,w,h) */
        *out = 0;
        return 1;
    }
    if (strcmp(name, "win_clear") == 0) {    /* win_clear(id,color) */
        *out = 0;
        return 1;
    }
    if (strcmp(name, "win_putc") == 0) {     /* win_putc(id,r,c,ch,color) */
        *out = 0;
        return 1;
    }
    if (strcmp(name, "win_puts") == 0) {     /* win_puts(id,r,c,color,"str") */
        *out = 0;
        return 1;
    }
    if (strcmp(name, "win_present") == 0) {  /* win_present(id) */
        *out = 0;
        return 1;
    }
    if (strcmp(name, "win_get_event") == 0) { /* win_get_event(id) */
        *out = 0;
        return 1;
    }
    if (strcmp(name, "win_create_button") == 0) { /* win_create_button(id,r,c,w,"text",color) */
        *out = 1;
        return 1;
    }
    if (strcmp(name, "win_button_clicked") == 0) { /* win_button_clicked(widget_id) */
        *out = 0;
        return 1;
    }

    return 0;     /* not an extended builtin */
}

/* String-literal builtins: name + a path/text string + optional int
 * args. Returns 1 if it handled the call (result in *out). Called
 * from eval where the string literal is still available. */
static int ext_call_str(const char *name, const char *str,
                        int *a, int n, int *out) {
    *out = 0;
    if (strcmp(name, "fopen") == 0) {      /* fopen("path") -> fd or -1 */
        int real = fs_open(str);
        *out = (real < 0) ? -1 : zbx_fh_alloc(real);
        if (*out < 0 && real >= 0) fs_close(real);
        return 1;
    }
    if (strcmp(name, "fcreate") == 0) {    /* fcreate("path") -> fd or -1 */
        fs_unlink(str);
        if (fs_create(str) < 0) { *out = -1; return 1; }
        int real = fs_open(str);
        *out = (real < 0) ? -1 : zbx_fh_alloc(real);
        return 1;
    }
    if (strcmp(name, "at_puts") == 0) {    /* at_puts(row,col,color,"str") */
        if (n >= 3) {
            u8 color = (u8)a[2];
            vga_write(a[0], a[1], str, color & 0x0F, (color >> 4) & 0x0F);
        }
        return 1;
    }
    if (strcmp(name, "window") == 0) {     /* window(r,c,w,h,"title") */
        if (n >= 4) {
            int r0=a[0], c0=a[1], w=a[2], h=a[3];
            /* Mac-classic chrome: grey title bar with three "dots",
             * centred title, white body, simple right/bottom shadow.
             * Matches what the desktop draws around its widgets so a
             * .ZBX window looks at home next to a built-in one. */
            for (int x = 0; x < w; x++)
                vga_put_cell(r0, c0 + x, ' ', 0, 7);
            vga_put_cell(r0, c0 + 1, 0x07, 4, 7);
            vga_put_cell(r0, c0 + 2, 0x07, 14, 7);
            vga_put_cell(r0, c0 + 3, 0x07, 2, 7);
            int tlen = 0; while (str[tlen]) tlen++;
            int tx = c0 + (w - tlen) / 2;
            for (int i = 0; i < tlen; i++)
                vga_put_cell(r0, tx + i, str[i], 0, 7);
            for (int y = r0 + 1; y < r0 + h; y++)
                for (int x = 0; x < w; x++)
                    vga_put_cell(y, c0 + x, ' ', 0, 15);
            for (int y = r0 + 1; y < r0 + h + 1; y++)
                vga_put_cell(y, c0 + w, 0xB1, 8, 1);
            for (int x = c0 + 1; x < c0 + w + 1; x++)
                vga_put_cell(r0 + h, x, 0xB1, 8, 1);
        }
        return 1;
    }
    return 0;
}

/* ---- evaluator ----------------------------------------------- */
static int eval(int n_idx);

static int call_function(const char *name, int *args, int argn) {
    if (strcmp(name, "print")   == 0) { int r; builtin_print  (args, argn, &r); return r; }
    if (strcmp(name, "putchar") == 0) { int r; builtin_putchar(args, argn, &r); return r; }
    if (strcmp(name, "getchar") == 0) { int r; builtin_getchar(args, argn, &r); return r; }
    if (strcmp(name, "puts")    == 0) { /* arg is a string literal -- handled in eval */ return 0; }
    { int r; if (ext_call(name, args, argn, &r)) return r; }   /* ZBX runtime */

    for (int i = 0; i < func_count; i++) {
        if (strcmp(funcs[i].name, name) != 0) continue;
        if (scope_depth >= MAX_CALL) { zbc_err("call too deep"); return 0; }
        scope_depth++;
        scope_var_count[scope_depth - 1] = 0;
        for (int p = 0; p < funcs[i].param_count && p < argn; p++)
            set_local(funcs[i].params[p], args[p]);
        had_return = 0; return_value = 0;
        eval(funcs[i].body);
        had_return = 0;
        scope_depth--;
        return return_value;
    }
    zbc_err("undefined function");
    kprintf("       (called: %s)\n", name);
    return 0;
}

static int eval(int idx) {
    if (idx < 0 || had_error) return 0;
    /* Yield to the desktop every 256 eval calls so the UI stays
     * responsive during long-running interpreted programs. */
    { static int eval_yield; if (++eval_yield >= 256) { eval_yield = 0;
      extern void proc_yield(void); proc_yield(); } }
    struct node *n = &nodes[idx];
    switch (n->kind) {
    case N_NUM: return n->ival;
    case N_STR: return n->str_off;
    case N_VAR: {
        struct var *v = find_var(n->name);
        if (!v) { zbc_err("undefined variable"); kprintf("       (%s)\n", n->name); return 0; }
        return v->value;
    }
    case N_ASSIGN: {
        struct node *lhs = &nodes[n->child[0]];
        int rv = eval(n->child[1]);
        /* Array subscript as L-value: arr[i] = expr */
        if (lhs->kind == N_SUBSCRIPT) {
            int base = eval(lhs->child[0]);
            int idx  = eval(lhs->child[1]);
            int addr = base + idx;
            if (addr < 1 || addr >= pool_pos) { zbc_err("array index out of bounds"); return rv; }
            switch (n->op) {
            case T_ASSIGN:  pool[addr] = rv; break;
            case T_PLUSEQ:  pool[addr] += rv; break;
            case T_MINUSEQ: pool[addr] -= rv; break;
            case T_STAREQ:  pool[addr] *= rv; break;
            case T_SLASHEQ: pool[addr] = rv ? pool[addr] / rv : 0; break;
            }
            return pool[addr];
        }
        /* Struct member as L-value: f.x = expr */
        if (lhs->kind == N_MEMBER) {
            struct var *sv = NULL;
            if (lhs->child[0] >= 0 && nodes[lhs->child[0]].kind == N_VAR)
                sv = find_var(nodes[lhs->child[0]].name);
            int base = eval(lhs->child[0]);
            if (lhs->ival) base = pool[base]; /* arrow -> dereference */
            int off = 0;
            if (sv && sv->struct_type[0]) {
                for (int si = 0; si < struct_def_count; si++) {
                    if (strcmp(struct_defs[si].name, sv->struct_type) != 0) continue;
                    for (int fi = 0; fi < struct_defs[si].count; fi++) {
                        if (strcmp(struct_defs[si].fields[fi], lhs->name) == 0) { off = fi; break; }
                    }
                    break;
                }
            }
            int addr = base + off;
            if (addr < 1 || addr >= pool_pos) { zbc_err("struct member out of bounds"); return rv; }
            switch (n->op) {
            case T_ASSIGN:  pool[addr] = rv; break;
            case T_PLUSEQ:  pool[addr] += rv; break;
            case T_MINUSEQ: pool[addr] -= rv; break;
            case T_STAREQ:  pool[addr] *= rv; break;
            case T_SLASHEQ: pool[addr] = rv ? pool[addr] / rv : 0; break;
            }
            return pool[addr];
        }
        /* Deref as L-value: *ptr = expr */
        if (lhs->kind == N_DEREF) {
            int addr = eval(lhs->child[0]);
            if (addr < 1 || addr >= pool_pos) { zbc_err("null pointer dereference"); return rv; }
            pool[addr] = rv;
            return rv;
        }
        struct var *v = find_var(lhs->name);
        if (!v) {
            if (lhs->ival == 1) {       /* declaration */
                set_local(lhs->name, rv);
                /* If this is a struct var, tag struct_type (ival>0 on the N_ASSIGN node). */
                if (n->ival > 0) {
                    struct var *nv = find_var(lhs->name);
                    if (nv && (n->ival - 1) < struct_def_count)
                        strncpy(nv->struct_type, struct_defs[n->ival - 1].name, 31);
                }
                return rv;
            }
            zbc_err("assign to undefined variable");
            kprintf("       (%s)\n", lhs->name);
            return rv;
        }
        switch (n->op) {
        case T_ASSIGN:  v->value = rv; break;
        case T_PLUSEQ:  v->value += rv; break;
        case T_MINUSEQ: v->value -= rv; break;
        case T_STAREQ:  v->value *= rv; break;
        case T_SLASHEQ: v->value = rv ? v->value / rv : 0; break;
        }
        return v->value;
    }
    case N_BIN: {
        int a = eval(n->child[0]);
        if (n->op == T_AND) return a ? (eval(n->child[1]) != 0) : 0;
        if (n->op == T_OR)  return a ? 1 : (eval(n->child[1]) != 0);
        int b = eval(n->child[1]);
        switch (n->op) {
        case T_PLUS:  return a + b;
        case T_MINUS: return a - b;
        case T_STAR:  return a * b;
        case T_SLASH: return b ? a / b : 0;
        case T_PCT:   return b ? a % b : 0;
        case T_EQ:    return a == b;
        case T_NEQ:   return a != b;
        case T_LT:    return a < b;
        case T_GT:    return a > b;
        case T_LE:    return a <= b;
        case T_GE:    return a >= b;
        case T_BAND:  return a & b;
        case T_BOR:   return a | b;
        case T_BXOR:  return a ^ b;
        case T_SHL:   return a << b;
        case T_SHR:   return a >> b;
        }
        return 0;
    }
    case N_UN: {
        int a = eval(n->child[0]);
        if (n->op == T_MINUS) return -a;
        if (n->op == T_NOT)   return !a;
        if (n->op == T_TILDE) return ~a;  /* bitwise NOT */
        return a;
    }
    case N_CALL: {
        /* Collect arg values. */
        if (strcmp(n->name, "puts") == 0) {
            /* String literal expected. */
            int a = n->child[0];
            if (a < 0 || nodes[a].kind != N_STR) { zbc_err("puts needs a string"); return 0; }
            kputs(&strtab[nodes[a].str_off]);
            kputc('\n');
            return 0;
        }
        if (strcmp(n->name, "printf") == 0) {
            /* printf("fmt", args...) -- supports %d %i %u %x %c %s %%.
             * The format must be a string literal; %s args must be string
             * literals too (no char* variables in zbc yet). */
            int a = n->child[0];
            if (a < 0 || nodes[a].kind != N_STR) { zbc_err("printf needs a format string"); return 0; }
            const char *f = &strtab[nodes[a].str_off];
            int arg = nodes[a].next;     /* first value arg after the fmt */
            for (const char *q = f; *q; q++) {
                if (*q != '%') { kputc(*q); continue; }
                q++;
                if (*q == '%') { kputc('%'); continue; }
                if (*q == 's') {
                    if (arg >= 0 && nodes[arg].kind == N_STR) {
                        kputs(&strtab[nodes[arg].str_off]);
                        arg = nodes[arg].next;
                    }
                    continue;
                }
                int v = 0;
                if (arg >= 0) { v = eval(arg); arg = nodes[arg].next; }
                switch (*q) {
                    case 'd': case 'i': kprintf("%d", v); break;
                    case 'u':           kprintf("%u", (u32)v); break;
                    case 'x':           kprintf("%x", (u32)v); break;
                    case 'c':           kputc((char)v); break;
                    default:            kputc('%'); kputc(*q); break;
                }
            }
            return 0;
        }
        /* ZBX string-literal builtins: first arg may be a string,
         * remaining args are ints. fopen/fcreate take just the path;
         * at_puts takes (row,col,color,"str"). To keep the int-arg
         * convention we detect a leading OR trailing string literal. */
        {
            int first = n->child[0];
            int str_node = -1;
            /* Find the (single) string-literal argument, if any. */
            for (int c2 = first; c2 >= 0; c2 = nodes[c2].next)
                if (nodes[c2].kind == N_STR) { str_node = c2; break; }
            if (str_node >= 0) {
                int ia[8]; int ian = 0;
                for (int c2 = first; c2 >= 0 && ian < 8; c2 = nodes[c2].next)
                    if (c2 != str_node) ia[ian++] = eval(c2);
                int r;
                if (ext_call_str(n->name, &strtab[nodes[str_node].str_off],
                                 ia, ian, &r))
                    return r;
            }
        }
        int args[8]; int an = 0;
        int c = n->child[0];
        while (c >= 0 && an < 8) {
            args[an++] = eval(c);
            c = nodes[c].next;
        }
        return call_function(n->name, args, an);
    }
    case N_IF: {
        if (eval(n->child[0])) eval(n->child[1]);
        else if (n->child[2] >= 0) eval(n->child[2]);
        return 0;
    }
    case N_WHILE: {
        while (!had_return && !had_break && !had_error && eval(n->child[0])) {
            had_continue = 0;
            eval(n->child[1]);
        }
        had_break = 0;
        return 0;
    }
    case N_FOR: {
        /* child[0]=init child[1]=cond child[2]=post child[3]=body.
         * `continue` skips the rest of the body but still runs post. */
        if (n->child[0] >= 0) eval(n->child[0]);
        while (!had_return && !had_break && !had_error) {
            if (n->child[1] >= 0 && !eval(n->child[1])) break;
            had_continue = 0;
            if (n->child[3] >= 0) eval(n->child[3]);
            had_continue = 0;
            if (n->child[2] >= 0) eval(n->child[2]);
        }
        had_break = 0;
        return 0;
    }
    case N_BLOCK: {
        int c = n->child[0];
        while (c >= 0 && !had_return && !had_break && !had_continue && !had_error) {
            eval(c);
            c = nodes[c].next;
        }
        return 0;
    }
    case N_RETURN:
        if (n->child[0] >= 0) return_value = eval(n->child[0]);
        had_return = 1;
        return return_value;
    case N_BREAK: had_break = 1; return 0;
    case N_CONT:  had_continue = 1; return 0;
    case N_EXPR_STMT: return eval(n->child[0]);
    case N_DOWHILE: {
        do {
            had_continue = 0;
            eval(n->child[0]);
        } while (!had_return && !had_break && !had_error && eval(n->child[1]));
        had_break = 0;
        return 0;
    }
    case N_SUBSCRIPT: {
        int base = eval(n->child[0]);
        int idx  = eval(n->child[1]);
        int addr = base + idx;
        if (addr < 1 || addr >= pool_pos) { zbc_err("array index out of bounds"); return 0; }
        return pool[addr];
    }
    case N_MEMBER: {
        struct var *sv = NULL;
        if (n->child[0] >= 0 && nodes[n->child[0]].kind == N_VAR)
            sv = find_var(nodes[n->child[0]].name);
        int base = eval(n->child[0]);
        if (n->ival) base = (base >= 1 && base < pool_pos) ? pool[base] : 0; /* -> dereference */
        int off = 0;
        if (sv && sv->struct_type[0]) {
            for (int si = 0; si < struct_def_count; si++) {
                if (strcmp(struct_defs[si].name, sv->struct_type) != 0) continue;
                for (int fi = 0; fi < struct_defs[si].count; fi++) {
                    if (strcmp(struct_defs[si].fields[fi], n->name) == 0) { off = fi; break; }
                }
                break;
            }
        }
        int addr = base + off;
        if (addr < 1 || addr >= pool_pos) { zbc_err("struct member access out of bounds"); return 0; }
        return pool[addr];
    }
    case N_DEREF: {
        int addr = eval(n->child[0]);
        if (addr < 1 || addr >= pool_pos) { zbc_err("null dereference"); return 0; }
        return pool[addr];
    }
    case N_ADDR: {
        if (n->child[0] >= 0 && nodes[n->child[0]].kind == N_VAR) {
            struct var *v = find_var(nodes[n->child[0]].name);
            if (v) return v->value;  /* pool base address */
        }
        return 0;
    }
    case N_INCR: {
        /* ival=0 prefix (++x), ival=1 postfix (x++) */
        if (n->child[0] >= 0 && nodes[n->child[0]].kind == N_VAR) {
            struct var *v = find_var(nodes[n->child[0]].name);
            if (!v) { zbc_err("undefined variable"); return 0; }
            if (n->op == T_INCR) { int old = v->value; v->value++; return n->ival ? old : v->value; }
            else                  { int old = v->value; v->value--; return n->ival ? old : v->value; }
        }
        return 0;
    }
    case N_SWITCH: {
        int val = eval(n->child[0]);
        int c = n->child[1];  /* linked list of cases */
        int matched = 0;
        int default_case = -1;
        /* First pass: find matching case */
        while (c >= 0) {
            if (nodes[c].kind == N_CASE) {
                if (val == nodes[c].ival) { matched = 1; break; }
            } else if (nodes[c].kind == N_DEFAULT) {
                default_case = c;
            }
            c = nodes[c].next;
        }
        /* If no case matched, try default */
        if (!matched && default_case >= 0) c = default_case;
        else if (!matched) return 0;  /* no match, no default */
        /* Execute from matched case onward (fall-through) */
        while (c >= 0 && !had_return && !had_break && !had_error) {
            /* Execute statements in this case */
            int s = nodes[c].child[0];
            while (s >= 0 && !had_return && !had_break && !had_error) {
                eval(s);
                s = nodes[s].next;
            }
            if (had_break) break;
            c = nodes[c].next;
        }
        had_break = 0;
        return 0;
    }
    case N_TERNARY: {
        if (eval(n->child[0])) return eval(n->child[1]);
        else return eval(n->child[2]);
    }
    case N_COMMA: {
        eval(n->child[0]);
        return eval(n->child[1]);
    }
    case N_SIZEOF:
        return 4;  /* always int = 4 bytes */
    default: return 0;
    }
}

/* ---- entry point --------------------------------------------- */

/* ZBE = Zenbite Executable: a thin 8-byte header + the source text.
 *   bytes 0..3 : magic "ZBE!"
 *   bytes 4..7 : reserved, zero
 *   bytes 8..  : the C source (NUL-terminated optional)
 *
 * `cc src.c -o name` writes name.zbe. Typing `name` from the shell reads
 * the file -- if it starts with the magic the header is skipped, otherwise
 * the whole file is treated as raw source. So `cc` can also run plain .c
 * files. */
static const char ZBE_MAGIC[4] = { 'Z', 'B', 'E', '!' };

static int read_source(const char *path, char *buf, int max) {
    int h = fs_open(path);
    if (h < 0) { kprintf("ZBC: cannot open %s\n", path); return -1; }
    int size = fs_size(h);
    if (size <= 0 || size >= max) { fs_close(h); kputs("ZBC: source too big or empty\n"); return -1; }
    int n = fs_read(h, buf, (size_t)size);
    fs_close(h);
    if (n < 0) { kputs("ZBC: read failed\n"); return -1; }
    buf[n] = '\0';
    /* Skip the ZBE header if present. */
    if (n >= 8 && memcmp(buf, ZBE_MAGIC, 4) == 0) {
        for (int i = 0; i < n - 8; i++) buf[i] = buf[i + 8];
        buf[n - 8] = '\0';
    }
    return n;
}

int zbc_compile(const char *src_path, const char *out_path) {
    static char tmp[MAX_SRC];
    int n = read_source(src_path, tmp, MAX_SRC);
    if (n < 0) return -1;
    fs_unlink(out_path);
    if (fs_create(out_path) < 0) { kprintf("ZBC: cannot create %s\n", out_path); return -1; }
    int h = fs_open(out_path);
    if (h < 0) return -1;
    char header[8] = { 'Z', 'B', 'E', '!', 0, 0, 0, 0 };
    fs_write(h, header, 8);
    int slen = (int)strlen(tmp);
    fs_write(h, tmp, (size_t)slen);
    fs_close(h);
    return 0;
}

/* Run from an in-memory source string. Shared by zbc_run (reads a
 * file) and zbx_run (strips the ZBX header first). Resets the ZBX
 * runtime so file handles from a previous program don't leak. */
int zbc_run_src(const char *source) {
    int slen = (int)strlen(source);
    if (slen >= MAX_SRC) slen = MAX_SRC - 1;
    memcpy(src, source, slen);
    src[slen] = '\0';

    tok_count = tok_pos = strtab_pos = node_count = 0;
    func_count = global_count = scope_depth = 0;
    had_error = had_return = had_break = had_continue = 0;
    pool_pos = 1;           /* 0 = null/uninitialized */
    struct_def_count = 0;
    zbx_runtime_reset();

    if (tokenize(src) < 0) return -1;

    /* Top-level: a sequence of "int main(...) { ... }" or "void name(...) { ... }". */
    while (peek()->kind != T_EOF && !had_error) {
        int is_void = 0;
        if (accept(T_KW_INT)) {
            is_void = 0;
        } else if (accept(T_KW_VOID)) {
            is_void = 1;
        } else {
            zbc_err("expected function definition");
            break;
        }
        parse_func(is_void);
    }
    if (had_error) { zbx_runtime_cleanup(); return -1; }

    int args[1] = {0};
    int rc = call_function("main", args, 0);
    zbx_runtime_cleanup();
    return rc;
}

int zbc_run(const char *path) {
    if (read_source(path, src, MAX_SRC) < 0) return -1;
    static char copy[MAX_SRC];
    int n = (int)strlen(src);
    if (n >= MAX_SRC) n = MAX_SRC - 1;
    memcpy(copy, src, n); copy[n] = '\0';
    return zbc_run_src(copy);
}

/* ---- ZBX executable format ---------------------------------------
 * A .ZBX file is the C source for a program prefixed with an 8-byte
 * header:
 *     bytes 0..3 : magic 'Z','B','X','1'
 *     byte  4    : flags (bit0 = fullscreen TUI app)
 *     bytes 5..7 : reserved
 *     bytes 8..  : C source text
 * Files without the magic are treated as plain source, so a .c written
 * with host gcc-style syntax (within the zbc subset) runs unchanged.
 */
#define ZBX_FLAG_FULLSCREEN 0x01

/* Returns the flags byte (>=0) on success, or -1 if the file can't be
 * read. The source is left in the static `src` buffer; *out_src points
 * at the start of the program text within it. */
int zbx_inspect(const char *path, const char **out_src) {
    static char raw[MAX_SRC];
    int h = fs_open(path);
    if (h < 0) { kprintf("run: %s not found\n", path); return -1; }
    int total = 0, r;
    while (total < MAX_SRC - 1 &&
           (r = fs_read(h, raw + total, MAX_SRC - 1 - total)) > 0)
        total += r;
    fs_close(h);
    raw[total] = '\0';
    int flags = 0;
    const char *s = raw;
    if (total >= 8 && raw[0]=='Z' && raw[1]=='B' && raw[2]=='X' && raw[3]=='1') {
        flags = (u8)raw[4];
        s = raw + 8;
    }
    if (out_src) *out_src = s;
    return flags;
}

/* Build a .ZBX from a .c source. flags forwarded into the header. */
int zbx_make(const char *src_path, const char *out_path, int flags) {
    static char tmp[MAX_SRC];
    int n = read_source(src_path, tmp, MAX_SRC);
    if (n < 0) return -1;
    fs_unlink(out_path);
    if (fs_create(out_path) < 0) { kprintf("zbx: cannot create %s\n", out_path); return -1; }
    int h = fs_open(out_path);
    if (h < 0) return -1;
    char header[8] = { 'Z','B','X','1', (char)(u8)flags, 0,0,0 };
    fs_write(h, header, 8);
    fs_write(h, tmp, (size_t)strlen(tmp));
    fs_close(h);
    return 0;
}

/* Run a .ZBX (or plain .c). Returns the program's main() return value,
 * or -1 on load/parse error. Fullscreen apps are bracketed with
 * tui_end()/tui_init() by the caller (shell vs desktop differ), so we
 * just report the flag via zbx_is_fullscreen for them to inspect. */
static int g_zbx_last_flags;
int zbx_is_fullscreen(void) { return (g_zbx_last_flags & ZBX_FLAG_FULLSCREEN) != 0; }

int zbx_run(const char *path) {
    const char *source;
    int flags = zbx_inspect(path, &source);
    if (flags < 0) return -1;
    g_zbx_last_flags = flags;
    return zbc_run_src(source);
}
