/* tcc_cdecl.c -- TinyCC as a reader of C declarations, for `import c`.

   This file is the libtcc translation unit: it includes libtcc.c, which
   with ONE_SOURCE includes the whole of TinyCC, so the parser's internals
   (static there) are visible below. cmake/libtcc.cmake builds the library
   from this file instead of libtcc.c; nothing in TinyCC itself changes.

   gcd_parse() compiles a C source (in practice `#include "x.h"`) without
   generating code, and before TinyCC frees its symbol tables walks every
   name the source declared: functions, variables, typedefs, enum constants,
   struct/union/enum tags and object-like macros. Each comes to a callback
   as an opaque handle, read with the gcd_* accessors, which are valid only
   during the callback. Macros are evaluated here, as C constant
   expressions in the scope of the header (so casts to its typedefs work).

   The interface is plain C (tcc_cdecl.h); the compiler is C++ and never
   sees TinyCC's own headers. */

#include "libtcc.c"
#include "tcc_cdecl.h"

/* --- accessors -------------------------------------------------------- */

const void *gcd_sym_type(const void *sym) { return &((const Sym *)sym)->type; }
long long gcd_sym_enum_value(const void *sym) { return ((const Sym *)sym)->enum_val; }
const void *gcd_sym_next(const void *sym) { return ((const Sym *)sym)->next; }
int gcd_sym_offset(const void *sym) { return ((const Sym *)sym)->c; }

const char *gcd_sym_name(const void *sym)
{
    int v = ((const Sym *)sym)->v & ~(SYM_FIELD | SYM_STRUCT);
    if (v >= SYM_FIRST_ANOM || v < TOK_IDENT)
        return NULL;
    return get_tok_str(v, NULL);
}

int gcd_type_kind(const void *type)
{
    const CType *t = type;
    int bt = t->t & VT_BTYPE;
    switch (bt) {
    case VT_VOID: return GCD_VOID;
    case VT_BYTE: case VT_SHORT: case VT_INT: case VT_LLONG: return GCD_INT;
    case VT_BOOL: return GCD_BOOL;
    case VT_FLOAT: return GCD_FLOAT;
    case VT_DOUBLE: return GCD_DOUBLE;
    case VT_LDOUBLE: return GCD_LDOUBLE;
    case VT_FUNC: return GCD_FUNC;
    case VT_PTR: return (t->t & VT_ARRAY) ? GCD_ARRAY : GCD_PTR;
    case VT_STRUCT: return IS_UNION(t->t) ? GCD_UNION : GCD_STRUCT;
    }
    return GCD_OTHER;
}

int gcd_type_size(const void *type, int *align)
{
    CType t = *(const CType *)type;
    int a = 0, n;
    if ((t.t & VT_BTYPE) == VT_STRUCT && t.ref->c < 0)
        return -1;                      /* incomplete */
    if ((t.t & VT_BTYPE) == VT_FUNC)
        return -1;
    if ((t.t & VT_ARRAY) && t.ref->c < 0)
        return -1;                      /* unknown length */
    n = type_size(&t, &a);
    if (align) *align = a;
    return n;
}

int gcd_type_unsigned(const void *type) { return (((const CType *)type)->t & VT_UNSIGNED) != 0; }
int gcd_type_const(const void *type) { return (((const CType *)type)->t & VT_CONSTANT) != 0; }
int gcd_type_long(const void *type) { return (((const CType *)type)->t & VT_LONG) != 0; }
int gcd_type_bitfield(const void *type) { return (((const CType *)type)->t & VT_BITFIELD) != 0; }
int gcd_type_enum(const void *type) { return IS_ENUM(((const CType *)type)->t); }

int gcd_type_char(const void *type)
{
    return (((const CType *)type)->t & VT_BTYPE) == VT_BYTE;
}

const void *gcd_type_target(const void *type)
{
    const CType *t = type;
    int bt = t->t & VT_BTYPE;
    if (bt == VT_PTR || bt == VT_FUNC)
        return &t->ref->type;           /* pointee, element, or result */
    return NULL;
}

