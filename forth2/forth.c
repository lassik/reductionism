// Forth runtime

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*word_func_t)(void);

static void die(const char *msg) __attribute__((__noreturn__));
static void *die_if_no_memory(void *p);
static void die_if_overflow(bool overflow);

static void die(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(2);
}

static void *die_if_no_memory(void *p)
{
    if (!p) {
        die("out of memory");
    }
    return p;
}

static void die_if_overflow(bool overflow)
{
    if (overflow) {
        die("numeric overflow");
    }
}

static uintptr_t stackbuf[16];
static uintptr_t *stack = stackbuf;
static bool flag;

static size_t bytes_from_cells(uintptr_t n)
{
    size_t nbytes;
    die_if_overflow(__builtin_mul_overflow(n, sizeof(void *), &nbytes));
    return nbytes;
}

static void push(uintptr_t x) { *stack++ = x; }
static void pushsigned(intptr_t x) { push((uintptr_t)x); }
static void pushpointer(void *x) { push((uintptr_t)x); }
static void pushfunc(word_func_t func) { push((uintptr_t)func); }
static void push_c_string(const char *str)
{
    push((uintptr_t)str);
    push(strlen(str));
}

static uintptr_t peek(void) { return stack[-1]; }
static uintptr_t pop(void) { return *--stack; }
static void drop(void) { stack--; }
static void *poppointer(void) { return (void *)(pop()); }
static size_t popsize(void) { return (size_t)pop(); }
static int popint(void) { return (int)pop(); }

static void pop2(uintptr_t *a, uintptr_t *b)
{
    *b = pop();
    *a = pop();
}

static void pop2signed(intptr_t *a, intptr_t *b)
{
    *b = (intptr_t)pop();
    *a = (intptr_t)pop();
}

static void peekpop(uintptr_t *a, uintptr_t *b)
{
    *b = pop();
    *a = peek();
}

static void peekpopsigned(intptr_t *a, intptr_t *b)
{
    *b = (intptr_t)(pop());
    *a = (intptr_t)(peek());
}

#include "forth_os_unix.h"

static void prim_flag(void) { push(flag); }

static void prim_drop(void) { drop(); }

static void prim_dup(void) { push(peek()); }

static void prim_ne(void)
{
    uintptr_t a, b;
    peekpop(&a, &b);
    flag = a != b;
}

static void prim_eq(void)
{
    uintptr_t a, b;
    peekpop(&a, &b);
    flag = a == b;
}

static void prim_lt(void)
{
    uintptr_t a, b;
    peekpop(&a, &b);
    flag = a < b;
}

static void prim_lt_s(void)
{
    intptr_t a, b;
    peekpopsigned(&a, &b);
    flag = a < b;
}

static void prim_gt(void)
{
    uintptr_t a, b;
    peekpop(&a, &b);
    flag = a > b;
}

static void prim_gt_s(void)
{
    intptr_t a, b;
    peekpopsigned(&a, &b);
    flag = a > b;
}

static void prim_le(void)
{
    uintptr_t a, b;
    peekpop(&a, &b);
    flag = a <= b;
}

static void prim_le_s(void)
{
    intptr_t a, b;
    peekpopsigned(&a, &b);
    flag = a <= b;
}

static void prim_ge(void)
{
    uintptr_t a, b;
    peekpop(&a, &b);
    flag = a >= b;
}

static void prim_ge_s(void)
{
    intptr_t a, b;
    peekpopsigned(&a, &b);
    flag = a >= b;
}

static void prim_plus(void)
{
    uintptr_t a, b, c;
    pop2(&a, &b);
    die_if_overflow(__builtin_add_overflow(a, b, &c));
    push(c);
}

static void prim_plus_carry(void)
{
    uintptr_t a, b, c;
    pop2(&a, &b);
    flag = __builtin_add_overflow(a, b, &c);
    push(c);
}

static void prim_pluss(void)
{
    intptr_t a, b, c;
    pop2signed(&a, &b);
    die_if_overflow(__builtin_add_overflow(a, b, &c));
    pushsigned(c);
}

