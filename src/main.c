#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *text;
    int number;
} SourceLine;

typedef struct {
    SourceLine *lines;
    size_t count;
    char *path;
} Source;

typedef struct {
    char *name;
    int64_t value;
    int line;
    bool is_equ;
} Symbol;

typedef struct {
    Symbol *items;
    size_t len;
    size_t cap;
} SymbolTable;

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
    int64_t base;
    bool have_base;
} Output;

typedef struct {
    const char *filename;
    SymbolTable symbols;
    char *current_scope;
    char error[512];
    bool has_error;
} Assembler;

typedef enum {
    R8_B = 0,
    R8_C = 1,
    R8_D = 2,
    R8_E = 3,
    R8_H = 4,
    R8_L = 5,
    R8_A = 7,
    R8_I = 8,
    R8_R = 9,
    R8_INVALID = -1
} Reg8;

typedef enum {
    R16_BC = 0,
    R16_DE = 1,
    R16_HL = 2,
    R16_SP = 3,
    R16_IX = 4,
    R16_IY = 5,
    R16_AF = 6,
    R16_INVALID = -1
} Reg16;

typedef enum {
    COND_NZ = 0,
    COND_Z = 1,
    COND_NC = 2,
    COND_C = 3,
    COND_PO = 4,
    COND_PE = 5,
    COND_P = 6,
    COND_M = 7,
    COND_INVALID = -1
} Cond;

typedef enum {
    OP_IMM,
    OP_REG8,
    OP_REG16,
    OP_COND,
    OP_MEM_REG,
    OP_MEM_C,
    OP_MEM_INDEX,
    OP_MEM_IMM,
    OP_AF_ALT
} OperandType;

typedef struct {
    OperandType type;
    Reg8 r8;
    Reg16 r16;
    Cond cond;
    Reg16 index_reg;
    char *expr;
    bool explicit_disp;
} Operand;

typedef struct {
    int pass;
    Assembler *asmr;
    Output *out;
    int64_t *pc;
    const SourceLine *line;
} EmitCtx;

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
    return p;
}

static void *xrealloc(void *ptr, size_t n) {
    void *p = realloc(ptr, n ? n : 1);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
    return p;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s);
    char *out = xmalloc(n + 1);
    memcpy(out, s, n + 1);
    return out;
}

static char *xstrndup2(const char *s, size_t n) {
    char *out = xmalloc(n + 1);
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static bool eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static bool starts_ci(const char *s, const char *prefix) {
    while (*prefix) {
        if (toupper((unsigned char)*s) != toupper((unsigned char)*prefix)) {
            return false;
        }
        s++;
        prefix++;
    }
    return true;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) {
        s++;
    }
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return s;
}

static bool is_ident_start_char(char c) {
    return isalpha((unsigned char)c) || c == '_' || c == '.';
}

static bool is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '.' || c == '$';
}

static const char *symbol_token_end(const char *p) {
    if (is_ident_start_char(*p)) {
        p++;
        while (is_ident_char(*p)) {
            p++;
        }
        return p;
    }
    if (isdigit((unsigned char)*p)) {
        const char *q = p;
        while (isdigit((unsigned char)*q)) {
            q++;
        }
        if (*q == '$') {
            return q + 1;
        }
    }
    return NULL;
}

static bool is_sdcc_numeric_local(const char *name) {
    const char *p = name;
    if (!isdigit((unsigned char)*p)) {
        return false;
    }
    while (isdigit((unsigned char)*p)) {
        p++;
    }
    return p[0] == '$' && p[1] == '\0';
}

static bool is_sdcc_numeric_local_token(const char *start, const char *end) {
    if (start >= end || !isdigit((unsigned char)*start)) {
        return false;
    }
    const char *p = start;
    while (p < end && isdigit((unsigned char)*p)) {
        p++;
    }
    return p + 1 == end && *p == '$';
}

static bool is_local_symbol_name(const char *name) {
    return name[0] == '.' || is_sdcc_numeric_local(name);
}

static bool starts_single_quote(const char *start, const char *p) {
    return p == start || !is_ident_char(p[-1]);
}

static int set_error(Assembler *asmr, const SourceLine *line, const char *fmt, ...) {
    if (!asmr->has_error) {
        va_list ap;
        va_start(ap, fmt);
        if (line && line->number > 0) {
            int used = snprintf(asmr->error, sizeof(asmr->error), "%s:%d: ",
                                asmr->filename, line->number);
            if (used < 0 || (size_t)used >= sizeof(asmr->error)) {
                used = 0;
            }
            vsnprintf(asmr->error + used, sizeof(asmr->error) - (size_t)used, fmt, ap);
        } else {
            vsnprintf(asmr->error, sizeof(asmr->error), fmt, ap);
        }
        va_end(ap);
        asmr->has_error = true;
    }
    return -1;
}

static Symbol *find_symbol(SymbolTable *tab, const char *name) {
    for (size_t i = 0; i < tab->len; i++) {
        if (strcmp(tab->items[i].name, name) == 0) {
            return &tab->items[i];
        }
    }
    return NULL;
}

static void set_current_scope(Assembler *asmr, const char *name) {
    free(asmr->current_scope);
    asmr->current_scope = name ? xstrdup(name) : NULL;
}

static char *qualify_symbol_name(Assembler *asmr, const char *name) {
    if (is_local_symbol_name(name) && asmr->current_scope && asmr->current_scope[0]) {
        size_t scope_len = strlen(asmr->current_scope);
        size_t name_len = strlen(name);
        bool needs_dot = name[0] != '.';
        char *full = xmalloc(scope_len + (needs_dot ? 1 : 0) + name_len + 1);
        memcpy(full, asmr->current_scope, scope_len);
        if (needs_dot) {
            full[scope_len] = '.';
        }
        memcpy(full + scope_len + (needs_dot ? 1 : 0), name, name_len + 1);
        return full;
    }
    return xstrdup(name);
}

static int define_symbol(Assembler *asmr, const SourceLine *line, const char *name,
                         int64_t value, bool is_equ) {
    Symbol *existing = find_symbol(&asmr->symbols, name);
    if (existing) {
        return set_error(asmr, line, "duplicate symbol '%s' previously defined on line %d",
                         name, existing->line);
    }
    if (asmr->symbols.len == asmr->symbols.cap) {
        size_t new_cap = asmr->symbols.cap ? asmr->symbols.cap * 2 : 64;
        asmr->symbols.items = xrealloc(asmr->symbols.items, new_cap * sizeof(Symbol));
        asmr->symbols.cap = new_cap;
    }
    Symbol *sym = &asmr->symbols.items[asmr->symbols.len++];
    sym->name = xstrdup(name);
    sym->value = value;
    sym->line = line ? line->number : 0;
    sym->is_equ = is_equ;
    return 0;
}

static void free_symbols(SymbolTable *tab) {
    for (size_t i = 0; i < tab->len; i++) {
        free(tab->items[i].name);
    }
    free(tab->items);
    tab->items = NULL;
    tab->len = 0;
    tab->cap = 0;
}

static int define_label_list(Assembler *asmr, const SourceLine *line, char **labels,
                             size_t label_count, int64_t value, bool is_equ, int pass) {
    for (size_t i = 0; i < label_count; i++) {
        char *full = qualify_symbol_name(asmr, labels[i]);
        if (pass == 1 && define_symbol(asmr, line, full, value, is_equ) != 0) {
            free(full);
            return -1;
        }
        free(full);
        if (!is_local_symbol_name(labels[i])) {
            set_current_scope(asmr, labels[i]);
        }
    }
    return 0;
}

static void output_free(Output *out) {
    free(out->data);
    out->data = NULL;
    out->len = 0;
    out->cap = 0;
    out->base = 0;
    out->have_base = false;
}

static int output_ensure(Output *out, Assembler *asmr, const SourceLine *line, int64_t pc) {
    if (!out->have_base) {
        out->base = pc;
        out->have_base = true;
    }
    if (pc < out->base) {
        return set_error(asmr, line, "address moved before output base");
    }
    uint64_t off64 = (uint64_t)(pc - out->base);
    if (off64 > SIZE_MAX) {
        return set_error(asmr, line, "output is too large");
    }
    size_t need = (size_t)off64;
    if (need > out->len) {
        if (need > out->cap) {
            size_t new_cap = out->cap ? out->cap : 256;
            while (new_cap < need) {
                if (new_cap > SIZE_MAX / 2) {
                    return set_error(asmr, line, "output is too large");
                }
                new_cap *= 2;
            }
            out->data = xrealloc(out->data, new_cap);
            out->cap = new_cap;
        }
        memset(out->data + out->len, 0, need - out->len);
        out->len = need;
    }
    return 0;
}

static int output_emit(Output *out, Assembler *asmr, const SourceLine *line, int64_t pc,
                       unsigned char value) {
    if (output_ensure(out, asmr, line, pc) != 0) {
        return -1;
    }
    uint64_t off64 = (uint64_t)(pc - out->base);
    if (off64 >= SIZE_MAX) {
        return set_error(asmr, line, "output is too large");
    }
    size_t off = (size_t)off64;
    if (off == out->cap) {
        size_t new_cap = out->cap ? out->cap * 2 : 256;
        out->data = xrealloc(out->data, new_cap);
        out->cap = new_cap;
    }
    if (off == out->len) {
        out->len++;
    }
    out->data[off] = value;
    return 0;
}

static int emit_byte(EmitCtx *ctx, unsigned char value) {
    if (ctx->pass == 2 && output_emit(ctx->out, ctx->asmr, ctx->line, *ctx->pc, value) != 0) {
        return -1;
    }
    (*ctx->pc)++;
    return 0;
}

static int output_set_org(Output *out, Assembler *asmr, const SourceLine *line,
                          int64_t current_pc, int64_t new_pc) {
    if (!out->have_base) {
        out->base = new_pc;
        out->have_base = true;
        return 0;
    }
    if (new_pc < current_pc) {
        return set_error(asmr, line, "ORG moved backward from 0x%llX to 0x%llX",
                         (long long)current_pc, (long long)new_pc);
    }
    return output_ensure(out, asmr, line, new_pc);
}

