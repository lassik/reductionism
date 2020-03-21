#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOURCE "scheme.4th"

#define DEF_COMPILE (1 << 0)
#define DEF_TOP_LEVEL (1 << 1)
#define DEF_PRIMITIVE (1 << 2)
#define DEF_USER (1 << 3)

#define TOK_EOF (1 << 0)
#define TOK_WORD (1 << 1)
#define TOK_STRING (1 << 3)
#define TOK_CHAR (1 << 4)
#define TOK_UINT (1 << 5)
#define TOK_NEGINT (1 << 6)

struct vec {
    unsigned char *bytes;
    size_t itemsize;
    size_t cap;
    size_t len;
    size_t mark;
};

struct token {
    size_t tag;
    char *string;
    uintptr_t number; // number, character, or string-pool index
};

struct definition {
    const char *forth_word;
    char *c_func_name;
    void (*compile)(void);
    size_t tag;
};

struct local {
    const char *forth_word;
    char *forth_word_setter;
    char *c_var_name;
};

static const char indent[] = "    ";

static const char ascii[] = "0123456789"
                            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                            "abcdefghijklmnopqrstuvwxyz";

static const char *mangle_one_char[] = { "=_equal", "@_fetch", "!_store",
    "+_plus", "*_star", "/_slash", "?_p", 0 };
static const char *mangle_two_char[] = { "->_to_", 0 };
static struct vec *mangle_pool;
static struct vec *definitions;
static struct vec *locals;

static struct token token_eof = { .tag = TOK_EOF };
static struct vec *tokens;
static size_t tokens_pos;

static size_t source_pos;
static struct vec *source;

static void panic(const char *s) __attribute__((__noreturn__));
static void panic1(const char *s1, const char *s2)
    __attribute__((__noreturn__));

static void panic(const char *s)
{
    fprintf(stderr, "%s\n", s);
    exit(2);
}

static void panic1(const char *s1, const char *s2)
{
    fprintf(stderr, "%s %s\n", s1, s2);
    exit(2);
}

static void *zeroalloc(size_t nbyte)
{
    void *p = calloc(nbyte, 1);
    if (!p)
        panic("out of memory");
    return p;
}

static char *copy_string_span(const char *start, const char *limit)
{
    char *dup;
    size_t len;

    len = (size_t)(limit - start);
    if (!(dup = zeroalloc(len + 1))) {
        panic("out of memory");
    }
    memcpy(dup, start, len);
    return dup;
}

static char *copy_string(const char *str)
{
    return copy_string_span(str, str + strlen(str));
}

static char *copy_two_strings(const char *a, const char *b)
{
    char *dup;
    size_t alen, blen;

    alen = strlen(a);
    blen = strlen(b);
    if (!(dup = zeroalloc(alen + blen + 1))) {
        panic("out of memory");
    }
    memcpy(dup, a, alen);
    memcpy(dup + alen, b, blen);
    return dup;
}

static struct vec *vec_new(size_t itemsize)
{
    struct vec *vec = zeroalloc(sizeof(*vec));
    vec->itemsize = itemsize;
    return vec;
}

static void *vec_get(struct vec *vec, size_t i)
{
    return vec->bytes + vec->itemsize * i;
}

static void vec_mark(struct vec *vec) { vec->mark = vec->len; }

static void vec_clear_to_mark(struct vec *vec)
{
    memset(vec->bytes + vec->itemsize * vec->mark, 0,
        vec->itemsize * (vec->cap - vec->mark));
    vec->len = vec->mark;
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
        if (!(vec->bytes = realloc(vec->bytes, vec->cap * vec->itemsize))) {
            panic("out of memory");
        }
    }
    void *p = vec->bytes + vec->len * vec->itemsize;
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

static void vec_putd(struct vec *vec, int n)
{
    char s[16];
    snprintf(s, sizeof(s), "%d", n);
    vec_puts(vec, s);
}

static void display(const char *str) { printf("%s", str); }

