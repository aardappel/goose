/* The header pkg_c_import.goose imports with `import c`: one case of each
   rule of the translation (docs/reference.md, "import c"). */
#ifndef PKG_C_IMPORT_H
#define PKG_C_IMPORT_H

#include <stddef.h>

#define CI_ANSWER 42
#define CI_NEG (-7)
#define CI_BIG 0x100000000LL
#define CI_MASK 0xFFFFFFFFu
#define CI_RATIO 1.5
#define CI_NAME "pk" "g"
#define CI_TWICE(x) ((x) * 2)                  /* function-like: not a constant */

typedef void (*ci_fn)(void *);
#define CI_SENTINEL ((ci_fn)-1)                /* a pointer constant */

enum ci_color { CI_RED, CI_GREEN = 5, CI_BLUE };

typedef struct ci_counter ci_counter;          /* never completed: a handle */
typedef struct { char tag; int value; double weight; } ci_item;   /* C pads it */
typedef struct { int x, y; } ci_point;         /* no padding: crosses by value */

ci_counter *ci_counter_new(const char *name);
int ci_counter_add(ci_counter *c, int n);
const char *ci_counter_name(const ci_counter *c);
void ci_counter_free(ci_counter *c);
int ci_counter_open(const char *name, ci_counter **out);

size_t ci_strlen(const char *s);               /* NULL: 0 */
long ci_long_twice(long x);
unsigned char ci_flags(unsigned char f);
_Bool ci_is_even(int n);
enum ci_color ci_next_color(enum ci_color c);

void ci_item_fill(ci_item *it, char tag, int value, double weight);
double ci_item_weight(const ci_item *it);
ci_point ci_point_add(ci_point a, ci_point b);
int ci_sum_bytes(const void *p, size_t n);     /* const cptr */
long long ci_sum_ints(const int *xs, size_t n); /* a pointer and a length: a slice, empty too */
void ci_fill(int *out, size_t n, int v);
unsigned long long ci_addr(const void *p);     /* an address held as u64 crosses as it is */
int ci_maybe(int *out);                        /* NULL-able */
int ci_call_count(void);

int ci_printf(const char *fmt, ...);           /* variadic: left out */
int ci_absent(int x);                         /* never built: a JIT run that does not call it links */
extern int ci_global;                          /* a variable: left out */

#endif