static char *strip_comment(const char *line) {
    char *copy = xstrdup(line);
    char quote = '\0';
    bool esc = false;
    for (char *p = copy; *p; p++) {
        if (esc) {
            esc = false;
            continue;
        }
        if (quote) {
            if (*p == '\\') {
                esc = true;
            } else if (*p == quote) {
                quote = '\0';
            }
            continue;
        }
        if (*p == '"' || (*p == '\'' && starts_single_quote(copy, p))) {
            quote = *p;
        } else if (*p == ';') {
            *p = '\0';
            break;
        }
    }
    return copy;
}

static Source read_source_or_die(const char *path) {
    Source src;
    memset(&src, 0, sizeof(src));
    src.path = xstrdup(path);

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        exit(1);
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        fclose(f);
        exit(1);
    }
    long size = ftell(f);
    if (size < 0) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        fclose(f);
        exit(1);
    }
    rewind(f);
    char *buf = xmalloc((size_t)size + 1);
    size_t got = fread(buf, 1, (size_t)size, f);
    if (got != (size_t)size) {
        fprintf(stderr, "%s: read failed\n", path);
        fclose(f);
        free(buf);
        exit(1);
    }
    fclose(f);
    buf[size] = '\0';

    size_t cap = 0;
    const char *start = buf;
    int line_no = 1;
    for (char *p = buf; ; p++) {
        if (*p == '\n' || *p == '\0') {
            const char *end = p;
            if (end > start && end[-1] == '\r') {
                end--;
            }
            if (src.count == cap) {
                cap = cap ? cap * 2 : 128;
                src.lines = xrealloc(src.lines, cap * sizeof(SourceLine));
            }
            src.lines[src.count].text = xstrndup2(start, (size_t)(end - start));
            src.lines[src.count].number = line_no++;
            src.count++;
            if (*p == '\0') {
                break;
            }
            start = p + 1;
        }
    }
    free(buf);
    return src;
}

static void free_source(Source *src) {
    for (size_t i = 0; i < src->count; i++) {
        free(src->lines[i].text);
    }
    free(src->lines);
    free(src->path);
    memset(src, 0, sizeof(*src));
}

typedef struct {
    char **items;
    size_t len;
} StringList;

static void free_string_list(StringList *list) {
    for (size_t i = 0; i < list->len; i++) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->len = 0;
}

static int split_list(Assembler *asmr, const SourceLine *line, const char *text,
                      StringList *out) {
    memset(out, 0, sizeof(*out));
    size_t cap = 0;
    const char *start = text;
    int depth = 0;
    char quote = '\0';
    bool esc = false;
    for (const char *p = text; ; p++) {
        char c = *p;
        if (esc) {
            esc = false;
        } else if (quote) {
            if (c == '\\') {
                esc = true;
            } else if (c == quote) {
                quote = '\0';
            } else if (c == '\0') {
                free_string_list(out);
                return set_error(asmr, line, "unterminated quoted string");
            }
        } else {
            if (c == '"' || (c == '\'' && starts_single_quote(text, p))) {
                quote = c;
            } else if (c == '(') {
                depth++;
            } else if (c == ')') {
                if (depth == 0) {
                    free_string_list(out);
                    return set_error(asmr, line, "unmatched ')'");
                }
                depth--;
            } else if (c == ',' || c == '\0') {
                char *item = xstrndup2(start, (size_t)(p - start));
                char *t = trim(item);
                if (*t == '\0') {
                    free(item);
                    free_string_list(out);
                    return set_error(asmr, line, "empty list item");
                }
                char *final = xstrdup(t);
                free(item);
                if (out->len == cap) {
                    cap = cap ? cap * 2 : 8;
                    out->items = xrealloc(out->items, cap * sizeof(char *));
                }
                out->items[out->len++] = final;
                if (c == '\0') {
                    break;
                }
                start = p + 1;
            }
        }
    }
    if (depth != 0) {
        free_string_list(out);
        return set_error(asmr, line, "unmatched '('");
    }
    return 0;
}

static bool parse_label_at_start(char **p_io, char **name_out) {
    char *p = trim(*p_io);
    const char *q_const = symbol_token_end(p);
    if (!q_const) {
        return false;
    }
    char *q = (char *)q_const;
    char *after = q;
    while (isspace((unsigned char)*after)) {
        after++;
    }
    if (*after != ':') {
        return false;
    }
    *name_out = xstrndup2(p, (size_t)(q - p));
    *p_io = after + (after[1] == ':' ? 2 : 1);
    return true;
}

static bool parse_equ_line(char *p, char **name_out, char **expr_out) {
    p = trim(p);
    const char *q_const = symbol_token_end(p);
    if (!q_const) {
        return false;
    }
    char *q = (char *)q_const;
    char *after_name = q;
    while (isspace((unsigned char)*after_name)) {
        after_name++;
    }
    if (!starts_ci(after_name, "EQU")) {
        return false;
    }
    char *after_equ = after_name + 3;
    if (*after_equ && !isspace((unsigned char)*after_equ)) {
        return false;
    }
    after_equ = trim(after_equ);
    if (*after_equ == '\0') {
        return false;
    }
    *name_out = xstrndup2(p, (size_t)(q - p));
    *expr_out = xstrdup(after_equ);
    return true;
}

static Reg8 parse_reg8_name(const char *s) {
    if (eq_ci(s, "B")) return R8_B;
    if (eq_ci(s, "C")) return R8_C;
    if (eq_ci(s, "D")) return R8_D;
    if (eq_ci(s, "E")) return R8_E;
    if (eq_ci(s, "H")) return R8_H;
    if (eq_ci(s, "L")) return R8_L;
    if (eq_ci(s, "A")) return R8_A;
    if (eq_ci(s, "I")) return R8_I;
    if (eq_ci(s, "R")) return R8_R;
    return R8_INVALID;
}

static Reg16 parse_reg16_name(const char *s) {
    if (eq_ci(s, "BC")) return R16_BC;
    if (eq_ci(s, "DE")) return R16_DE;
    if (eq_ci(s, "HL")) return R16_HL;
    if (eq_ci(s, "SP")) return R16_SP;
    if (eq_ci(s, "IX")) return R16_IX;
    if (eq_ci(s, "IY")) return R16_IY;
    if (eq_ci(s, "AF")) return R16_AF;
    return R16_INVALID;
}

static Cond parse_cond_name(const char *s) {
    if (eq_ci(s, "NZ")) return COND_NZ;
    if (eq_ci(s, "Z")) return COND_Z;
    if (eq_ci(s, "NC")) return COND_NC;
    if (eq_ci(s, "C")) return COND_C;
    if (eq_ci(s, "PO")) return COND_PO;
    if (eq_ci(s, "PE")) return COND_PE;
    if (eq_ci(s, "P")) return COND_P;
    if (eq_ci(s, "M")) return COND_M;
    return COND_INVALID;
}

static void free_operands(Operand *ops, size_t n) {
    for (size_t i = 0; i < n; i++) {
        free(ops[i].expr);
    }
}

static bool is_wrapped_by_parens(const char *s) {
    size_t n = strlen(s);
    if (n < 2 || s[0] != '(' || s[n - 1] != ')') {
        return false;
    }
    int depth = 0;
    char quote = '\0';
    bool esc = false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (esc) {
            esc = false;
            continue;
        }
        if (quote) {
            if (c == '\\') {
                esc = true;
            } else if (c == quote) {
                quote = '\0';
            }
            continue;
        }
        if (c == '"' || (c == '\'' && starts_single_quote(s, s + i))) {
            quote = c;
        } else if (c == '(') {
            depth++;
        } else if (c == ')') {
            depth--;
            if (depth == 0 && i != n - 1) {
                return false;
            }
        }
    }
    return depth == 0 && !quote;
}

static char *signed_expr_text(char sign, char *rest) {
    rest = trim(rest);
    if (sign == '+') {
        return xstrdup(rest);
    }
    size_t n = strlen(rest);
    char *out = xmalloc(n + 2);
    out[0] = '-';
    memcpy(out + 1, rest, n + 1);
    return out;
}

static int parse_index_inner(const char *inner, Reg16 *reg_out, char **expr_out,
                             bool *explicit_disp_out) {
    char *copy = xstrdup(inner);
    char *p = trim(copy);
    Reg16 reg = R16_INVALID;
    if (starts_ci(p, "IX")) {
        reg = R16_IX;
        p += 2;
    } else if (starts_ci(p, "IY")) {
        reg = R16_IY;
        p += 2;
    } else {
        free(copy);
        return 0;
    }
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '\0') {
        *reg_out = reg;
        *expr_out = xstrdup("0");
        *explicit_disp_out = false;
        free(copy);
        return 1;
    }
    if (*p != '+' && *p != '-') {
        free(copy);
        return 0;
    }
    char sign = *p++;
    char *rest = trim(p);
    if (*rest == '\0') {
        free(copy);
        return 0;
    }
    *reg_out = reg;
    *expr_out = signed_expr_text(sign, rest);
    *explicit_disp_out = true;
    free(copy);
    return 1;
}

static int parse_sdcc_index_operand(const char *text, Reg16 *reg_out, char **expr_out) {
    char *copy = xstrdup(text);
    char *p = trim(copy);
    size_t n = strlen(p);
    if (n < 4 || p[n - 1] != ')') {
        free(copy);
        return 0;
    }
    p[n - 1] = '\0';
    char *open = strrchr(p, '(');
    if (!open) {
        free(copy);
        return 0;
    }
    char *reg_text = trim(open + 1);
    Reg16 reg = parse_reg16_name(reg_text);
    if (reg != R16_IX && reg != R16_IY) {
        free(copy);
        return 0;
    }
    *open = '\0';
    char *expr = trim(p);
    if (*expr == '\0') {
        free(copy);
        return 0;
    }
    *reg_out = reg;
    *expr_out = xstrdup(expr);
    free(copy);
    return 1;
}