long long gcd_type_count(const void *type)
{
    const CType *t = type;
    return (t->t & VT_ARRAY) ? t->ref->c : -1;
}

const void *gcd_type_first(const void *type)
{
    const CType *t = type;
    int bt = t->t & VT_BTYPE;
    if (bt == VT_FUNC || bt == VT_STRUCT)
        return t->ref->next;            /* first parameter or field */
    return NULL;
}

int gcd_type_variadic(const void *type)
{
    const CType *t = type;
    return (t->t & VT_BTYPE) == VT_FUNC && t->ref->f.func_type == FUNC_ELLIPSIS;
}

int gcd_type_oldstyle(const void *type)
{
    const CType *t = type;
    return (t->t & VT_BTYPE) == VT_FUNC && t->ref->f.func_type == FUNC_OLD;
}

const void *gcd_type_tag(const void *type)
{
    const CType *t = type;
    if ((t->t & VT_BTYPE) == VT_STRUCT || IS_ENUM(t->t))
        return t->ref;
    return NULL;
}

const char *gcd_tag_name(const void *tag) { return gcd_sym_name(tag); }
int gcd_tag_complete(const void *tag) { return ((const Sym *)tag)->c >= 0; }

/* --- macros ----------------------------------------------------------- */

static void gcd_quiet(void *opaque, const char *msg) { (void)opaque; (void)msg; }

/* The value of the object-like macro `name`, read as C from a buffer of
   its own: integer, floating or string constant, or 0 if it is none of
   these (a type, a statement, an expression that needs a variable...). An
   error inside unwinds to here and only drops this macro. */
static int gcd_eval_text(TCCState *s1, const char *text, const char *name, gcd_cb *cb);
static int gcd_eval_macro(TCCState *s1, const char *name, gcd_cb *cb)
{
    return gcd_eval_text(s1, name, name, cb);
}

/* The value of `text` as C, reported under `name` as gcd_eval_macro's. */
static int gcd_eval_text(TCCState *s1, const char *text, const char *name, gcd_cb *cb)
{
    jmp_buf saved;
    TCCErrorFunc *ef = s1->error_func;
    void *eo = s1->error_opaque;
    int ne = s1->nb_errors, ok = 0;
    BufferedFile *f0 = file;
    SValue *v0 = vtop;
    int flags0 = parse_flags, nocode0 = nocode_wanted;
    int len = (int)strlen(text);

    memcpy(saved, s1->error_jmp_buf, sizeof saved);
    s1->error_func = gcd_quiet;
    if (setjmp(s1->error_jmp_buf) == 0) {
        tcc_open_bf(s1, "<macro>", len);
        memcpy(file->buffer, text, len);
        parse_flags = PARSE_FLAG_PREPROCESS | PARSE_FLAG_TOK_NUM | PARSE_FLAG_TOK_STR;
        next();
        if (tok == TOK_STR) {
            /* a string literal, perhaps several adjacent ones */
            CString cs;
            cstr_new(&cs);
            while (tok == TOK_STR) {
                cstr_cat(&cs, tokc.str.data, tokc.str.size - 1);
                next();
            }
            if (tok == TOK_EOF) {
                cb->macro(cb->ctx, name, GCD_MACRO_STRING, 0, 0, 0, 0.0, cs.data, cs.size);
                ok = 1;
            }
            cstr_free(&cs);
        } else if (tok != TOK_EOF) {
            nocode_wanted = DATA_ONLY_WANTED;
            expr_const1();
            if (tok == TOK_EOF &&
                (vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM | VT_NONCONST)) == VT_CONST) {
                int bt = vtop->type.t & VT_BTYPE, a;
                CType ty = vtop->type;
                if (bt == VT_FLOAT || bt == VT_DOUBLE || bt == VT_LDOUBLE) {
                    double d = bt == VT_FLOAT ? vtop->c.f : bt == VT_DOUBLE ? vtop->c.d : (double)vtop->c.ld;
                    cb->macro(cb->ctx, name, GCD_MACRO_FLOAT, 0, 0, type_size(&ty, &a), d, NULL, 0);
                    ok = 1;
                } else if (bt != VT_STRUCT && bt != VT_FUNC && bt != VT_VOID) {
                    int kind = bt == VT_PTR ? GCD_MACRO_POINTER : GCD_MACRO_INT;
                    cb->macro(cb->ctx, name, kind, (long long)vtop->c.i, (ty.t & VT_UNSIGNED) != 0,
                              type_size(&ty, &a), 0.0, NULL, 0);
                    ok = 1;
                }
            }
        }
    }
    /* put the parser back as the walk found it, whether or not it threw */
    while (macro_stack)
        end_macro();
    while (file && file != f0)
        tcc_close();
    vtop = v0;
    parse_flags = flags0;
    nocode_wanted = nocode0;
    s1->nb_errors = ne;
    s1->error_func = ef;
    s1->error_opaque = eo;
    memcpy(s1->error_jmp_buf, saved, sizeof saved);
    return ok;
}

