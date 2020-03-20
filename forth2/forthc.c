#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXKBYTES 64
#define MAXLOCALS 16
#define MAXTOKENS 1024
#define SOURCE "scheme.4th"

#define TOPLEVEL (1 << 0)
#define INNER (1 << 1)
#define LOCKED (1 << 2)
#define USER (1 << 3)
//#define LOCAL (1 << 4)

#define TOK_EOF (1 << 0)
#define TOK_WORD (1 << 1)
#define TOK_QUOTED_WORD (1 << 2)
#define TOK_STRING (1 << 3)
#define TOK_CHAR (1 << 4)
#define TOK_INT (1 << 5)

struct vec {
    unsigned char *items;
    size_t itemsize;
    size_t cap;
    size_t len;
};

struct token {
    size_t tag;
    char *string;
    uintptr_t number; // number, character, or string-pool index
};

struct definition {
    const char *forth_word;
    char *c_func_name;
    void (*compile_func)(void);
    size_t tag;
};

struct local {
    const char *forth_var_name;
    char *c_var_name;
    char *c_getter_name;
    char *c_setter_name;
};

static const char ascii[] = "0123456789"
                            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                            "abcdefghijklmnopqrstuvwxyz";

static const char *mangle_one_char[] = { "=equal", "@fetch", "!store",
    "+plus", "-minus", "*star", "/slash", "?_p", 0 };
static const char *mangle_two_char[] = { "->to", 0 };
static char *manglepool[MAXTOKENS];
static size_t manglepool_count;
static char mangle_buf[64];

static struct definition definitions[MAXTOKENS];
static size_t definitions_count;

static struct local locals[MAXLOCALS];
static size_t locals_count;

static size_t tokens_pos;
static size_t tokens_count;
static struct token tokens[MAXTOKENS];
static struct token token_eof = { .tag = TOK_EOF };

static size_t source_mark;
static size_t source_pos;
static size_t source_len;
static char source[1024 * MAXKBYTES];

static void panic(const char *msg) __attribute__((__noreturn__));

static void panic(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(2);
}

static char *xstrdupspan(const char *start, const char *limit)
{
    char *dup;
    size_t len;

    len = (size_t)(limit - start);
    if (!(dup = calloc(1, len + 1))) {
        panic("out of memory");
    }
    memcpy(dup, start, len);
    return dup;
}

static char *copy_string(const char *str)
{
    char *dup;

    if (!(dup = calloc(1, strlen(str) + 1))) {
        panic("out of memory");
    }
    memcpy(dup, str, strlen(str) + 1);
    return dup;
}

static char *copy_two_strings(const char *a, const char *b)
{
    char *dup;
    size_t alen, blen;

    alen = strlen(a);
    blen = strlen(b);
    if (!(dup = calloc(1, alen + blen + 1))) {
        panic("out of memory");
    }
    memcpy(dup, a, alen);
    memcpy(dup + alen, b, blen);
    return dup;
}

static void *zeroalloc(size_t nbyte)
{
    void *p;

    if (!(p = calloc(1, nbyte))) {
        panic("out of memory");
    }
    return p;
}

static struct vec *vec_new(size_t itemsize)
{
    struct vec *vec = zeroalloc(sizeof(*vec));
    vec->itemsize = itemsize;
    return vec;
}

static void *vec_reserve(struct vec *vec, size_t nitem)
{
    if (vec->cap - vec->len < nitem) {
        if (!vec->cap) {
            vec->cap = 32;
        }
        while (vec->cap - vec->len < nitem) {
            vec->cap *= 2;
        }
        if (!(vec->items = realloc(vec->items, vec->cap * vec->itemsize))) {
            panic("out of memory");
        }
    }
    void *p = vec->items + vec->len * vec->itemsize;
    vec->len += nitem;
    return p;
}

static void vec_putb(struct vec *vec, const char *bytes, size_t n)
{
    memcpy(vec_reserve(vec, n), bytes, n);
}

static void vec_putc(struct vec *vec, char ch) { vec_putb(vec, &ch, 1); }

static void vec_puts(struct vec *vec, const char *str)
{
    vec_putb(vec, str, strlen(str));
}

static void write(const char *str) { printf("%s", str); }

static void writeln(const char *str) { printf("%s\n", str); }

static void write_unsigned(uintptr_t u) { printf("%" PRIuPTR "\n", u); }