static int parse_operand(const char *text, Operand *op) {
    memset(op, 0, sizeof(*op));
    op->type = OP_IMM;
    op->r8 = R8_INVALID;
    op->r16 = R16_INVALID;
    op->cond = COND_INVALID;
    op->index_reg = R16_INVALID;
    char *copy = xstrdup(text);
    char *p = trim(copy);

    if (eq_ci(p, "AF'")) {
        op->type = OP_AF_ALT;
        free(copy);
        return 0;
    }

    Reg8 r8 = parse_reg8_name(p);
    if (r8 != R8_INVALID) {
        op->type = OP_REG8;
        op->r8 = r8;
        free(copy);
        return 0;
    }

    Reg16 r16 = parse_reg16_name(p);
    if (r16 != R16_INVALID) {
        op->type = OP_REG16;
        op->r16 = r16;
        free(copy);
        return 0;
    }

    Cond cond = parse_cond_name(p);
    if (cond != COND_INVALID) {
        op->type = OP_COND;
        op->cond = cond;
        free(copy);
        return 0;
    }

    if (is_wrapped_by_parens(p)) {
        size_t n = strlen(p);
        char *inner_alloc = xstrndup2(p + 1, n - 2);
        char *inner = trim(inner_alloc);
        Reg16 mem_reg = parse_reg16_name(inner);
        if (mem_reg == R16_BC || mem_reg == R16_DE || mem_reg == R16_HL || mem_reg == R16_SP) {
            op->type = OP_MEM_REG;
            op->r16 = mem_reg;
            free(inner_alloc);
            free(copy);
            return 0;
        }
        if (eq_ci(inner, "C")) {
            op->type = OP_MEM_C;
            free(inner_alloc);
            free(copy);
            return 0;
        }
        Reg16 idx = R16_INVALID;
        char *expr = NULL;
        bool explicit_disp = false;
        if (parse_index_inner(inner, &idx, &expr, &explicit_disp)) {
            op->type = OP_MEM_INDEX;
            op->index_reg = idx;
            op->expr = expr;
            op->explicit_disp = explicit_disp;
            free(inner_alloc);
            free(copy);
            return 0;
        }
        op->type = OP_MEM_IMM;
        op->expr = xstrdup(inner);
        free(inner_alloc);
        free(copy);
        return 0;
    }

    Reg16 sdcc_idx = R16_INVALID;
    char *sdcc_expr = NULL;
    if (parse_sdcc_index_operand(p, &sdcc_idx, &sdcc_expr)) {
        op->type = OP_MEM_INDEX;
        op->index_reg = sdcc_idx;
        op->expr = sdcc_expr;
        op->explicit_disp = true;
        free(copy);
        return 0;
    }

    op->type = OP_IMM;
    op->expr = xstrdup(p);
    free(copy);
    return 0;
}

static int parse_operands(Assembler *asmr, const SourceLine *line, const char *text,
                          Operand **ops_out, size_t *n_out) {
    *ops_out = NULL;
    *n_out = 0;
    char *copy = xstrdup(text);
    char *t = trim(copy);
    if (*t == '\0') {
        free(copy);
        return 0;
    }
    StringList list;
    if (split_list(asmr, line, t, &list) != 0) {
        free(copy);
        return -1;
    }
    Operand *ops = xmalloc(list.len * sizeof(Operand));
    for (size_t i = 0; i < list.len; i++) {
        if (parse_operand(list.items[i], &ops[i]) != 0) {
            free_operands(ops, i);
            free(ops);
            free_string_list(&list);
            free(copy);
            return -1;
        }
    }
    *ops_out = ops;
    *n_out = list.len;
    free_string_list(&list);
    free(copy);
    return 0;
}

typedef struct {
    const char *p;
    Assembler *asmr;
    const SourceLine *line;
    bool allow_undefined;
} ExprParser;

static void expr_skip_ws(ExprParser *ps) {
    while (isspace((unsigned char)*ps->p)) {
        ps->p++;
    }
}

static int parse_expr_value(ExprParser *ps, int64_t *out);

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_escape_char(ExprParser *ps, int64_t *out) {
    char c = *ps->p++;
    switch (c) {
    case 'n': *out = '\n'; return 0;
    case 'r': *out = '\r'; return 0;
    case 't': *out = '\t'; return 0;
    case '0': *out = '\0'; return 0;
    case '\\': *out = '\\'; return 0;
    case '\'': *out = '\''; return 0;
    case '"': *out = '"'; return 0;
    case 'x': {
        int hi = hex_value(ps->p[0]);
        int lo = hex_value(ps->p[1]);
        if (hi < 0 || lo < 0) {
            return set_error(ps->asmr, ps->line, "invalid hex escape in character literal");
        }
        ps->p += 2;
        *out = hi * 16 + lo;
        return 0;
    }
    default:
        *out = (unsigned char)c;
        return 0;
    }
}

static int parse_char_literal(ExprParser *ps, int64_t *out) {
    ps->p++;
    if (*ps->p == '\0') {
        return set_error(ps->asmr, ps->line, "unterminated character literal");
    }
    int64_t value = 0;
    if (*ps->p == '\\') {
        ps->p++;
        if (parse_escape_char(ps, &value) != 0) {
            return -1;
        }
    } else {
        value = (unsigned char)*ps->p++;
    }
    if (*ps->p != '\'') {
        return set_error(ps->asmr, ps->line, "character literal must contain one byte");
    }
    ps->p++;
    *out = value;
    return 0;
}

static int parse_number_literal(ExprParser *ps, int64_t *out) {
    const char *start = ps->p;
    int base = 10;
    if (ps->p[0] == '0' && (ps->p[1] == 'x' || ps->p[1] == 'X')) {
        ps->p += 2;
        if (hex_value(*ps->p) < 0) {
            return set_error(ps->asmr, ps->line, "invalid hexadecimal number");
        }
        int64_t value = 0;
        while (hex_value(*ps->p) >= 0) {
            value = value * 16 + hex_value(*ps->p++);
        }
        *out = value;
        return 0;
    }
    if (*ps->p == '$') {
        ps->p++;
        if (hex_value(*ps->p) < 0) {
            return set_error(ps->asmr, ps->line, "invalid hexadecimal number");
        }
        int64_t value = 0;
        while (hex_value(*ps->p) >= 0) {
            value = value * 16 + hex_value(*ps->p++);
        }
        *out = value;
        return 0;
    }

    const char *q = ps->p;
    bool hex_suffix_possible = false;
    while (hex_value(*q) >= 0) {
        if (isalpha((unsigned char)*q)) {
            hex_suffix_possible = true;
        }
        q++;
    }
    if ((q > ps->p) && (*q == 'h' || *q == 'H')) {
        base = 16;
        q++;
    } else {
        q = ps->p;
        while (isdigit((unsigned char)*q)) {
            q++;
        }
        hex_suffix_possible = false;
    }
    if (q == ps->p) {
        return set_error(ps->asmr, ps->line, "invalid number");
    }
    int64_t value = 0;
    const char *end = q;
    if (base == 16) {
        end = q - 1;
    }
    for (const char *p = ps->p; p < end; p++) {
        int d = base == 16 ? hex_value(*p) : (*p - '0');
        if (d < 0 || d >= base) {
            return set_error(ps->asmr, ps->line, "invalid digit in number");
        }
        value = value * base + d;
    }
    ps->p = q;
    if (hex_suffix_possible && base != 16) {
        ps->p = start;
        return set_error(ps->asmr, ps->line, "hexadecimal suffix requires trailing H");
    }
    *out = value;
    return 0;
}

static int parse_primary(ExprParser *ps, int64_t *out) {
    expr_skip_ws(ps);
    const char *symbol_end = symbol_token_end(ps->p);
    if (symbol_end && is_sdcc_numeric_local_token(ps->p, symbol_end)) {
        char *name = xstrndup2(ps->p, (size_t)(symbol_end - ps->p));
        ps->p = symbol_end;
        char *full_name = qualify_symbol_name(ps->asmr, name);
        Symbol *sym = find_symbol(&ps->asmr->symbols, full_name);
        if (!sym) {
            int rc = ps->allow_undefined ? 0 :
                     set_error(ps->asmr, ps->line, "undefined symbol '%s'", name);
            free(full_name);
            free(name);
            *out = 0;
            return rc;
        }
        *out = sym->value;
        free(full_name);
        free(name);
        return 0;
    }
    if (*ps->p == '(') {
        ps->p++;
        if (parse_expr_value(ps, out) != 0) {
            return -1;
        }
        expr_skip_ws(ps);
        if (*ps->p != ')') {
            return set_error(ps->asmr, ps->line, "expected ')' in expression");
        }
        ps->p++;
        return 0;
    }
    if (*ps->p == '\'') {
        return parse_char_literal(ps, out);
    }
    if (*ps->p == '%') {
        ps->p++;
        if (*ps->p != '0' && *ps->p != '1') {
            return set_error(ps->asmr, ps->line, "invalid binary number");
        }
        int64_t value = 0;
        while (*ps->p == '0' || *ps->p == '1') {
            value = value * 2 + (*ps->p++ - '0');
        }
        *out = value;
        return 0;
    }
    if (isdigit((unsigned char)*ps->p) || *ps->p == '$') {
        return parse_number_literal(ps, out);
    }
    if (symbol_end) {
        const char *start = ps->p;
        ps->p = symbol_end;
        char *name = xstrndup2(start, (size_t)(symbol_end - start));
        char *full_name = qualify_symbol_name(ps->asmr, name);
        Symbol *sym = find_symbol(&ps->asmr->symbols, full_name);
        if (!sym) {
            int rc = ps->allow_undefined ? 0 :
                     set_error(ps->asmr, ps->line, "undefined symbol '%s'", name);
            free(full_name);
            free(name);
            *out = 0;
            return rc;
        }
        *out = sym->value;
        free(full_name);
        free(name);
        return 0;
    }
    return set_error(ps->asmr, ps->line, "expected expression");
}

static int parse_unary(ExprParser *ps, int64_t *out) {
    expr_skip_ws(ps);
    if (*ps->p == '#') {
        ps->p++;
        return parse_unary(ps, out);
    }
    if (*ps->p == '+') {
        ps->p++;
        return parse_unary(ps, out);
    }
    if (*ps->p == '-') {
        ps->p++;
        if (parse_unary(ps, out) != 0) {
            return -1;
        }
        *out = -*out;
        return 0;
    }
    if (*ps->p == '<') {
        ps->p++;
        if (parse_unary(ps, out) != 0) {
            return -1;
        }
        *out &= 0xFF;
        return 0;
    }
    if (*ps->p == '>') {
        ps->p++;
        if (parse_unary(ps, out) != 0) {
            return -1;
        }
        *out = (*out >> 8) & 0xFF;
        return 0;
    }
    return parse_primary(ps, out);
}

