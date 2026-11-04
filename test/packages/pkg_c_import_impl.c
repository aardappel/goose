/* pkg_c_import.h's functions. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "pkg_c_import.h"

struct ci_counter { char name[32]; int total; };
int ci_global = 3;
static int calls;

ci_counter *ci_counter_new(const char *name) {
    ci_counter *c = calloc(1, sizeof *c);
    strncpy(c->name, name ? name : "", sizeof c->name - 1);
    calls++;
    return c;
}
int ci_counter_add(ci_counter *c, int n) { calls++; return c->total += n; }
const char *ci_counter_name(const ci_counter *c) { calls++; return c->name; }
void ci_counter_free(ci_counter *c) { calls++; free(c); }
int ci_counter_open(const char *name, ci_counter **out) {
    calls++;
    if (!name || !*name) return -1;
    *out = ci_counter_new(name);
    return 0;
}
size_t ci_strlen(const char *s) { calls++; return s ? strlen(s) : 0; }
long ci_long_twice(long x) { calls++; return 2 * x; }
unsigned char ci_flags(unsigned char f) { calls++; return (unsigned char)(f | 0x80); }
_Bool ci_is_even(int n) { calls++; return n % 2 == 0; }
enum ci_color ci_next_color(enum ci_color c) { calls++; return c == CI_RED ? CI_GREEN : CI_BLUE; }
void ci_item_fill(ci_item *it, char tag, int value, double weight) {
    calls++;
    it->tag = tag; it->value = value; it->weight = weight;
}
double ci_item_weight(const ci_item *it) { calls++; return it->weight * it->value; }
ci_point ci_point_add(ci_point a, ci_point b) { calls++; ci_point r = { a.x + b.x, a.y + b.y }; return r; }
long long ci_sum_ints(const int *xs, size_t n) {
    long long s = 0;
    calls++;
    if (!xs) return -1;
    for (size_t i = 0; i < n; i++) s += xs[i];
    return s;
}

void ci_fill(int *out, size_t n, int v) {
    calls++;
    for (size_t i = 0; i < n; i++) out[i] = v + (int)i;
}

unsigned long long ci_addr(const void *p) {
    calls++;
    return (unsigned long long)(uintptr_t)p;
}

int ci_each(const int *xs, size_t n, ci_visit f, void *ud) {
    int s = 0;
    calls++;
    if (!f) return -1;
    for (size_t i = 0; i < n; i++) s += f(ud, xs[i]);
    return s;
}

int ci_sum_bytes(const void *p, size_t n) {
    const unsigned char *b = p;
    int s = 0;
    calls++;
    for (size_t i = 0; i < n; i++) s += b[i];
    return s;
}
int ci_maybe(int *out) { calls++; if (out) *out = 99; return out != 0; }
int ci_call_count(void) { return calls; }
int ci_printf(const char *fmt, ...) { (void)fmt; return 0; }
