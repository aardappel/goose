#include "cmath2.h"

int32_t cmath2_square(int32_t x) { return x * x; }

int64_t cmath2_dot(cmath2_ints a, cmath2_ints b) {
    int64_t n = a.len < b.len ? a.len : b.len, sum = 0;
    for (int64_t i = 0; i < n; i++) sum += (int64_t)a.data[i] * b.data[i];
    return sum;
}