/* --- function-like macros ---------------------------------------------- */

static TCCState *gcd_state;   /* the parse a callback is inside, for gcd_eval */
static gcd_cb *gcd_cur;

int gcd_eval(const char *text, const char *name)
{
    return gcd_state ? gcd_eval_text(gcd_state, text, name, gcd_cur) : 0;
}

int gcd_macro_nparams(const void *sym)
{
    const Sym *s = sym, *a;
    int n = 0;
    for (a = s->next; a; a = a->next) {
        if (a->type.t)
            return -1;   /* `...` or `args...`: variadic */
        n++;
    }
    return n;
}

int gcd_macro_param(const void *sym, int i, char *buf, int size)
{
    const Sym *a = ((const Sym *)sym)->next;
    const char *s;
    while (a && i--)
        a = a->next;
    if (!a)
        return 0;
    s = get_tok_str(a->v & ~SYM_FIELD, NULL);
    if ((int)strlen(s) >= size)
        return 0;
    strcpy(buf, s);
    return 1;
}

int gcd_macro_body(const void *sym, char *buf, int size)
{
    const int *str = ((const Sym *)sym)->d;
    int t, n = 0;
    CValue cv;
    if (size > 0)
        buf[0] = 0;
    while (str) {
        const char *s;
        int len;
        TOK_GET(&t, &str, &cv);
        if (t == 0 || t == TOK_EOF)
            break;
        if (t == ' ')
            continue;
        s = t == TOK_PPJOIN ? "##" : get_tok_str(t, &cv);
        len = (int)strlen(s);
        if (n + len + 2 > size)
            return 0;
        if (n)
            buf[n++] = ' ';
        memcpy(buf + n, s, len);
        n += len;
        buf[n] = 0;
    }
    return 1;
}

/* --- the walk --------------------------------------------------------- */