static int parse_expr_value(ExprParser *ps, int64_t *out) {
    if (parse_unary(ps, out) != 0) {
        return -1;
    }
    for (;;) {
        expr_skip_ws(ps);
        if (*ps->p != '+' && *ps->p != '-') {
            break;
        }
        char op = *ps->p++;
        int64_t rhs = 0;
        if (parse_unary(ps, &rhs) != 0) {
            return -1;
        }
        if (op == '+') {
            *out += rhs;
        } else {
            *out -= rhs;
        }
    }
    return 0;
}

static int eval_expr(Assembler *asmr, const SourceLine *line, const char *expr,
                     bool allow_undefined, int64_t *out) {
    ExprParser ps;
    ps.p = expr;
    ps.asmr = asmr;
    ps.line = line;
    ps.allow_undefined = allow_undefined;
    if (parse_expr_value(&ps, out) != 0) {
        return -1;
    }
    expr_skip_ws(&ps);
    if (*ps.p != '\0') {
        return set_error(asmr, line, "unexpected text in expression: '%s'", ps.p);
    }
    return 0;
}

static int emit_expr_byte(EmitCtx *ctx, const char *expr) {
    int64_t value = 0;
    if (ctx->pass == 2) {
        if (eval_expr(ctx->asmr, ctx->line, expr, false, &value) != 0) {
            return -1;
        }
        if (value < -128 || value > 255) {
            return set_error(ctx->asmr, ctx->line, "byte value out of range: %lld",
                             (long long)value);
        }
    }
    return emit_byte(ctx, (unsigned char)(value & 0xFF));
}

static int emit_expr_word(EmitCtx *ctx, const char *expr) {
    int64_t value = 0;
    if (ctx->pass == 2) {
        if (eval_expr(ctx->asmr, ctx->line, expr, false, &value) != 0) {
            return -1;
        }
        if (value < -32768 || value > 65535) {
            return set_error(ctx->asmr, ctx->line, "word value out of range: %lld",
                             (long long)value);
        }
    }
    if (emit_byte(ctx, (unsigned char)(value & 0xFF)) != 0) {
        return -1;
    }
    return emit_byte(ctx, (unsigned char)((value >> 8) & 0xFF));
}

static int emit_expr_disp(EmitCtx *ctx, const char *expr) {
    int64_t value = 0;
    if (ctx->pass == 2) {
        if (eval_expr(ctx->asmr, ctx->line, expr, false, &value) != 0) {
            return -1;
        }
        if (value < -128 || value > 127) {
            return set_error(ctx->asmr, ctx->line, "IX/IY displacement out of range: %lld",
                             (long long)value);
        }
    }
    return emit_byte(ctx, (unsigned char)(value & 0xFF));
}

static int emit_relative(EmitCtx *ctx, const char *expr, int64_t start_pc, int inst_size) {
    int64_t value = 0;
    if (ctx->pass == 2) {
        if (eval_expr(ctx->asmr, ctx->line, expr, false, &value) != 0) {
            return -1;
        }
        int64_t off = value - (start_pc + inst_size);
        if (off < -128 || off > 127) {
            return set_error(ctx->asmr, ctx->line, "relative branch out of range: %lld",
                             (long long)off);
        }
        value = off;
    }
    return emit_byte(ctx, (unsigned char)(value & 0xFF));
}

static int eval_small_int(EmitCtx *ctx, const char *expr, int64_t min, int64_t max,
                          const char *what, int64_t *out) {
    int64_t value = 0;
    if (ctx->pass == 2) {
        if (eval_expr(ctx->asmr, ctx->line, expr, false, &value) != 0) {
            return -1;
        }
        if (value < min || value > max) {
            return set_error(ctx->asmr, ctx->line, "%s out of range: %lld",
                             what, (long long)value);
        }
    }
    *out = value;
    return 0;
}

static bool is_normal_r8(const Operand *op, int *code) {
    if (op->type != OP_REG8) {
        return false;
    }
    if (op->r8 == R8_B || op->r8 == R8_C || op->r8 == R8_D || op->r8 == R8_E ||
        op->r8 == R8_H || op->r8 == R8_L || op->r8 == R8_A) {
        if (code) {
            *code = (int)op->r8;
        }
        return true;
    }
    return false;
}

static bool is_reg16(const Operand *op, Reg16 reg) {
    return op->type == OP_REG16 && op->r16 == reg;
}

static bool is_mem_reg(const Operand *op, Reg16 reg) {
    return op->type == OP_MEM_REG && op->r16 == reg;
}

static bool is_index_mem(const Operand *op, Reg16 reg) {
    return op->type == OP_MEM_INDEX && op->index_reg == reg;
}

static bool is_imm(const Operand *op) {
    return op->type == OP_IMM;
}

static bool cond_from_operand(const Operand *op, Cond *cond) {
    if (op->type == OP_COND) {
        *cond = op->cond;
        return true;
    }
    if (op->type == OP_REG8 && op->r8 == R8_C) {
        *cond = COND_C;
        return true;
    }
    return false;
}

static bool rp_code_hl(const Operand *op, int *code) {
    if (op->type != OP_REG16) return false;
    if (op->r16 == R16_BC || op->r16 == R16_DE || op->r16 == R16_HL || op->r16 == R16_SP) {
        *code = (int)op->r16;
        return true;
    }
    return false;
}

static bool rp_code_ixiy(const Operand *op, Reg16 index, int *code) {
    if (op->type != OP_REG16) return false;
    if (op->r16 == R16_BC) { *code = 0; return true; }
    if (op->r16 == R16_DE) { *code = 1; return true; }
    if (op->r16 == index) { *code = 2; return true; }
    if (op->r16 == R16_SP) { *code = 3; return true; }
    return false;
}

static int emit_prefix_index(EmitCtx *ctx, const Operand *op) {
    return emit_byte(ctx, op->index_reg == R16_IX ? 0xDD : 0xFD);
}

static int emit_indexed_op(EmitCtx *ctx, const Operand *op, unsigned char opcode) {
    if (emit_prefix_index(ctx, op) != 0) return -1;
    if (emit_byte(ctx, opcode) != 0) return -1;
    return emit_expr_disp(ctx, op->expr);
}

static int emit_indexed_cb(EmitCtx *ctx, const Operand *op, unsigned char opcode) {
    if (emit_prefix_index(ctx, op) != 0) return -1;
    if (emit_byte(ctx, 0xCB) != 0) return -1;
    if (emit_expr_disp(ctx, op->expr) != 0) return -1;
    return emit_byte(ctx, opcode);
}

static int invalid_operands(EmitCtx *ctx, const char *mnemonic) {
    return set_error(ctx->asmr, ctx->line, "invalid operands for %s", mnemonic);
}

static int encode_accum(EmitCtx *ctx, const char *mnemonic, const Operand *op,
                        unsigned char reg_base, unsigned char imm_op, unsigned char hl_op,
                        unsigned char ixiy_op) {
    int r = 0;
    if (is_normal_r8(op, &r)) {
        return emit_byte(ctx, (unsigned char)(reg_base + r));
    }
    if (is_mem_reg(op, R16_HL)) {
        return emit_byte(ctx, hl_op);
    }
    if (is_index_mem(op, R16_IX) || is_index_mem(op, R16_IY)) {
        return emit_indexed_op(ctx, op, ixiy_op);
    }
    if (is_imm(op)) {
        if (emit_byte(ctx, imm_op) != 0) return -1;
        return emit_expr_byte(ctx, op->expr);
    }
    return invalid_operands(ctx, mnemonic);
}

static int encode_accum_optional_a(EmitCtx *ctx, const char *mnemonic, const Operand *ops,
                                   size_t n, unsigned char reg_base, unsigned char imm_op,
                                   unsigned char hl_op, unsigned char ixiy_op) {
    if (n == 1) {
        return encode_accum(ctx, mnemonic, &ops[0], reg_base, imm_op, hl_op, ixiy_op);
    }
    if (n == 2 && ops[0].type == OP_REG8 && ops[0].r8 == R8_A) {
        return encode_accum(ctx, mnemonic, &ops[1], reg_base, imm_op, hl_op, ixiy_op);
    }
    return invalid_operands(ctx, mnemonic);
}