static void displayln(const char *str) { printf("%s\n", str); }

static void display_uintptr(uintptr_t u) { printf("%" PRIuPTR, u); }

static void newline(void) { printf("\n"); }

static void slurp(void)
{
    const size_t chunk_cap = 512;
    char *chunk;
    FILE *input;
    size_t empty, chunk_len;

    if (!(input = fopen(SOURCE, "rb"))) {
        panic("cannot open " SOURCE);
    }
    do {
        chunk = vec_reserve(source, chunk_cap);
        chunk_len = fread(chunk, 1, chunk_cap, input);
        if (ferror(input)) {
            panic("cannot read from file");
        }
        empty = chunk_cap - chunk_len;
    } while (!empty);
    source->len -= empty;
    if (fclose(input) != 0) {
        panic("cannot close file");
    }
    if (memchr(source->bytes, 0, source->len)) {
        panic("source code contains null byte");
    }
}

static int read_char_if(int (*predicate)(int))
{
    if (source_pos >= source->len) {
        return 0;
    }
    if (!predicate(source->bytes[source_pos])) {
        return 0;
    }
    return source->bytes[source_pos++];
}

static int read_the_char(int ch)
{
    if (source_pos >= source->len) {
        return 0;
    }
    if (source->bytes[source_pos] != ch) {
        return 0;
    }
    return source->bytes[source_pos++];
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

    tok = vec_reserve(tokens, 1);
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

    source->mark = source_pos;
    while (!read_the_char('"')) {
        if (read_char_if(is_string_char)) {
            ;
        } else {
            panic("Syntax error inside string");
        }
    }
    tok = allocate_token(TOK_STRING);
    tok->string = copy_string_span((char *)source->bytes + source->mark,
        (char *)source->bytes + source_pos - 1);
}

static int parse_number(const char *str, const char *limit, struct token *tok)
{
    const char digits[] = "0123456789abcdef";
    const char *digitp;
    uintptr_t digit, value;
    size_t base = 10;
    int is_negative;

    value = 0;
    is_negative = (str[0] == '-');
    if (is_negative) {
        str++;
    }
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
        if (!(digitp = strchr(digits, *str++))) {
            return 0;
        }
        digit = (uintptr_t)(digitp - digits);
        if (digit >= base) {
            return 0;
        }
        value *= base;
        value += digit;
    }
    tok->tag = is_negative ? TOK_NEGINT : TOK_UINT;
    tok->number = value;
    return 1;
}

static void read_word_token_or_panic(void)
{
    struct token *tok;

    source->mark = source_pos;
    while (read_char_if(is_word_char))
        ;
    if (source->mark == source_pos) {
        panic("Syntax error at top level");
    }
    tok = allocate_token(TOK_WORD);
    tok->string = copy_string_span((char *)source->bytes + source->mark,
        (char *)source->bytes + source_pos);
    parse_number((char *)source->bytes + source->mark,
                 (char *)source->bytes + source_pos, tok);
}