static void prim_minus(void)
{
    uintptr_t a, b, c;
    pop2(&a, &b);
    die_if_overflow(__builtin_sub_overflow(a, b, &c));
    push(c);
}

static void prim_minus_s(void)
{
    intptr_t a, b, c;
    pop2signed(&a, &b);
    die_if_overflow(__builtin_sub_overflow(a, b, &c));
    pushsigned(c);
}

static void prim_star(void)
{
    uintptr_t a, b, c;
    pop2(&a, &b);
    die_if_overflow(__builtin_mul_overflow(a, b, &c));
    push(c);
}

static void prim_star_s(void)
{
    intptr_t a, b, c;
    pop2signed(&a, &b);
    die_if_overflow(__builtin_mul_overflow(a, b, &c));
    pushsigned(c);
}

static void prim_cells(void)
{
    push(sizeof(uintptr_t));
    prim_star();
}

static void prim_cell_bits(void) { push(sizeof(uintptr_t) * CHAR_BIT); }

static void prim_max_to_n_bits(void)
{
    unsigned long x = pop();
    unsigned int width = sizeof(x) * CHAR_BIT;
    unsigned int n_bits = width - (unsigned int)__builtin_clzl(x);
    push(n_bits);
}

static void prim_n_bits_to_bitmask(void)
{
    uintptr_t n_bits = pop();
    push(((uintptr_t)1 << n_bits) - 1);
}

static void prim_and_bits(void)
{
    uintptr_t a, b;
    pop2(&a, &b);
    push(a & b);
}

static void prim_or_bits(void)
{
    uintptr_t a, b;
    pop2(&a, &b);
    push(a | b);
}

static void prim_call(void)
{
    word_func_t func = (word_func_t)poppointer();
    func();
}

static void prim_allocate(void)
{
    pushpointer(die_if_no_memory(calloc(1, popsize())));
}

static void prim_reallocate(void)
{
    void *p = poppointer();
    pushpointer(die_if_no_memory(realloc(p, popsize())));
}

static void prim_deallocate(void) { free(poppointer()); }

static void prim_fetch(void)
{
    uintptr_t *p = poppointer();
    push(*p);
}

static void prim_store(void)
{
    uintptr_t *p = poppointer();
    *p = pop();
}

static void prim_byte_fetch(void)
{
    uint8_t *p = poppointer();
    push(*p);
}

static void prim_byte_store(void)
{
    uint8_t *p = poppointer();
    *p = (uint8_t)(pop());
}

static void prim_bytes_equal(void)
{
    uintptr_t nbyte = pop();
    uint8_t *b = poppointer();
    uint8_t *a = poppointer();
    flag = 1;
    for (; nbyte; nbyte--) {
        flag &= (*a++ == *b++);
    }
}

static void prim_zero_cells(void)
{
    size_t nbytes = bytes_from_cells(popsize());
    memset_s(poppointer(), nbytes, 0, nbytes);
}

static void prim_show(void) { fprintf(stderr, "%" PRIuPTR "\n", peek()); }

static void prim_shows(void)
{
    fprintf(stderr, "%" PRIdPTR "\n", (intptr_t)peek());
}

static void prim_show_hex(void)
{
    fprintf(stderr, "0x%" PRIxPTR "\n", peek());
}

static void prim_show_byte(void)
{
    fprintf(stderr, "%c", (int)(uint8_t)(peek()));
}

static void prim_show_bytes(void)
{
    size_t n = popsize();
    fwrite(poppointer(), 1, n, stderr);
}

static void prim_show_stack(void)
{
    uintptr_t *p;

    fprintf(stderr, "(");
    p = stackbuf;
    if (p < stack) {
        fprintf(stderr, "%" PRIdPTR, *p++);
    }
    while (p < stack) {
        fprintf(stderr, " %" PRIdPTR, *p++);
    }
    fprintf(stderr, ") %c\n", flag ? 'T' : 'F');
}

#include "scheme.h"

int main(void)
{
    word_main();
    return 0;
}