static int encode_ld(EmitCtx *ctx, const Operand *ops, size_t n) {
    if (n != 2) {
        return invalid_operands(ctx, "LD");
    }
    const Operand *dst = &ops[0];
    const Operand *src = &ops[1];
    int rdst = 0, rsrc = 0, rp = 0;

    if (dst->type == OP_REG8 && dst->r8 == R8_I && is_reg16(src, R16_AF)) {
        return invalid_operands(ctx, "LD");
    }

    if (dst->type == OP_REG8 && dst->r8 == R8_A) {
        if (src->type == OP_REG8 && src->r8 == R8_I) {
            return emit_byte(ctx, 0xED) || emit_byte(ctx, 0x57);
        }
        if (src->type == OP_REG8 && src->r8 == R8_R) {
            return emit_byte(ctx, 0xED) || emit_byte(ctx, 0x5F);
        }
        if (is_mem_reg(src, R16_BC)) return emit_byte(ctx, 0x0A);
        if (is_mem_reg(src, R16_DE)) return emit_byte(ctx, 0x1A);
        if (src->type == OP_MEM_IMM) {
            if (emit_byte(ctx, 0x3A) != 0) return -1;
            return emit_expr_word(ctx, src->expr);
        }
    }
    if (dst->type == OP_REG8 && dst->r8 == R8_I && src->type == OP_REG8 && src->r8 == R8_A) {
        return emit_byte(ctx, 0xED) || emit_byte(ctx, 0x47);
    }
    if (dst->type == OP_REG8 && dst->r8 == R8_R && src->type == OP_REG8 && src->r8 == R8_A) {
        return emit_byte(ctx, 0xED) || emit_byte(ctx, 0x4F);
    }

    if (is_normal_r8(dst, &rdst)) {
        if (is_normal_r8(src, &rsrc)) {
            return emit_byte(ctx, (unsigned char)(0x40 + rdst * 8 + rsrc));
        }
        if (is_mem_reg(src, R16_HL)) {
            return emit_byte(ctx, (unsigned char)(0x46 + rdst * 8));
        }
        if (is_index_mem(src, R16_IX) || is_index_mem(src, R16_IY)) {
            if (emit_prefix_index(ctx, src) != 0) return -1;
            if (emit_byte(ctx, (unsigned char)(0x46 + rdst * 8)) != 0) return -1;
            return emit_expr_disp(ctx, src->expr);
        }
        if (is_imm(src)) {
            if (emit_byte(ctx, (unsigned char)(0x06 + rdst * 8)) != 0) return -1;
            return emit_expr_byte(ctx, src->expr);
        }
    }

    if (is_mem_reg(dst, R16_BC) && src->type == OP_REG8 && src->r8 == R8_A) {
        return emit_byte(ctx, 0x02);
    }
    if (is_mem_reg(dst, R16_DE) && src->type == OP_REG8 && src->r8 == R8_A) {
        return emit_byte(ctx, 0x12);
    }
    if (is_mem_reg(dst, R16_HL)) {
        if (is_normal_r8(src, &rsrc)) {
            return emit_byte(ctx, (unsigned char)(0x70 + rsrc));
        }
        if (is_imm(src)) {
            if (emit_byte(ctx, 0x36) != 0) return -1;
            return emit_expr_byte(ctx, src->expr);
        }
    }
    if (is_index_mem(dst, R16_IX) || is_index_mem(dst, R16_IY)) {
        if (is_normal_r8(src, &rsrc)) {
            if (emit_prefix_index(ctx, dst) != 0) return -1;
            if (emit_byte(ctx, (unsigned char)(0x70 + rsrc)) != 0) return -1;
            return emit_expr_disp(ctx, dst->expr);
        }
        if (is_imm(src)) {
            if (emit_prefix_index(ctx, dst) != 0) return -1;
            if (emit_byte(ctx, 0x36) != 0) return -1;
            if (emit_expr_disp(ctx, dst->expr) != 0) return -1;
            return emit_expr_byte(ctx, src->expr);
        }
    }

    if (dst->type == OP_MEM_IMM) {
        if (src->type == OP_REG8 && src->r8 == R8_A) {
            if (emit_byte(ctx, 0x32) != 0) return -1;
            return emit_expr_word(ctx, dst->expr);
        }
        if (src->type == OP_REG16) {
            switch (src->r16) {
            case R16_BC:
                if (emit_byte(ctx, 0xED) != 0 || emit_byte(ctx, 0x43) != 0) return -1;
                return emit_expr_word(ctx, dst->expr);
            case R16_DE:
                if (emit_byte(ctx, 0xED) != 0 || emit_byte(ctx, 0x53) != 0) return -1;
                return emit_expr_word(ctx, dst->expr);
            case R16_HL:
                if (emit_byte(ctx, 0x22) != 0) return -1;
                return emit_expr_word(ctx, dst->expr);
            case R16_IX:
                if (emit_byte(ctx, 0xDD) != 0 || emit_byte(ctx, 0x22) != 0) return -1;
                return emit_expr_word(ctx, dst->expr);
            case R16_IY:
                if (emit_byte(ctx, 0xFD) != 0 || emit_byte(ctx, 0x22) != 0) return -1;
                return emit_expr_word(ctx, dst->expr);
            case R16_SP:
                if (emit_byte(ctx, 0xED) != 0 || emit_byte(ctx, 0x73) != 0) return -1;
                return emit_expr_word(ctx, dst->expr);
            default:
                break;
            }
        }
    }

    if (dst->type == OP_REG16 && src->type == OP_MEM_IMM) {
        switch (dst->r16) {
        case R16_BC:
            if (emit_byte(ctx, 0xED) != 0 || emit_byte(ctx, 0x4B) != 0) return -1;
            return emit_expr_word(ctx, src->expr);
        case R16_DE:
            if (emit_byte(ctx, 0xED) != 0 || emit_byte(ctx, 0x5B) != 0) return -1;
            return emit_expr_word(ctx, src->expr);
        case R16_HL:
            if (emit_byte(ctx, 0x2A) != 0) return -1;
            return emit_expr_word(ctx, src->expr);
        case R16_IX:
            if (emit_byte(ctx, 0xDD) != 0 || emit_byte(ctx, 0x2A) != 0) return -1;
            return emit_expr_word(ctx, src->expr);
        case R16_IY:
            if (emit_byte(ctx, 0xFD) != 0 || emit_byte(ctx, 0x2A) != 0) return -1;
            return emit_expr_word(ctx, src->expr);
        case R16_SP:
            if (emit_byte(ctx, 0xED) != 0 || emit_byte(ctx, 0x7B) != 0) return -1;
            return emit_expr_word(ctx, src->expr);
        default:
            break;
        }
    }

    if (dst->type == OP_REG16 && is_imm(src)) {
        if (rp_code_hl(dst, &rp)) {
            if (emit_byte(ctx, (unsigned char)(0x01 + rp * 0x10)) != 0) return -1;
            return emit_expr_word(ctx, src->expr);
        }
        if (dst->r16 == R16_IX || dst->r16 == R16_IY) {
            if (emit_byte(ctx, dst->r16 == R16_IX ? 0xDD : 0xFD) != 0) return -1;
            if (emit_byte(ctx, 0x21) != 0) return -1;
            return emit_expr_word(ctx, src->expr);
        }
    }

    if (is_reg16(dst, R16_SP)) {
        if (is_reg16(src, R16_HL)) return emit_byte(ctx, 0xF9);
        if (is_reg16(src, R16_IX)) return emit_byte(ctx, 0xDD) || emit_byte(ctx, 0xF9);
        if (is_reg16(src, R16_IY)) return emit_byte(ctx, 0xFD) || emit_byte(ctx, 0xF9);
    }

    return invalid_operands(ctx, "LD");
}

static int encode_add(EmitCtx *ctx, const Operand *ops, size_t n) {
    int rp = 0;
    if (n != 2) return invalid_operands(ctx, "ADD");
    if (ops[0].type == OP_REG8 && ops[0].r8 == R8_A) {
        return encode_accum(ctx, "ADD", &ops[1], 0x80, 0xC6, 0x86, 0x86);
    }
    if (is_reg16(&ops[0], R16_HL) && rp_code_hl(&ops[1], &rp)) {
        return emit_byte(ctx, (unsigned char)(0x09 + rp * 0x10));
    }
    if (is_reg16(&ops[0], R16_IX) && rp_code_ixiy(&ops[1], R16_IX, &rp)) {
        return emit_byte(ctx, 0xDD) || emit_byte(ctx, (unsigned char)(0x09 + rp * 0x10));
    }
    if (is_reg16(&ops[0], R16_IY) && rp_code_ixiy(&ops[1], R16_IY, &rp)) {
        return emit_byte(ctx, 0xFD) || emit_byte(ctx, (unsigned char)(0x09 + rp * 0x10));
    }
    return invalid_operands(ctx, "ADD");
}

static int encode_adc_sbc(EmitCtx *ctx, const char *mnemonic, const Operand *ops, size_t n,
                          bool is_adc) {
    int rp = 0;
    unsigned char reg_base = is_adc ? 0x88 : 0x98;
    unsigned char imm_op = is_adc ? 0xCE : 0xDE;
    unsigned char hl_op = is_adc ? 0x8E : 0x9E;
    unsigned char ed_base = is_adc ? 0x4A : 0x42;
    if (n == 2 && ops[0].type == OP_REG8 && ops[0].r8 == R8_A) {
        return encode_accum(ctx, mnemonic, &ops[1], reg_base, imm_op, hl_op, hl_op);
    }
    if (!is_adc && n == 1) {
        return encode_accum(ctx, mnemonic, &ops[0], reg_base, imm_op, hl_op, hl_op);
    }
    if (n == 2 && is_reg16(&ops[0], R16_HL) && rp_code_hl(&ops[1], &rp)) {
        return emit_byte(ctx, 0xED) || emit_byte(ctx, (unsigned char)(ed_base + rp * 0x10));
    }
    return invalid_operands(ctx, mnemonic);
}

static int encode_inc_dec(EmitCtx *ctx, const char *mnemonic, const Operand *ops, size_t n,
                          bool is_inc) {
    if (n != 1) return invalid_operands(ctx, mnemonic);
    int r = 0, rp = 0;
    if (is_normal_r8(&ops[0], &r)) {
        return emit_byte(ctx, (unsigned char)((is_inc ? 0x04 : 0x05) + r * 8));
    }
    if (rp_code_hl(&ops[0], &rp)) {
        return emit_byte(ctx, (unsigned char)((is_inc ? 0x03 : 0x0B) + rp * 0x10));
    }
    if (is_reg16(&ops[0], R16_IX)) return emit_byte(ctx, 0xDD) || emit_byte(ctx, is_inc ? 0x23 : 0x2B);
    if (is_reg16(&ops[0], R16_IY)) return emit_byte(ctx, 0xFD) || emit_byte(ctx, is_inc ? 0x23 : 0x2B);
    if (is_mem_reg(&ops[0], R16_HL)) return emit_byte(ctx, is_inc ? 0x34 : 0x35);
    if (is_index_mem(&ops[0], R16_IX) || is_index_mem(&ops[0], R16_IY)) {
        return emit_indexed_op(ctx, &ops[0], is_inc ? 0x34 : 0x35);
    }
    return invalid_operands(ctx, mnemonic);
}

static int encode_jp(EmitCtx *ctx, const Operand *ops, size_t n) {
    if (n == 1) {
        if (is_mem_reg(&ops[0], R16_HL)) return emit_byte(ctx, 0xE9);
        if (is_index_mem(&ops[0], R16_IX) && !ops[0].explicit_disp) {
            return emit_byte(ctx, 0xDD) || emit_byte(ctx, 0xE9);
        }
        if (is_index_mem(&ops[0], R16_IY) && !ops[0].explicit_disp) {
            return emit_byte(ctx, 0xFD) || emit_byte(ctx, 0xE9);
        }
        if (is_imm(&ops[0])) {
            if (emit_byte(ctx, 0xC3) != 0) return -1;
            return emit_expr_word(ctx, ops[0].expr);
        }
    }
    if (n == 2) {
        Cond cond;
        if (cond_from_operand(&ops[0], &cond) && is_imm(&ops[1])) {
            if (emit_byte(ctx, (unsigned char)(0xC2 + (int)cond * 8)) != 0) return -1;
            return emit_expr_word(ctx, ops[1].expr);
        }
    }
    return invalid_operands(ctx, "JP");
}