static void tokenize(void)
{
    for (;;) {
        skip_whitespace();
        if (source_pos == source->len) {
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
    struct token *tok;

    if (tokens_pos >= tokens->len) {
        if (tag_bits & TOK_EOF) {
            return &token_eof;
        }
        return 0;
    }
    tok = vec_get(tokens, tokens_pos);
    if (!(tok->tag & tag_bits)) {
        return 0;
    }
    tokens_pos++;
    return tok;
}

static struct token *read_the_word(const char *word)
{
    struct token *tok;

    if (tokens_pos >= tokens->len) {
        return 0;
    }
    tok = vec_get(tokens, tokens_pos);
    if (!token_is_word(tok, word)) {
        return 0;
    }
    tokens_pos++;
    return tok;
}

static int mangle_pool_contains(const char *mangled)
{
    const char **sp;
    const char *s;
    size_t i;

    for (i = 0; i < mangle_pool->len; i++) {
        sp = vec_get(mangle_pool, i);
        s = *sp;
        if (!strcmp(mangled, s)) {
            return 1;
        }
    }
    return 0;
}

static char *mangle_pool_add(const char *mangled)
{
    char **mangledp;

    mangledp = vec_reserve(mangle_pool, 1);
    *mangledp = copy_string(mangled);
    return *mangledp;
}

static char *mangle(const char *prefix, const char *forth_word)
{
    const char *entry;
    const char **entryp;
    struct vec *mangled;
    int n;

    mangled = vec_new(sizeof(char));
    vec_puts(mangled, prefix);
    for (; forth_word[0]; forth_word++) {
        for (entryp = mangle_two_char; (entry = *entryp); entryp++) {
            if ((forth_word[0] == entry[0]) && (forth_word[1] == entry[1])) {
                break;
            }
        }
        if (entry) {
            vec_puts(mangled, &entry[2]);
            forth_word++;
            continue;
        }
        for (entryp = mangle_one_char; (entry = *entryp); entryp++) {
            if (forth_word[0] == entry[0]) {
                break;
            }
        }
        if (entry) {
            vec_puts(mangled, &entry[1]);
            continue;
        }
        vec_putc(mangled, strchr(ascii, forth_word[0]) ? forth_word[0] : '_');
    }
    vec_mark(mangled);
    vec_putc(mangled, 0);
    n = 0;
    while (mangle_pool_contains((char *)mangled->bytes)) {
        vec_clear_to_mark(mangled);
        vec_putc(mangled, '_');
        vec_putd(mangled, ++n);
        vec_putc(mangled, 0);
    }
    return mangle_pool_add((char *)mangled->bytes);
}

static struct definition *lookup(const char *forth_word, size_t required_tag)
{
    size_t i = definitions->len;
    while (i) {
        struct definition *def = vec_get(definitions, --i);
        if (strcmp(def->forth_word, forth_word)) {
            continue;
        }
        if ((def->tag & required_tag) != required_tag) {
            panic1("definition is not of the expected type:", forth_word);
        }
        return def;
    }
    return 0;
}

static struct definition *allocate_definition(const char *forth_word)
{
    struct definition *def;

    def = lookup(forth_word, 0);
    if (!def)
        def = vec_reserve(definitions, 1);
    memset(def, 0, sizeof(*def));
    def->forth_word = forth_word;
    return def;
}

static void define_compile_top_level(
    const char *forth_word, void (*compile)(void))
{
    struct definition *def = allocate_definition(forth_word);
    def->tag = DEF_COMPILE | DEF_TOP_LEVEL;
    def->compile = compile;
}

static void define_compile(const char *forth_word, void (*compile)(void))
{
    struct definition *def = allocate_definition(forth_word);
    def->tag = DEF_COMPILE;
    def->compile = compile;
}

static void define_primitive(const char *forth_word, const char *c_func_name)
{
    struct definition *def = allocate_definition(forth_word);
    def->tag = DEF_PRIMITIVE;
    def->c_func_name = copy_string(c_func_name);
}

static struct definition *define_user(const char *forth_word)
{
    struct definition *def = allocate_definition(forth_word);
    def->tag = DEF_USER;
    def->c_func_name = mangle("word_", forth_word);
    return def;
}

static struct local *lookup_local(const char *forth_word, int *out_is_setter)
{
    *out_is_setter = 0;
    size_t i = locals->len;
    while (i) {
        struct local *local = vec_get(locals, --i);
        if (!strcmp(forth_word, local->forth_word)) {
            return local;
        }
        if (!strcmp(forth_word, local->forth_word_setter)) {
            *out_is_setter = 1;
            return local;
        }
    }
    return 0;
}

static void add_local(const char *forth_word)
{
    struct local *local;

    local = vec_reserve(locals, 1);
    local->forth_word = copy_string(forth_word);
    local->forth_word_setter = copy_two_strings(forth_word, "!");
    local->c_var_name = mangle("local_", forth_word);
}

static void compile_locals(void)
{
    size_t i = locals->len;
    while (i > locals->mark) {
        if (i < locals->len)
            display(indent);
        struct local *local = vec_get(locals, --i);
        display("uintptr_t ");
        display(local->c_var_name);
        displayln(" = pop();");
    }
}

static void rollback_locals(void)
{
    size_t i = locals->len;
    while (i) {
        struct local *local = vec_get(locals, --i);
        free(local->forth_word_setter);
        // free(local->c_var_name); //! TODO: use-after-free
    }
    locals->mark = 0;
    vec_clear_to_mark(locals);
}

static void compile_top_level_variable(void)
{
    struct definition *def;
    struct token *tok;
    char *forth_word;
    char *forth_word_setter;
    char *c_var_name;

    if (!(tok = read_token(TOK_WORD))) {
        panic("variable name expected");
    }
    forth_word = tok->string;
    forth_word_setter = copy_two_strings(forth_word, "!");
    c_var_name = mangle("var_", forth_word);

    display("static uintptr_t ");
    display(c_var_name);
    displayln(";");
    newline();

    def = define_user(forth_word);
    display("static void ");
    display(def->c_func_name);
    displayln("(void) {");
    display(indent);
    display("push(");
    display(c_var_name);
    displayln(");");
    displayln("}");
    newline();

    def = define_user(forth_word_setter);
    display("static void ");
    display(def->c_func_name);
    displayln("(void) {");
    display(indent);
    display(c_var_name);
    displayln(" = pop();");
    displayln("}");
}

static void compile_top_level_definition(void)
{
    struct token *tok;
    struct local *local;
    struct definition *def;
    struct definition *inner_def;
    int is_setter;

    if (!(tok = read_token(TOK_WORD))) {
        panic("word name expected");
    }
    def = define_user(tok->string);
    display("static void ");
    display(def->c_func_name);
    displayln("(void) {");
    while (!read_the_word(";")) {
        display(indent);
        if ((tok = read_token(TOK_WORD))) {
            const char *forth_word = tok->string;
            if ((local = lookup_local(forth_word, &is_setter))) {
                if (!is_setter) {
                    display("push(");
                    display(local->c_var_name);
                    displayln(");");
                } else {
                    display(local->c_var_name);
                    displayln(" = pop();");
                }
            } else if ((inner_def = lookup(forth_word, 0))) {
                if (inner_def->tag == DEF_COMPILE) {
                    inner_def->compile();
                } else if ((inner_def->tag == DEF_PRIMITIVE)
                    || (inner_def->tag == DEF_USER)) {
                    display(inner_def->c_func_name);
                    displayln("();");
                } else {
                    panic1("cannot use that in a definition:", forth_word);
                }
            } else {
                panic1("not defined:", forth_word);
            }
        } else if ((tok = read_token(TOK_STRING))) {
            display("push((uintptr_t)(unsigned char *)");
            display("\"");
            display(tok->string);
            display("\"");
            displayln(");");
        } else if ((tok = read_token(TOK_CHAR | TOK_UINT))) {
            display("push(");
            display_uintptr(tok->number);
            displayln(");");
        } else if ((tok = read_token(TOK_NEGINT))) {
            display("push((uintptr_t)-(intptr_t)");
            display_uintptr(tok->number);
            displayln(");");
        } else {
            panic("huh?");
        }
    }
    displayln("}");
    rollback_locals();
}

static void compile_parentheses(void)
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
            } else if ((tok = read_token(TOK_UINT))) {
                intptr_t i = (intptr_t)tok->number;
                if ((i < 0x00) || (i > 0xff)) {
                    panic("byte out of range");
                }
                vec_putc(buf, (char)(unsigned char)i);
            }
        }
    } else {
        vec_mark(locals);
        while (!read_the_word(")")) {
            if ((tok = read_token(TOK_WORD))) {
                add_local(tok->string);
            } else {
                panic("wrong thing");
            }
        }
        compile_locals();
    }
}

