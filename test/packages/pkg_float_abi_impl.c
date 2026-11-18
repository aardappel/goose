/* Compiled by the host C compiler, so the JIT fixture checks an ABI boundary
   rather than two functions agreeing on the same incorrect stack layout. */
#include <stdint.h>

#pragma pack(push, 1)
typedef struct { float x, y, z; } V3;
typedef struct { V3 v; float w; } V4;
typedef struct { uint32_t id; } Handle;
#pragma pack(pop)

/* Eightbytes of different classes, which x86-64 System V passes and returns
   in one integer and one SSE register each: TinyCC merged them into one. */
typedef struct { int64_t n; double d; } Mix;   /* INTEGER, SSE */
typedef struct { double d; int64_t n; } Xim;   /* SSE, INTEGER */

int32_t abi_mix(int32_t a, Mix m, double b, Xim x) {
    return a == 1 && m.n == 2 && m.d == 3.5 && b == 4.5 && x.d == 5.5 && x.n == 6;
}

Mix abi_mix_make(int64_t n, double d) { Mix m = { n, d }; return m; }

/* Goose's own packed layout, a field at an offset that is not a multiple of
   its alignment: the convention passes and returns such a struct in
   memory, where TinyCC used registers. */
#pragma pack(push, 1)
typedef struct { int32_t a; double b; } Packed;   /* b at offset 4 */
typedef struct { uint8_t c; int32_t v; } Small;   /* v at offset 1 */
#pragma pack(pop)

int32_t abi_packed(int32_t x, Packed p, Small s, double y) {
    return x == 1 && p.a == 2 && p.b == 3.5 && s.c == 4 && s.v == 5 && y == 6.5;
}

Packed abi_packed_make(int32_t a, double b) { Packed p = { a, b }; return p; }
Xim abi_xim_make(double d, int64_t n) { Xim x = { d, n }; return x; }

static int is3(V3 v, float x, float y, float z) {
    return v.x == x && v.y == y && v.z == z;
}

int32_t abi_sphere(V4 s, V3 origin, V3 translation, float fraction) {
    return is3(s.v, 1, 2, 3) && s.w == 4 && is3(origin, 5, 6, 7) &&
           is3(translation, 8, 9, 10) && fraction == 11;
}

int32_t abi_mesh(Handle m, V3 scale, V3 origin, V3 translation, float fraction) {
    return m.id == 42 && is3(scale, 1, 2, 3) && is3(origin, 5, 6, 7) &&
           is3(translation, 8, 9, 10) && fraction == 11;
}

int32_t abi_scalars(float a, float b, float c, float d, float e, float f,
                    float g, float h, float i, float j, double k) {
    return a == 1 && b == 2 && c == 3 && d == 4 && e == 5 && f == 6 &&
           g == 7 && h == 8 && i == 9 && j == 10 && k == 11;
}

/* A spilled HFA starts after a single float, and a double after another
   single float must still be aligned to eight bytes. */
int32_t abi_aligned(V4 a, V4 b, float c, V3 d, float e, double f) {
    return is3(a.v, 1, 2, 3) && a.w == 4 && is3(b.v, 5, 6, 7) && b.w == 8 &&
           c == 9 && is3(d, 10, 11, 12) && e == 13 && f == 14;
}