static int encode_call(EmitCtx *ctx, const Operand *ops, size_t n) {
    if (n == 1 && is_imm(&ops[0])) {
        if (emit_byte(ctx, 0xCD) != 0) return -1;
        return emit_expr_word(ctx, ops[0].expr);
    }
    if (n == 2 && is_imm(&ops[1])) {
        Cond cond;
        if (cond_from_operand(&ops[0], &cond)) {
            if (emit_byte(ctx, (unsigned char)(0xC4 + (int)cond * 8)) != 0) return -1;
            return emit_expr_word(ctx, ops[1].expr);
        }
    }
    return invalid_operands(ctx, "CALL");
}

static int encode_ret(EmitCtx *ctx, const Operand *ops, size_t n) {
    if (n == 0) return emit_byte(ctx, 0xC9);
    if (n == 1) {
        Cond cond;
        if (cond_from_operand(&ops[0], &cond)) {
            return emit_byte(ctx, (unsigned char)(0xC0 + (int)cond * 8));
        }
    }
    return invalid_operands(ctx, "RET");
}

static int encode_jr(EmitCtx *ctx, const Operand *ops, size_t n) {
    int64_t start = *ctx->pc;
    if (n == 1 && is_imm(&ops[0])) {
        if (emit_byte(ctx, 0x18) != 0) return -1;
        return emit_relative(ctx, ops[0].expr, start, 2);
    }
    if (n == 2 && is_imm(&ops[1])) {
        Cond cond;
        if (cond_from_operand(&ops[0], &cond) &&
            (cond == COND_NZ || cond == COND_Z || cond == COND_NC || cond == COND_C)) {
            if (emit_byte(ctx, (unsigned char)(0x20 + (int)cond * 8)) != 0) return -1;
            return emit_relative(ctx, ops[1].expr, start, 2);
        }
    }
    return invalid_operands(ctx, "JR");
}

static int encode_djnz(EmitCtx *ctx, const Operand *ops, size_t n) {
    int64_t start = *ctx->pc;
    if (n != 1 || !is_imm(&ops[0])) return invalid_operands(ctx, "DJNZ");
    if (emit_byte(ctx, 0x10) != 0) return -1;
    return emit_relative(ctx, ops[0].expr, start, 2);
}

static int encode_push_pop(EmitCtx *ctx, const char *mnemonic, const Operand *ops, size_t n,
                           bool is_push) {
    if (n != 1 || ops[0].type != OP_REG16) return invalid_operands(ctx, mnemonic);
    switch (ops[0].r16) {
    case R16_BC: return emit_byte(ctx, is_push ? 0xC5 : 0xC1);
    case R16_DE: return emit_byte(ctx, is_push ? 0xD5 : 0xD1);
    case R16_HL: return emit_byte(ctx, is_push ? 0xE5 : 0xE1);
    case R16_AF: return emit_byte(ctx, is_push ? 0xF5 : 0xF1);
    case R16_IX: return emit_byte(ctx, 0xDD) || emit_byte(ctx, is_push ? 0xE5 : 0xE1);
    case R16_IY: return emit_byte(ctx, 0xFD) || emit_byte(ctx, is_push ? 0xE5 : 0xE1);
    default: break;
    }
    return invalid_operands(ctx, mnemonic);
}

static int encode_bit_res_set(EmitCtx *ctx, const char *mnemonic, const Operand *ops, size_t n,
                              unsigned char reg_base, unsigned char mem_base) {
    if (n != 2 || !is_imm(&ops[0])) return invalid_operands(ctx, mnemonic);
    int64_t bit = 0;
    if (eval_small_int(ctx, ops[0].expr, 0, 7, "bit number", &bit) != 0) return -1;
    int r = 0;
    if (is_normal_r8(&ops[1], &r)) {
        return emit_byte(ctx, 0xCB) ||
               emit_byte(ctx, (unsigned char)(reg_base + bit * 8 + r));
    }
    if (is_mem_reg(&ops[1], R16_HL)) {
        return emit_byte(ctx, 0xCB) ||
               emit_byte(ctx, (unsigned char)(mem_base + bit * 8));
    }
    if (is_index_mem(&ops[1], R16_IX) || is_index_mem(&ops[1], R16_IY)) {
        return emit_indexed_cb(ctx, &ops[1], (unsigned char)(mem_base + bit * 8));
    }
    return invalid_operands(ctx, mnemonic);
}

static int encode_shift(EmitCtx *ctx, const char *mnemonic, const Operand *ops, size_t n,
                        unsigned char base) {
    if (n != 1) return invalid_operands(ctx, mnemonic);
    int r = 0;
    if (is_normal_r8(&ops[0], &r)) {
        return emit_byte(ctx, 0xCB) || emit_byte(ctx, (unsigned char)(base + r));
    }
    if (is_mem_reg(&ops[0], R16_HL)) {
        return emit_byte(ctx, 0xCB) || emit_byte(ctx, (unsigned char)(base + 6));
    }
    if (is_index_mem(&ops[0], R16_IX) || is_index_mem(&ops[0], R16_IY)) {
        return emit_indexed_cb(ctx, &ops[0], (unsigned char)(base + 6));
    }
    return invalid_operands(ctx, mnemonic);
}

static int encode_in(EmitCtx *ctx, const Operand *ops, size_t n) {
    int r = 0;
    if (n != 2) return invalid_operands(ctx, "IN");
    if (is_normal_r8(&ops[0], &r) && ops[1].type == OP_MEM_C) {
        return emit_byte(ctx, 0xED) || emit_byte(ctx, (unsigned char)(0x40 + r * 8));
    }
    if (ops[0].type == OP_REG8 && ops[0].r8 == R8_A && ops[1].type == OP_MEM_IMM) {
        if (emit_byte(ctx, 0xDB) != 0) return -1;
        return emit_expr_byte(ctx, ops[1].expr);
    }
    return invalid_operands(ctx, "IN");
}

static int encode_out(EmitCtx *ctx, const Operand *ops, size_t n) {
    int r = 0;
    if (n != 2) return invalid_operands(ctx, "OUT");
    if (ops[0].type == OP_MEM_C && is_normal_r8(&ops[1], &r)) {
        return emit_byte(ctx, 0xED) || emit_byte(ctx, (unsigned char)(0x41 + r * 8));
    }
    if (ops[0].type == OP_MEM_IMM && ops[1].type == OP_REG8 && ops[1].r8 == R8_A) {
        if (emit_byte(ctx, 0xD3) != 0) return -1;
        return emit_expr_byte(ctx, ops[0].expr);
    }
    return invalid_operands(ctx, "OUT");
}

static int encode_ex(EmitCtx *ctx, const Operand *ops, size_t n) {
    if (n != 2) return invalid_operands(ctx, "EX");
    if (ops[0].type == OP_REG16 && ops[0].r16 == R16_AF && ops[1].type == OP_AF_ALT) {
        return emit_byte(ctx, 0x08);
    }
    if (is_mem_reg(&ops[0], R16_SP)) {
        if (is_reg16(&ops[1], R16_HL)) return emit_byte(ctx, 0xE3);
        if (is_reg16(&ops[1], R16_IX)) return emit_byte(ctx, 0xDD) || emit_byte(ctx, 0xE3);
        if (is_reg16(&ops[1], R16_IY)) return emit_byte(ctx, 0xFD) || emit_byte(ctx, 0xE3);
    }
    if (is_reg16(&ops[0], R16_DE) && is_reg16(&ops[1], R16_HL)) return emit_byte(ctx, 0xEB);
    return invalid_operands(ctx, "EX");
}

static int encode_im(EmitCtx *ctx, const Operand *ops, size_t n) {
    if (n != 1 || !is_imm(&ops[0])) return invalid_operands(ctx, "IM");
    int64_t mode = 0;
    if (eval_small_int(ctx, ops[0].expr, 0, 2, "interrupt mode", &mode) != 0) return -1;
    unsigned char op = mode == 0 ? 0x46 : (mode == 1 ? 0x56 : 0x5E);
    return emit_byte(ctx, 0xED) || emit_byte(ctx, op);
}

static int encode_rst(EmitCtx *ctx, const Operand *ops, size_t n) {
    if (n != 1 || !is_imm(&ops[0])) return invalid_operands(ctx, "RST");
    int64_t value = 0;
    if (eval_small_int(ctx, ops[0].expr, 0, 0x38, "RST target", &value) != 0) return -1;
    if (ctx->pass == 2 && (value % 8) != 0) {
        return set_error(ctx->asmr, ctx->line, "RST target must be one of 0,8,...,38H");
    }
    return emit_byte(ctx, (unsigned char)(0xC7 + value));
}