static void gcd_walk(TCCState *s1, int first, int names_only, gcd_cb *cb)
{
    int i, n = tok_ident - TOK_IDENT;
    gcd_state = s1;
    gcd_cur = cb;
    for (i = first - TOK_IDENT; i < n; i++) {
        TokenSym *ts = table_ident[i];
        Sym *s;
        char name[256];
        if (!ts || ts->len >= (int)sizeof name)
            continue;
        memcpy(name, ts->str, ts->len);
        name[ts->len] = 0;
        if ((s = ts->sym_struct)) {
            /* a tag is reported as its type, whose identity is the tag */
            CType ct;
            ct.t = s->type.t & (VT_BTYPE | VT_STRUCT_MASK | VT_UNSIGNED);
            ct.ref = s;
            cb->decl(cb->ctx, GCD_TAG, name, &ct);
        }
        if ((s = ts->sym_identifier)) {
            int t = s->type.t, kind;
            if (t & VT_TYPEDEF)
                kind = GCD_TYPEDEF;
            else if (IS_ENUM_VAL(t))
                kind = GCD_ENUM_CONST;
            else if ((t & VT_BTYPE) == VT_FUNC)
                kind = (t & VT_STATIC) ? GCD_STATIC : GCD_FUNCTION;
            else
                kind = (t & VT_STATIC) ? GCD_STATIC : GCD_VARIABLE;
            cb->decl(cb->ctx, kind, name, s);
        }
        if ((s = ts->sym_define) && s->d) {
            if (names_only || (s->type.t & MACRO_FUNC))
                cb->decl(cb->ctx, (s->type.t & MACRO_FUNC) ? GCD_MACRO_FUNCTION : GCD_MACRO, name, s);
            else if (!gcd_eval_macro(s1, name, cb))
                cb->macro(cb->ctx, name, GCD_MACRO_NONE, 0, 0, 0, 0.0, NULL, 0);
        }
    }
    gcd_state = NULL;
    gcd_cur = NULL;
}

static void gcd_error(void *opaque, const char *msg)
{
    gcd_cb *cb = opaque;
    if (cb->error)
        cb->error(cb->ctx, msg);
}

int gcd_parse(const char *libpath, const char *const *sysinc, int nsys,
              const char *const *inc, int ninc, const char *const *defines, int ndef,
              const char *source, int names_only, gcd_cb *cb)
{
    TCCState *s1 = tcc_new();
    int i, ret, first;
    if (!s1)
        return -1;
    tcc_set_error_func(s1, cb, gcd_error);
    if (libpath)
        tcc_set_lib_path(s1, libpath);
    tcc_set_options(s1, "-nostdinc -nostdlib");
    for (i = 0; i < nsys; i++)
        tcc_add_sysinclude_path(s1, sysinc[i]);
    for (i = 0; i < ninc; i++)
        tcc_add_include_path(s1, inc[i]);
    for (i = 0; i < ndef; i++) {
        const char *eq = strchr(defines[i], '=');
        if (eq) {
            char name[256];
            int n = (int)(eq - defines[i]);
            if (n >= (int)sizeof name) n = (int)sizeof name - 1;
            memcpy(name, defines[i], n);
            name[n] = 0;
            tcc_define_symbol(s1, name, eq + 1);
        } else {
            tcc_define_symbol(s1, defines[i], NULL);
        }
    }
    if (tcc_set_output_type(s1, TCC_OUTPUT_OBJ) < 0) {
        tcc_delete(s1);
        return -1;
    }
    s1->gen_deps = 1;                   /* every file the source includes */
    s1->include_sys_deps = 1;

    /* tcc_compile, with the walk where the symbol tables are still alive */
    tcc_enter_state(s1);
    s1->error_set_jmp_enabled = 1;
    if (setjmp(s1->error_jmp_buf) == 0) {
        int len = (int)strlen(source);
        tcc_open_bf(s1, "<import>", len);
        memcpy(file->buffer, source, len);
        preprocess_start(s1, AFF_TYPE_C);
        tccgen_init(s1);
        tccelf_begin_file(s1);
        /* Every name is walked, TinyCC's predefined ones too: the caller
           tells the header's own names from the rest. */
        first = TOK_IDENT;
        tccgen_compile(s1);
        if (s1->nb_errors == 0)
            gcd_walk(s1, first, names_only, cb);
        tccelf_end_file(s1);
    }
    tccgen_finish(s1);
    preprocess_end(s1);
    s1->error_set_jmp_enabled = 0;
    tcc_exit_state(s1);

    ret = s1->nb_errors ? -1 : 0;
    if (ret == 0 && cb->dep)
        for (i = 0; i < s1->nb_target_deps; i++)
            cb->dep(cb->ctx, s1->target_deps[i]);
    tcc_delete(s1);
    return ret;
}
