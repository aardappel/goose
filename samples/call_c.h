/* The C side of call_c.goose: three functions with the argument shapes an
   `extern fn` can pass. The header is included after the generated type
   declarations, so it can use the slice and struct typedefs Goose emits
   (sl_u8, sl_i32, the packed struct for `Stats`). */
#include <string.h>

typedef struct { int32_t *data; int64_t len; } gs_call_c_sl_i32;

/* CRC-32 (IEEE) over a byte slice. */
static uint32_t crc32_bytes(sl_u8 s) {
    uint32_t crc = 0xFFFFFFFFu;
    for (int64_t i = 0; i < s.len; i++) {
        crc ^= s.data[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* Fills a Goose struct through a pointer: min, max and mean of the slice. */
static void stats_of(sl_i32 xs, Stats *out) {
    int64_t sum = 0;
    out->lo = xs.len ? xs.data[0] : 0;
    out->hi = out->lo;
    for (int64_t i = 0; i < xs.len; i++) {
        int32_t v = xs.data[i];
        if (v < out->lo) out->lo = v;
        if (v > out->hi) out->hi = v;
        sum += v;
    }
    out->mean = xs.len ? (double)sum / (double)xs.len : 0.0;
}

/* Appends text to a Goose string builder: the way C hands bytes back. */
static void c_version(gs_rref out) {
    const char *s = "C library 1.0";
    gs_bld_append(out, s, strlen(s));
}