static int encode_instruction(EmitCtx *ctx, const char *mnemonic, const Operand *ops, size_t n) {
    if (eq_ci(mnemonic, "LD")) return encode_ld(ctx, ops, n);
    if (eq_ci(mnemonic, "ADD")) return encode_add(ctx, ops, n);
    if (eq_ci(mnemonic, "ADC")) return encode_adc_sbc(ctx, "ADC", ops, n, true);
    if (eq_ci(mnemonic, "SBC")) return encode_adc_sbc(ctx, "SBC", ops, n, false);
    if (eq_ci(mnemonic, "SUB")) {
        return encode_accum_optional_a(ctx, "SUB", ops, n, 0x90, 0xD6, 0x96, 0x96);
    }
    if (eq_ci(mnemonic, "AND")) {
        return encode_accum_optional_a(ctx, "AND", ops, n, 0xA0, 0xE6, 0xA6, 0xA6);
    }
    if (eq_ci(mnemonic, "OR")) {
        return encode_accum_optional_a(ctx, "OR", ops, n, 0xB0, 0xF6, 0xB6, 0xB6);
    }
    if (eq_ci(mnemonic, "XOR")) {
        return encode_accum_optional_a(ctx, "XOR", ops, n, 0xA8, 0xEE, 0xAE, 0xAE);
    }
    if (eq_ci(mnemonic, "CP")) {
        return encode_accum_optional_a(ctx, "CP", ops, n, 0xB8, 0xFE, 0xBE, 0xBE);
    }
    if (eq_ci(mnemonic, "INC")) return encode_inc_dec(ctx, "INC", ops, n, true);
    if (eq_ci(mnemonic, "DEC")) return encode_inc_dec(ctx, "DEC", ops, n, false);
    if (eq_ci(mnemonic, "JP")) return encode_jp(ctx, ops, n);
    if (eq_ci(mnemonic, "CALL")) return encode_call(ctx, ops, n);
    if (eq_ci(mnemonic, "RET")) return encode_ret(ctx, ops, n);
    if (eq_ci(mnemonic, "JR")) return encode_jr(ctx, ops, n);
    if (eq_ci(mnemonic, "DJNZ")) return encode_djnz(ctx, ops, n);
    if (eq_ci(mnemonic, "PUSH")) return encode_push_pop(ctx, "PUSH", ops, n, true);
    if (eq_ci(mnemonic, "POP")) return encode_push_pop(ctx, "POP", ops, n, false);
    if (eq_ci(mnemonic, "BIT")) return encode_bit_res_set(ctx, "BIT", ops, n, 0x40, 0x46);
    if (eq_ci(mnemonic, "RES")) return encode_bit_res_set(ctx, "RES", ops, n, 0x80, 0x86);
    if (eq_ci(mnemonic, "SET")) return encode_bit_res_set(ctx, "SET", ops, n, 0xC0, 0xC6);
    if (eq_ci(mnemonic, "RLC")) return encode_shift(ctx, "RLC", ops, n, 0x00);
    if (eq_ci(mnemonic, "RRC")) return encode_shift(ctx, "RRC", ops, n, 0x08);
    if (eq_ci(mnemonic, "RL")) return encode_shift(ctx, "RL", ops, n, 0x10);
    if (eq_ci(mnemonic, "RR")) return encode_shift(ctx, "RR", ops, n, 0x18);
    if (eq_ci(mnemonic, "SLA")) return encode_shift(ctx, "SLA", ops, n, 0x20);
    if (eq_ci(mnemonic, "SRA")) return encode_shift(ctx, "SRA", ops, n, 0x28);
    if (eq_ci(mnemonic, "SLL")) return encode_shift(ctx, "SLL", ops, n, 0x30);
    if (eq_ci(mnemonic, "SRL")) return encode_shift(ctx, "SRL", ops, n, 0x38);
    if (eq_ci(mnemonic, "IN")) return encode_in(ctx, ops, n);
    if (eq_ci(mnemonic, "OUT")) return encode_out(ctx, ops, n);
    if (eq_ci(mnemonic, "EX")) return encode_ex(ctx, ops, n);
    if (eq_ci(mnemonic, "IM")) return encode_im(ctx, ops, n);
    if (eq_ci(mnemonic, "RST")) return encode_rst(ctx, ops, n);

    if (n != 0) {
        return set_error(ctx->asmr, ctx->line, "instruction %s takes no operands", mnemonic);
    }

    struct Fixed {
        const char *name;
        unsigned char bytes[2];
        int len;
    };
    static const struct Fixed fixed[] = {
        {"CCF", {0x3F, 0x00}, 1}, {"CPD", {0xED, 0xA9}, 2},
        {"CPDR", {0xED, 0xB9}, 2}, {"CPI", {0xED, 0xA1}, 2},
        {"CPIR", {0xED, 0xB1}, 2}, {"CPL", {0x2F, 0x00}, 1},
        {"DAA", {0x27, 0x00}, 1}, {"DI", {0xF3, 0x00}, 1},
        {"EI", {0xFB, 0x00}, 1}, {"EXX", {0xD9, 0x00}, 1},
        {"HALT", {0x76, 0x00}, 1}, {"IND", {0xED, 0xAA}, 2},
        {"INDR", {0xED, 0xBA}, 2}, {"INI", {0xED, 0xA2}, 2},
        {"INIR", {0xED, 0xB2}, 2}, {"LDD", {0xED, 0xA8}, 2},
        {"LDDR", {0xED, 0xB8}, 2}, {"LDI", {0xED, 0xA0}, 2},
        {"LDIR", {0xED, 0xB0}, 2}, {"NEG", {0xED, 0x44}, 2},
        {"NOP", {0x00, 0x00}, 1}, {"OTDR", {0xED, 0xBB}, 2},
        {"OTIR", {0xED, 0xB3}, 2}, {"OUTD", {0xED, 0xAB}, 2},
        {"OUTI", {0xED, 0xA3}, 2}, {"RETI", {0xED, 0x4D}, 2},
        {"RETN", {0xED, 0x45}, 2}, {"RLA", {0x17, 0x00}, 1},
        {"RLCA", {0x07, 0x00}, 1}, {"RLD", {0xED, 0x6F}, 2},
        {"RRA", {0x1F, 0x00}, 1}, {"RRCA", {0x0F, 0x00}, 1},
        {"RRD", {0xED, 0x67}, 2}, {"SCF", {0x37, 0x00}, 1},
    };
    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++) {
        if (eq_ci(mnemonic, fixed[i].name)) {
            for (int j = 0; j < fixed[i].len; j++) {
                if (emit_byte(ctx, fixed[i].bytes[j]) != 0) return -1;
            }
            return 0;
        }
    }

    return set_error(ctx->asmr, ctx->line, "unknown instruction '%s'", mnemonic);
}

static int decode_string_item(Assembler *asmr, const SourceLine *line, const char *item,
                              unsigned char **bytes_out, size_t *len_out) {
    *bytes_out = NULL;
    *len_out = 0;
    size_t n = strlen(item);
    if (n < 2 || (item[0] != '\'' && item[0] != '"') || item[n - 1] != item[0]) {
        return 1;
    }
    unsigned char *bytes = xmalloc(n);
    size_t len = 0;
    for (size_t i = 1; i < n - 1; i++) {
        unsigned char c = (unsigned char)item[i];
        if (c == '\\') {
            if (i + 1 >= n - 1) {
                free(bytes);
                return set_error(asmr, line, "unterminated escape in string");
            }
            char e = item[++i];
            switch (e) {
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case '0': c = '\0'; break;
            case '\\': c = '\\'; break;
            case '\'': c = '\''; break;
            case '"': c = '"'; break;
            case 'x': {
                if (i + 2 >= n - 1) {
                    free(bytes);
                    return set_error(asmr, line, "invalid hex escape in string");
                }
                int hi = hex_value(item[i + 1]);
                int lo = hex_value(item[i + 2]);
                if (hi < 0 || lo < 0) {
                    free(bytes);
                    return set_error(asmr, line, "invalid hex escape in string");
                }
                c = (unsigned char)(hi * 16 + lo);
                i += 2;
                break;
            }
            default:
                c = (unsigned char)e;
                break;
            }
        }
        bytes[len++] = c;
    }
    *bytes_out = bytes;
    *len_out = len;
    return 0;
}

static int handle_db(EmitCtx *ectx, const char *rest) {
    StringList list;
    if (split_list(ectx->asmr, ectx->line, rest, &list) != 0) {
        return -1;
    }
    for (size_t i = 0; i < list.len; i++) {
        unsigned char *bytes = NULL;
        size_t len = 0;
        int rc = decode_string_item(ectx->asmr, ectx->line, list.items[i], &bytes, &len);
        if (rc < 0) {
            free_string_list(&list);
            return -1;
        }
        if (rc == 0) {
            for (size_t j = 0; j < len; j++) {
                if (emit_byte(ectx, bytes[j]) != 0) {
                    free(bytes);
                    free_string_list(&list);
                    return -1;
                }
            }
            free(bytes);
        } else {
            if (emit_expr_byte(ectx, list.items[i]) != 0) {
                free_string_list(&list);
                return -1;
            }
        }
    }
    free_string_list(&list);
    return 0;
}

static int handle_ascii(EmitCtx *ectx, const char *rest, const char *name) {
    StringList list;
    if (split_list(ectx->asmr, ectx->line, rest, &list) != 0) {
        return -1;
    }
    for (size_t i = 0; i < list.len; i++) {
        unsigned char *bytes = NULL;
        size_t len = 0;
        int rc = decode_string_item(ectx->asmr, ectx->line, list.items[i], &bytes, &len);
        if (rc < 0) {
            free_string_list(&list);
            return -1;
        }
        if (rc != 0) {
            free_string_list(&list);
            return set_error(ectx->asmr, ectx->line, "%s expects string literal operands", name);
        }
        for (size_t j = 0; j < len; j++) {
            if (emit_byte(ectx, bytes[j]) != 0) {
                free(bytes);
                free_string_list(&list);
                return -1;
            }
        }
        free(bytes);
    }
    free_string_list(&list);
    return 0;
}

static int handle_dw(EmitCtx *ectx, const char *rest) {
    StringList list;
    if (split_list(ectx->asmr, ectx->line, rest, &list) != 0) {
        return -1;
    }
    for (size_t i = 0; i < list.len; i++) {
        if (emit_expr_word(ectx, list.items[i]) != 0) {
            free_string_list(&list);
            return -1;
        }
    }
    free_string_list(&list);
    return 0;
}

static int handle_ds(EmitCtx *ectx, const char *rest) {
    StringList list;
    if (split_list(ectx->asmr, ectx->line, rest, &list) != 0) {
        return -1;
    }
    if (list.len < 1 || list.len > 2) {
        free_string_list(&list);
        return set_error(ectx->asmr, ectx->line, "DS expects size or size,fill");
    }
    int64_t count = 0;
    if (eval_expr(ectx->asmr, ectx->line, list.items[0], false, &count) != 0) {
        free_string_list(&list);
        return -1;
    }
    if (count < 0 || count > INT_MAX) {
        free_string_list(&list);
        return set_error(ectx->asmr, ectx->line, "DS size out of range: %lld",
                         (long long)count);
    }
    int64_t fill = 0;
    if (list.len == 2 && eval_small_int(ectx, list.items[1], -128, 255, "DS fill", &fill) != 0) {
        free_string_list(&list);
        return -1;
    }
    for (int64_t i = 0; i < count; i++) {
        if (emit_byte(ectx, (unsigned char)(fill & 0xFF)) != 0) {
            free_string_list(&list);
            return -1;
        }
    }
    free_string_list(&list);
    return 0;
}