static void slurp(void)
{
    FILE *input;

    if (!(input = fopen(SOURCE, "rb"))) {
        panic("cannot open " SOURCE);
    }
    source_len = fread(source, 1, sizeof(source), input);
    if (ferror(input)) {
        panic("cannot read from file");
    }
    if (source_len == sizeof(source)) {
        panic("source code is too long, add more KILOS");
    }
    if (memchr(source, 0, source_len)) {
        panic("source code contains null byte");
    }
    if (!feof(input)) {
        panic("eof not reached");
    }
    if (fclose(input) != 0) {
        panic("cannot close file");
    }
}

static int read_char_if(int (*predicate)(int))
{
    if (source_pos >= source_len) {
        return 0;
    }
    if (!predicate(source[source_pos])) {
        return 0;
    }
    return source[source_pos++];
}

static int read_the_char(int ch)
{
    if (source_pos >= source_len) {
        return 0;
    }
    if (source[source_pos] != ch) {
        return 0;
    }
    return source[source_pos++];
}

static int is_word_char(int ch)
{
    return (!isspace(ch)) && (ch != '"') && (ch != '#');
}

static int is_string_char(int ch) { return isprint(ch) && (ch != '"'); }

static int is_horizontal_char(int ch) { return ch != '\n'; }

static int token_is_word(struct token *tok, const char *word)
{
    if (tok->tag != TOK_WORD)
        return 0;
    return !strcmp(tok->string, word);
}

static struct token *allocate_token(size_t tag)
{
    struct token *tok;

    if (tokens_count >= MAXTOKENS) {
        panic("too many tokens");
    }
    tok = &tokens[tokens_count++];
    tok->tag = tag;
    return tok;
}

static void skip_whitespace(void)
{
    while (read_char_if(isspace))
        ;
}

static void skip_rest_of_line(void)
{
    while (read_char_if(is_horizontal_char))
        ;
}

static void read_string_token(void)
{
    struct token *tok;

    source_mark = source_pos;
    while (!read_the_char('"')) {
        if (read_char_if(is_string_char)) {
            ;
        } else {
            panic("Syntax error inside string");
        }
    }
    tok = allocate_token(TOK_STRING);
    tok->string = xstrdupspan(source + source_mark, source + source_pos - 1);
}

static int parse_number(const char *str, const char *limit, uintptr_t *out)
{
    const char digits[] = "0123456789abcdef";
    const char *digit;
    uintptr_t value;
    size_t base = 10;

    *out = value = 0;
    if (str[0] == '0') {
        if (str[1] == 'b') {
            base = 2;
            str += 2;
        } else if (str[1] == 'o') {
            base = 8;
            str += 2;
        } else if (str[1] == 'x') {
            base = 16;
            str += 2;
        }
    }
    while (str < limit) {
        if (!(digit = strchr(digits, *str++))) {
            return 0;
        }
        value *= base;
        value += (uintptr_t)(digit - digits);
    }
    *out = value;
    return 1;
}

static void read_word_token_or_panic(void)
{
    struct token *tok;

    source_mark = source_pos;
    while (read_char_if(is_word_char))
        ;
    if (source_mark == source_pos) {
        panic("Syntax error at top level");
    }
    tok = allocate_token(TOK_WORD);
    tok->string = xstrdupspan(source + source_mark, source + source_pos);
    if (parse_number(
            source + source_mark, source + source_pos, &tok->number)) {
        tok->tag = TOK_INT;
    }
}

static void tokenize(void)
{
    for (;;) {
        skip_whitespace();
        if (source_pos == source_len) {
            break;
        } else if (read_the_char('\\')) {
            skip_rest_of_line();
        } else if (read_the_char('"')) {
            read_string_token();
        } else {
            read_word_token_or_panic();
        }
    }
}

static struct token *read_token(size_t tag_bits)
{
    if (tokens_pos >= tokens_count) {
        if (tag_bits & TOK_EOF) {
            return &token_eof;
        } else {
            return 0;
        }
    }
    if (!(tokens[tokens_pos].tag & tag_bits)) {
        return 0;
    }
    return &tokens[tokens_pos++];
}

static struct token *read_the_word(const char *word)
{
    if (tokens_pos >= tokens_count) {
        return 0;
    }
    if (!token_is_word(&tokens[tokens_pos], word)) {
        return 0;
    }
    return &tokens[tokens_pos++];
}

