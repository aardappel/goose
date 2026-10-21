// The Goose standard library: `ffi` (docs/stdlib.md): reading memory a C
// library owns, handed back as a `u64` pointer by an extern bound from a
// shared library (spec §7.10, docs/reference.md §24). Each function copies
// into a builder the caller passes, so nothing foreign is ever a Goose
// value without a copy, and each trusts the pointer as every extern call
// trusts C: a wrong pointer or length is C's kind of error, not Goose's.
namespace ffi;

// The bytes at p up to its NUL, the NUL left out: a C string. Appends
// nothing and returns false for a null p.
extern "gs_ffi_cstr" fn cstr(p: u64, out: u8[>..]&) -> bool;
// The n bytes at p. Appends nothing and returns false for a null p.
extern "gs_ffi_bytes" fn bytes(p: u64, n: i64, out: u8[>..]&) -> bool;

// The fresh-string forms: empty for a null p.
fn cstr(p: u64) -> u8[>..] {
    var out: u8[>..] = [];
    cstr(p, out);
    return out;
}
fn bytes(p: u64, n: i64) -> u8[>..] {
    var out: u8[>..] = [];
    bytes(p, n, out);
    return out;
}