static int process_line(Assembler *asmr, const SourceLine *line, int pass,
                        int64_t *pc, Output *out, bool *org_seen) {
    char *clean = strip_comment(line->text);
    char *p = trim(clean);
    char **labels = NULL;
    size_t label_count = 0;
    size_t label_cap = 0;
    while (*p) {
        char *label = NULL;
        if (!parse_label_at_start(&p, &label)) {
            break;
        }
        if (label_count == label_cap) {
            label_cap = label_cap ? label_cap * 2 : 4;
            labels = xrealloc(labels, label_cap * sizeof(char *));
        }
        labels[label_count++] = label;
        p = trim(p);
    }
    if (*p == '\0') {
        if (define_label_list(asmr, line, labels, label_count, *pc, false, pass) != 0) {
            for (size_t i = 0; i < label_count; i++) free(labels[i]);
            free(labels);
            free(clean);
            return -1;
        }
        for (size_t i = 0; i < label_count; i++) free(labels[i]);
        free(labels);
        free(clean);
        return 0;
    }

    char *equ_name = NULL;
    char *equ_expr = NULL;
    if (label_count > 0 && starts_ci(p, "EQU") && (p[3] == '\0' || isspace((unsigned char)p[3]))) {
        char *expr = trim(p + 3);
        if (*expr == '\0') {
            for (size_t i = 0; i < label_count; i++) free(labels[i]);
            free(labels);
            free(clean);
            return set_error(asmr, line, "EQU requires an expression");
        }
        if (pass == 1) {
            int64_t value = 0;
            if (eval_expr(asmr, line, expr, false, &value) != 0 ||
                define_label_list(asmr, line, labels, label_count, value, true, pass) != 0) {
                for (size_t i = 0; i < label_count; i++) free(labels[i]);
                free(labels);
                free(clean);
                return -1;
            }
        } else if (define_label_list(asmr, line, labels, label_count, 0, true, pass) != 0) {
            for (size_t i = 0; i < label_count; i++) free(labels[i]);
            free(labels);
            free(clean);
            return -1;
        }
        for (size_t i = 0; i < label_count; i++) free(labels[i]);
        free(labels);
        free(clean);
        return 0;
    }
    if (parse_equ_line(p, &equ_name, &equ_expr)) {
        if (pass == 1) {
            int64_t value = 0;
            char *full_name = qualify_symbol_name(asmr, equ_name);
            if (eval_expr(asmr, line, equ_expr, false, &value) != 0 ||
                define_symbol(asmr, line, full_name, value, true) != 0) {
                free(full_name);
                free(equ_name);
                free(equ_expr);
                for (size_t i = 0; i < label_count; i++) free(labels[i]);
                free(labels);
                free(clean);
                return -1;
            }
            free(full_name);
        }
        if (!is_local_symbol_name(equ_name)) {
            set_current_scope(asmr, equ_name);
        }
        free(equ_name);
        free(equ_expr);
        for (size_t i = 0; i < label_count; i++) free(labels[i]);
        free(labels);
        free(clean);
        return 0;
    }

    if (define_label_list(asmr, line, labels, label_count, *pc, false, pass) != 0) {
        for (size_t i = 0; i < label_count; i++) free(labels[i]);
        free(labels);
        free(clean);
        return -1;
    }
    for (size_t i = 0; i < label_count; i++) free(labels[i]);
    free(labels);

    char *mnemonic = p;
    while (*p && !isspace((unsigned char)*p)) {
        p++;
    }
    if (*p) {
        *p++ = '\0';
    }
    char *rest = trim(p);
    const char *directive = mnemonic;
    if (*directive == '.') {
        ++directive;
    }

    if (eq_ci(directive, "MODULE") || eq_ci(directive, "OPTSDCC") ||
        eq_ci(directive, "GLOBL") || eq_ci(directive, "AREA")) {
        free(clean);
        return 0;
    }

    if (eq_ci(directive, "ORG") || eq_ci(directive, "FORG")) {
        if (*rest == '\0') {
            free(clean);
            return set_error(asmr, line, "%s requires an expression", mnemonic);
        }
        int64_t value = 0;
        if (eval_expr(asmr, line, rest, false, &value) != 0) {
            free(clean);
            return -1;
        }
        if (*org_seen && value < *pc) {
            free(clean);
            return set_error(asmr, line, "ORG moved backward from 0x%llX to 0x%llX",
                             (long long)*pc, (long long)value);
        }
        if (pass == 2 && output_set_org(out, asmr, line, *pc, value) != 0) {
            free(clean);
            return -1;
        }
        *pc = value;
        *org_seen = true;
        free(clean);
        return 0;
    }

    EmitCtx ectx;
    ectx.pass = pass;
    ectx.asmr = asmr;
    ectx.out = out;
    ectx.pc = pc;
    ectx.line = line;

    if (eq_ci(directive, "DB") || eq_ci(directive, "DEFB")) {
        if (*rest == '\0') {
            free(clean);
            return set_error(asmr, line, "%s requires at least one value", mnemonic);
        }
        int rc = handle_db(&ectx, rest);
        free(clean);
        return rc;
    }
    if (eq_ci(directive, "ASCII")) {
        if (*rest == '\0') {
            free(clean);
            return set_error(asmr, line, "%s requires at least one string", mnemonic);
        }
        int rc = handle_ascii(&ectx, rest, mnemonic);
        free(clean);
        return rc;
    }
    if (eq_ci(directive, "DW") || eq_ci(directive, "DEFW")) {
        if (*rest == '\0') {
            free(clean);
            return set_error(asmr, line, "%s requires at least one value", mnemonic);
        }
        int rc = handle_dw(&ectx, rest);
        free(clean);
        return rc;
    }
    if (eq_ci(directive, "DS") || eq_ci(directive, "DEFS")) {
        if (*rest == '\0') {
            free(clean);
            return set_error(asmr, line, "%s requires a size", mnemonic);
        }
        int rc = handle_ds(&ectx, rest);
        free(clean);
        return rc;
    }
    if (eq_ci(directive, "EQU")) {
        free(clean);
        return set_error(asmr, line, "EQU must be written as NAME EQU expression");
    }

    Operand *ops = NULL;
    size_t nops = 0;
    if (parse_operands(asmr, line, rest, &ops, &nops) != 0) {
        free(clean);
        return -1;
    }
    int rc = encode_instruction(&ectx, mnemonic, ops, nops);
    free_operands(ops, nops);
    free(ops);
    free(clean);
    return rc;
}

static int assemble_source(Assembler *asmr, const Source *src, Output *out) {
    int64_t pc = 0;
    bool org_seen = false;
    set_current_scope(asmr, NULL);
    for (size_t i = 0; i < src->count; i++) {
        if (process_line(asmr, &src->lines[i], 1, &pc, NULL, &org_seen) != 0) {
            return -1;
        }
    }
    pc = 0;
    org_seen = false;
    set_current_scope(asmr, NULL);
    for (size_t i = 0; i < src->count; i++) {
        if (process_line(asmr, &src->lines[i], 2, &pc, out, &org_seen) != 0) {
            return -1;
        }
    }
    if (!out->have_base) {
        out->base = 0;
        out->have_base = true;
    }
    return 0;
}

static char *default_output_path(const char *input) {
    const char *slash = strrchr(input, '/');
    const char *base = slash ? slash + 1 : input;
    const char *dot = strrchr(base, '.');
    if (!dot) {
        size_t n = strlen(input);
        char *out = xmalloc(n + 5);
        memcpy(out, input, n);
        memcpy(out + n, ".bin", 5);
        return out;
    }
    size_t prefix = (size_t)(dot - input);
    char *out = xmalloc(prefix + 5);
    memcpy(out, input, prefix);
    memcpy(out + prefix, ".bin", 5);
    return out;
}

static int write_output_file(const char *path, const Output *out) {
    size_t n = strlen(path);
    char *tmp = xmalloc(n + 5);
    memcpy(tmp, path, n);
    memcpy(tmp + n, ".tmp", 5);

    FILE *f = fopen(tmp, "wb");
    if (!f) {
        fprintf(stderr, "%s: %s\n", tmp, strerror(errno));
        free(tmp);
        return 1;
    }
    if (out->len > 0 && fwrite(out->data, 1, out->len, f) != out->len) {
        fprintf(stderr, "%s: write failed\n", tmp);
        fclose(f);
        remove(tmp);
        free(tmp);
        return 1;
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "%s: close failed\n", tmp);
        remove(tmp);
        free(tmp);
        return 1;
    }
    if (rename(tmp, path) != 0) {
        fprintf(stderr, "%s: rename to %s failed: %s\n", tmp, path, strerror(errno));
        remove(tmp);
        free(tmp);
        return 1;
    }
    free(tmp);
    return 0;
}

static void usage(FILE *f) {
    fprintf(f, "usage: z80-asm [-o output.bin] file.asm\n");
}

int main(int argc, char **argv) {
    const char *input = NULL;
    const char *output_arg = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc) {
                usage(stderr);
                return 1;
            }
            output_arg = argv[i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (!input) {
            input = argv[i];
        } else {
            usage(stderr);
            return 1;
        }
    }
    if (!input) {
        usage(stderr);
        return 1;
    }

    char *owned_output = NULL;
    const char *output = output_arg;
    if (!output) {
        owned_output = default_output_path(input);
        output = owned_output;
    }

    Source src = read_source_or_die(input);
    Assembler asmr;
    memset(&asmr, 0, sizeof(asmr));
    asmr.filename = input;
    Output out;
    memset(&out, 0, sizeof(out));

    int rc = 0;
    if (assemble_source(&asmr, &src, &out) != 0) {
        fprintf(stderr, "%s\n", asmr.error[0] ? asmr.error : "assembly failed");
        rc = 1;
    } else {
        rc = write_output_file(output, &out);
    }

    output_free(&out);
    free_symbols(&asmr.symbols);
    free(asmr.current_scope);
    free_source(&src);
    free(owned_output);
    return rc;
}
