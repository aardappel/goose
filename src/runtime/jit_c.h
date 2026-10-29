/* The C surface used by generated code in the embedded JIT. Platform types
   and implementation headers belong to the runtime built into Goose. The
   integer and pointer types follow TinyCC's target definitions. */
#ifndef GOOSE_JIT_C_H
#define GOOSE_JIT_C_H
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef __INT64_TYPE__ int64_t;
typedef unsigned __INT64_TYPE__ uint64_t;
typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __INTPTR_TYPE__ intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;
#define NULL ((void *)0)
#define INT8_MIN (-128)
#define INT8_MAX 127
#define UINT8_MAX 255
#define INT16_MIN (-32768)
#define INT16_MAX 32767
#define UINT16_MAX 65535
#define INT32_MIN (-2147483647 - 1)
#define INT32_MAX 2147483647
#define UINT32_MAX 4294967295U
#define INT64_MIN (-9223372036854775807LL - 1)
#define INT64_MAX 9223372036854775807LL
#define UINT64_MAX 18446744073709551615ULL
#define INFINITY (1.0F / 0.0F)
#define NAN (0.0F / 0.0F)

void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
void *memchr(const void *, int, size_t);
size_t strlen(const char *);
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
int abs(int);

#define GS_JIT_MATH1(fn) double fn(double); float fn##f(float);
#define GS_JIT_MATH2(fn) double fn(double, double); float fn##f(float, float);
GS_JIT_MATH1(sqrt)
GS_JIT_MATH1(sin)
GS_JIT_MATH1(cos)
GS_JIT_MATH1(tan)
GS_JIT_MATH1(asin)
GS_JIT_MATH1(acos)
GS_JIT_MATH1(atan)
GS_JIT_MATH1(exp)
GS_JIT_MATH1(log)
GS_JIT_MATH1(log2)
GS_JIT_MATH1(log10)
GS_JIT_MATH1(floor)
GS_JIT_MATH1(ceil)
GS_JIT_MATH1(round)
GS_JIT_MATH1(trunc)
GS_JIT_MATH1(fabs)
GS_JIT_MATH2(atan2)
GS_JIT_MATH2(pow)
GS_JIT_MATH2(fmod)
GS_JIT_MATH1(cbrt)
GS_JIT_MATH2(hypot)
double ldexp(double, int);
float ldexpf(float, int);
double frexp(double, int *);
float frexpf(float, int *);
#undef GS_JIT_MATH1
#undef GS_JIT_MATH2
#endif