static void mangle_clear(void) { memset(mangle_buf, 0, sizeof(mangle_buf)); }

static void mangle_putc(int ch)
{
    size_t n = strlen(mangle_buf);
    if (n == sizeof(mangle_buf) - 1) {
        panic("token too long");
    }
    mangle_buf[n] = (char)ch;
}

static void mangle_puts(const char *str)
{
    for (; *str; str++) {
        mangle_putc(*str);
    }
}

static void mangle_putu(uintptr_t u)
{
    char buf[32];
    char *p = buf + sizeof(buf);

    *--p = 0;
    do {
        *--p = '0' + u % 10;
    } while ((u /= 10) && (p > buf));
    mangle_puts(p);
}

static void mangle_range(const char *str, const char *limit)
{
    const char *entry;
    const char **entryp;

    for (; str < limit; str++) {
        for (entryp = mangle_two_char; (entry = *entryp); entryp++) {
            if (str < limit - 1) {
                if ((str[0] == entry[0]) && (str[1] == entry[1])) {
                    break;
                }
            }
        }
        if (entry) {
            mangle_puts(&entry[2]);
            continue;
        }
        for (entryp = mangle_one_char; (entry = *entryp); entryp++) {
            if (str[0] == entry[0]) {
                break;
            }
        }
        if (entry) {
            mangle_puts(&entry[1]);
            continue;
        }
        if (!strchr(ascii, *str)) {
            mangle_putc('_');
            continue;
        }
        mangle_putc(*str);
    }
}

static int manglepool_contains()
{
    size_t i;

    for (i = 0; i < manglepool_count; i++) {
        if (!memcmp(manglepool[i], mangle_buf, strlen(mangle_buf))) {
            return 1;
        }
    }
    return 0;
}

static char *mangle(const char *prefix, const char *forth_word)
{
    uintptr_t n;
    char *pivot;
    char *mangled;

    mangle_clear();
    mangle_puts(prefix);
    mangle_range(forth_word, forth_word + strlen(forth_word));
    pivot = mangle_buf + strlen(mangle_buf);
    n = 0;
    for (;;) {
        if (!manglepool_contains()) {
            break;
        }
        n++;
        *pivot = 0;
        mangle_putc('_');
        mangle_putu(n);
    }
    if (!(mangled = strdup(mangle_buf))) {
        panic("out of memory");
    }
    return mangled;
}

static const char *lookup(struct token *tok, size_t required_tag)
{
    struct definition *def;

    def = definitions + definitions_count;
    while (def > definitions) {
        --def;
        if (token_is_word(tok, def->forth_word)) {
            if ((tok->tag & required_tag) != required_tag) {
                panic("definition is not suitable for use");
            }
            return def->c_func_name;
        }
    }
    panic("not defined");
}

static struct definition *allocate_definition(void)
{
    struct definition *def;

    for (def = definitions; def < definitions + MAXTOKENS; def++) {
        if (!def->forth_word) {
            memset(def, 0, sizeof(*def));
            return def;
        }
    }
    panic("exceeded");
}

static struct definition *define(const char *forth_word)
{
    struct definition *def;

    def = allocate_definition();
    def->forth_word = forth_word;
    def->c_func_name = mangle("word_", forth_word);
    def->tag = INNER;
    return def;
}

static void define_builtin(
    void *forth_word, void (*compile_func)(void), size_t tag)
{
    struct definition *def;

    def = allocate_definition();
    def->forth_word = forth_word;
    def->compile_func = compile_func;
    def->tag = tag;
}

static void add_local_variable(const char *forth_var_name)
{
    struct local *local;

    if (locals_count >= MAXLOCALS) {
        panic("too many locals");
    }
    local = &locals[locals_count++];
    local->forth_var_name = forth_var_name;
    local->c_getter_name = copy_string(forth_var_name);
    local->c_setter_name = copy_two_strings(forth_var_name, "!");
    mangle("local_", forth_var_name);
}

static void for_each_local(void (*func)(struct local *))
{
    struct local *local;

    local = locals + locals_count;
    while (local > locals) {
        func(--local);
    }
}

static void compile_local(struct local *local)
{
    write(local->c_var_name);
    writeln(" = pop();");
}

static void compile_locals(void) { for_each_local(compile_local); }

static void rollback_local(struct local *local)
{
    memset(local, 0, sizeof(*local));
}

static void rollback_locals(void) { for_each_local(rollback_local); }