static void compile_quote(void)
{
    struct definition *def;
    struct token *tok;
    char *forth_word;

    if (!(tok = read_token(TOK_WORD))) {
        panic("word name expected");
    }
    forth_word = tok->string;
    if (!(def = lookup(forth_word, DEF_USER))) {
        panic1("not defined:", forth_word);
    }
    display("push((uintptr_t)");
    display(def->c_func_name);
    displayln(");");
}

static void compile_and(void) { displayln("if (!flag) return;"); }

static void compile_or(void) { displayln("if (flag) return;"); }

static void compile_recurse(void)
{
    struct definition *def = vec_get(definitions, definitions->len - 1);
    display(def->c_func_name);
    displayln("();");
}

static int compile_top_level(void)
{
    struct definition *def;
    struct token *tok;
    const char *forth_word;

    if (read_token(TOK_EOF))
        return 0;
    if (!(tok = read_token(TOK_WORD)))
        panic("unknown top-level syntax");
    forth_word = tok->string;
    if (!(def = lookup(forth_word, DEF_TOP_LEVEL)))
        panic1("no top-level definition: ", forth_word);
    newline();
    def->compile();
    return 1;
}

int main(void)
{
    mangle_pool = vec_new(sizeof(char *));
    definitions = vec_new(sizeof(struct definition));
    locals = vec_new(sizeof(struct local));
    tokens = vec_new(sizeof(struct token));
    source = vec_new(sizeof(char));

    define_compile_top_level("variable", compile_top_level_variable);
    define_compile_top_level(":", compile_top_level_definition);

    define_compile("(", compile_parentheses);
    define_compile("'", compile_quote);
    define_compile("&", compile_and);
    define_compile("|", compile_or);
    define_compile("recurse", compile_recurse);

    define_primitive("<>", "prim_ne");
    define_primitive("=", "prim_eq");
    define_primitive("<", "prim_lt");
    define_primitive("<=", "prim_le");
    define_primitive(">", "prim_gt");
    define_primitive(">=", "prim_ge");
    define_primitive(">=s", "prim_ge_s");
    define_primitive("+", "prim_plus");
    define_primitive("+s", "prim_plus_s");
    define_primitive("+carry", "prim_plus_carry");
    define_primitive("-", "prim_minus");
    define_primitive("-s", "prim_minus_s");
    define_primitive("*", "prim_star");
    define_primitive("*s", "prim_star_s");
    define_primitive("@", "prim_fetch");
    define_primitive("!", "prim_store");
    define_primitive("byte@", "prim_byte_fetch");
    define_primitive("byte!", "prim_byte_store");
    define_primitive("bytes=", "prim_bytes_equal");
    define_primitive("allocate", "prim_allocate");
    define_primitive("and-bits", "prim_and_bits");
    define_primitive("call", "prim_call");
    define_primitive("cell-bits", "prim_cell_bits");
    define_primitive("cells", "prim_cells");
    define_primitive("deallocate", "prim_deallocate");
    define_primitive("drop", "prim_drop");
    define_primitive("dup", "prim_dup");
    define_primitive("flag", "prim_flag");
    define_primitive("max->n-bits", "prim_max_to_n_bits");
    define_primitive("n-bits->bitmask", "prim_n_bits_to_bitmask");
    define_primitive("or-bits", "prim_or_bits");
    define_primitive("os-error-message", "prim_os_error_message");
    define_primitive("os-exit", "prim_os_exit");
    define_primitive("os-read", "prim_os_read");
    define_primitive("os-write", "prim_os_write");
    define_primitive("reallocate", "prim_reallocate");
    define_primitive("show", "prim_show");
    define_primitive("show-byte", "prim_show_byte");
    define_primitive("show-bytes", "prim_show_bytes");
    define_primitive("show-hex", "prim_show_hex");
    define_primitive("show-stack", "prim_show_stack");
    define_primitive("shows", "prim_shows");
    define_primitive("zero-cells", "prim_zero_cells");

    slurp();
    tokenize();
    while (compile_top_level())
        ;
    return 0;
}
