/* tcc_cdecl.h -- the declarations of a C header, as TinyCC parses them
   (tcc_cdecl.c). Used by the compiler's `import c` (src/cimport.h). */

#ifndef TCC_CDECL_H
#define TCC_CDECL_H

#ifdef __cplusplus
extern "C" {
#endif

/* What a declared name is (gcd_cb.decl). */
enum {
    GCD_FUNCTION = 1,   /* an extern function */
    GCD_VARIABLE,       /* an extern variable */
    GCD_STATIC,         /* a static (inline) function or variable: no symbol to bind */
    GCD_TYPEDEF,
    GCD_ENUM_CONST,
    GCD_TAG,            /* a struct, union or enum tag */
    GCD_MACRO,          /* an object-like macro, unevaluated (names_only) */
    GCD_MACRO_FUNCTION  /* a function-like macro */
};

/* What an object-like macro's value is (gcd_cb.macro). */
enum {
    GCD_MACRO_NONE = 0, /* not a constant */
    GCD_MACRO_INT,      /* i (as unsigned if is_unsigned), of size bytes */
    GCD_MACRO_FLOAT,    /* d */
    GCD_MACRO_STRING,   /* s, n bytes (without the terminating zero) */
    GCD_MACRO_POINTER   /* i, a constant cast to a pointer type */
};

/* Kinds of type (gcd_type_kind). */
enum {
    GCD_VOID, GCD_INT, GCD_BOOL, GCD_FLOAT, GCD_DOUBLE, GCD_LDOUBLE,
    GCD_PTR, GCD_ARRAY, GCD_FUNC, GCD_STRUCT, GCD_UNION, GCD_OTHER
};

/* For GCD_TAG, decl's `sym` is the tag's type, to read with gcd_type_*. */
typedef struct gcd_cb {
    void *ctx;
    void (*decl)(void *ctx, int kind, const char *name, const void *sym);
    void (*macro)(void *ctx, const char *name, int kind, long long i, int is_unsigned,
                  int size, double d, const char *s, int n);
    void (*dep)(void *ctx, const char *path);       /* each file included */
    void (*error)(void *ctx, const char *msg);
} gcd_cb;

/* Parses `source` (`#include "x.h"`) with the given system include paths,
   include paths and -D defines (NAME or NAME=VALUE), and calls back for
   each declared name. With names_only, macros are reported by name
   (GCD_MACRO) without being evaluated. 0, or -1 after errors. */
int gcd_parse(const char *libpath, const char *const *sysinc, int nsys,
              const char *const *inc, int ninc, const char *const *defines, int ndef,
              const char *source, int names_only, gcd_cb *cb);

/* Accessors: valid only inside a callback. A sym is a declared name, a
   parameter or a field; a type belongs to a sym or to another type. */
const void *gcd_sym_type(const void *sym);
const char *gcd_sym_name(const void *sym);     /* NULL if anonymous */
const void *gcd_sym_next(const void *sym);     /* the next parameter or field */
int gcd_sym_offset(const void *sym);           /* a field's byte offset */
long long gcd_sym_enum_value(const void *sym);

int gcd_type_kind(const void *type);
int gcd_type_size(const void *type, int *align); /* -1: incomplete */
int gcd_type_unsigned(const void *type);
int gcd_type_const(const void *type);
int gcd_type_long(const void *type);           /* spelled `long` */
int gcd_type_char(const void *type);           /* a char type */
int gcd_type_bitfield(const void *type);
int gcd_type_enum(const void *type);
const void *gcd_type_target(const void *type); /* pointee, element, or result */
long long gcd_type_count(const void *type);    /* an array's length */
const void *gcd_type_first(const void *type);  /* first parameter or field */
int gcd_type_variadic(const void *type);
int gcd_type_oldstyle(const void *type);       /* f() without a prototype */
const void *gcd_type_tag(const void *type);    /* a struct/union/enum's identity */
const char *gcd_tag_name(const void *tag);     /* NULL if anonymous */
int gcd_tag_complete(const void *tag);

#ifdef __cplusplus
}
#endif

#endif