static void compile_variable(void)
{
    struct definition *def;
    struct token *tok;
    char *var_forth_word;
    char *c_var_name;

    if (!(tok = read_token(TOK_WORD))) {
        panic("variable name expected");
    }

    var_forth_word = tok->string;
    c_var_name = mangle("var_", var_forth_word);

    def = allocate_definition();
    def->forth_word = var_forth_word;
    def->c_func_name = mangle("word_", def->forth_word);
    def->tag = INNER;
    write("static void word_");
    write(def->c_func_name);
    writeln("(void) {");
    write("push(");
    write(c_var_name);
    writeln(");");
    writeln("}");

    def = allocate_definition();
    def->forth_word = copy_two_strings(var_forth_word, "!");
    def->c_func_name = mangle("word_", def->forth_word);
    def->tag = INNER;
    write("static void word_");
    write(def->c_func_name);
    writeln("(void) {");
    write(c_var_name);
    writeln(" = pop();");
    writeln("}");
}

static void compile_definition(void)
{
    struct token *tok;
    struct definition *def;

    if (!(tok = read_token(TOK_WORD))) {
        panic("word name expected");
    }
    def = define(tok->string);
    write("static void ");
    write(def->c_func_name);
    writeln("(void) {");
    while (!read_the_word(";")) {
        if ((tok = read_token(TOK_WORD))) {
            // compile_inner_word(tok);
        } else if ((tok = read_token(TOK_QUOTED_WORD))) {
        } else if ((tok = read_token(TOK_STRING))) {
            write("push((uintptr_t)stringpool_");
            write_unsigned(tok->number);
            writeln(");");
        } else if ((tok = read_token(TOK_CHAR | TOK_INT))) {
            write("push(");
            write_unsigned(tok->number);
            writeln(");");
        } else {
        }
    }
    writeln("}");
    rollback_locals();
}

static void compile_quote(void)
{
    struct token *name;
    const char *mangled;

    if (!(name = read_token(TOK_WORD))) {
        panic("word name expected");
    }
    if (!(mangled = lookup(name, INNER | USER))) {
        panic("not defined");
    }
#if 0
    if (def->type != USER) {
        panic("trying to quote something that is not a user word");
    }
#endif
    write("push((uintptr_t)");
    write(mangled);
    writeln(");");
}

static void compile_paren(void)
{
    struct token *tok;
    struct vec *buf;

    if (read_the_word("byte:")) {
        if (!read_token(TOK_STRING)) {
            panic("error");
        }
        if (!read_the_word(")")) {
            panic("error");
        }
    } else if (read_the_word("bytes:")) {
        buf = vec_new(1);
        while (!read_the_word(")")) {
            if ((tok = read_token(TOK_STRING))) {
                vec_puts(buf, tok->string);
            } else if ((tok = read_token(TOK_INT))) {
                intptr_t i = (intptr_t)tok->number;
                if ((i < 0x00) || (i > 0xff)) {
                    panic("byte out of range");
                }
                vec_putc(buf, (char)(unsigned char)i);
            }
        }
    } else {
        while (!read_the_word(")")) {
            if ((tok = read_token(TOK_WORD))) {
                add_local_variable(tok->string);
            }
        }
        compile_locals();
    }
}

static void compile_and(void) { writeln("if (!flag) return;"); }

static void compile_or(void) { writeln("if (flag) return;"); }

static void compile_recurse(void)
{
    write(definitions[definitions_count - 1].c_func_name);
    writeln("();");
}

// static void compile_word(struct token *tok, size_t context) {}

// static void compile_top_level(void) { panic("unknown top-level syntax"); }

int main(void)
{
    define_builtin(":", compile_definition, TOPLEVEL | LOCKED);
    define_builtin("variable", compile_variable, TOPLEVEL | LOCKED);
    define_builtin("&", compile_and, INNER | LOCKED);
    define_builtin("|", compile_or, INNER | LOCKED);
    define_builtin("recurse", compile_recurse, INNER | LOCKED);
    define_builtin("'", compile_quote, INNER | LOCKED);
    define_builtin("(", compile_paren, INNER | LOCKED);
    slurp();
    tokenize();
    while (!read_token(TOK_EOF)) {
        struct token *tok = read_token((size_t)-1);
        printf("%2zu  %s\n", tok->tag, tok->string);
        // lookup(TOPLEVEL);
    }
    return 0;
}
